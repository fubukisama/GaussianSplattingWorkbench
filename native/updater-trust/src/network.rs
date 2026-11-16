// SPDX-License-Identifier: GPL-3.0-or-later
use crate::policy::{validate_metadata, Offer, Profile, Result, DESCRIPTOR_LIMIT, METADATA_LIMIT};
use async_trait::async_trait;
use aws_lc_rs::digest::{Context, SHA256};
use bytes::Bytes;
use futures::stream;
#[cfg(not(feature = "fixture-profile"))]
use rustls_platform_verifier::BuilderVerifierExt;
use std::collections::BTreeMap;
use std::ffi::{c_int, c_void};
use std::future::Future;
use std::io::Write;
use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tough::{Transport, TransportError, TransportErrorKind, TransportStream};
use tough::gsw_store::strict_json;
use url::Url;

pub type CancelCallback = Option<unsafe extern "C" fn(*mut c_void) -> c_int>;
#[derive(Clone, Copy)]
pub struct Cancellation { pub callback: CancelCallback, pub context: usize }
impl std::fmt::Debug for Cancellation {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result { f.write_str("Cancellation") }
}
impl Cancellation {
    pub fn check(self) -> Result<()> {
        if self.callback.is_some_and(|callback| unsafe { callback(self.context as *mut c_void) } != 0) {
            return Err("CANCELLED");
        }
        Ok(())
    }
    async fn wait<T, F: Future<Output = T>>(self, future: F) -> Result<T> {
        tokio::pin!(future);
        loop {
            self.check()?;
            tokio::select! {
                result = &mut future => return Ok(result),
                _ = tokio::time::sleep(Duration::from_millis(100)) => {}
            }
        }
    }
}

fn https_client(_profile: &Profile, idle: Duration, total: Duration) -> Result<reqwest::Client> {
    let provider = Arc::new(rustls::crypto::aws_lc_rs::default_provider());
    let builder = rustls::ClientConfig::builder_with_provider(provider)
        .with_safe_default_protocol_versions().map_err(|_| "TLS_CONFIGURATION_FAILED")?;
    #[cfg(feature = "fixture-profile")]
    let config = {
        let mut roots = rustls::RootCertStore::empty();
        let certificates = pem::parse_many(_profile.fixture_ca_pem.as_deref().ok_or("PROFILE_INVALID")?)
            .map_err(|_| "PROFILE_INVALID")?;
        if certificates.is_empty() || certificates.len() > 8 { return Err("PROFILE_INVALID"); }
        for certificate in certificates {
            if certificate.tag() != "CERTIFICATE" { return Err("PROFILE_INVALID"); }
            roots.add(rustls::pki_types::CertificateDer::from(certificate.into_contents()))
                .map_err(|_| "PROFILE_INVALID")?;
        }
        builder.with_root_certificates(roots).with_no_client_auth()
    };
    #[cfg(not(feature = "fixture-profile"))]
    let config = builder.with_platform_verifier().map_err(|_| "TLS_CONFIGURATION_FAILED")?.with_no_client_auth();
    reqwest::Client::builder().use_preconfigured_tls(config)
        .https_only(true).no_proxy().redirect(reqwest::redirect::Policy::none())
        .connect_timeout(Duration::from_secs(5)).read_timeout(idle)
        .timeout(total).build().map_err(|_| "TLS_CONFIGURATION_FAILED")
}

// Only called after cached signed metadata was revalidated against the journal.
// A fresh installation has no resumable cache; failed attempts restart at zero
// inside its own staging root, never using another install's partial file.
async fn initial_archive_attempt(client: &reqwest::Client, profile: &Profile, offer: &Offer,
    destination: &Path, cancel: Cancellation) -> Result<()> {
    let mut url = profile.url(&offer.archive_url)?;
    for redirects in 0..=3 {
        cancel.check()?;
        let mut response = cancel.wait(client.get(url.clone()).header("Accept-Encoding", "identity").send()).await?
            .map_err(|error| if error.is_timeout() { "download_timeout" } else { "download_network_error" })?;
        if matches!(response.status().as_u16(), 301 | 302 | 303 | 307 | 308) {
            if redirects == 3 { return Err("download_redirect_limit"); }
            let location = response.headers().get(reqwest::header::LOCATION).ok_or("download_redirect_invalid")?
                .to_str().map_err(|_| "download_redirect_invalid")?;
            if location.len() > 8192 { return Err("download_redirect_invalid"); }
            let joined = url.join(location).map_err(|_| "download_redirect_invalid")?;
            url = profile.url(joined.as_str()).map_err(|_| "download_redirect_forbidden")?;
            continue;
        }
        if response.status() == reqwest::StatusCode::SERVICE_UNAVAILABLE { return Err("download_service_unavailable"); }
        if response.status() != reqwest::StatusCode::OK { return Err("download_http_error"); }
        let headers = response.headers();
        if headers.get_all(reqwest::header::CONTENT_ENCODING).iter().any(|v|
            !v.as_bytes().eq_ignore_ascii_case(b"identity")) { return Err("download_content_encoding"); }
        if headers.get_all(reqwest::header::CONTENT_LENGTH).iter().any(|v|
            v.to_str().ok().and_then(|value| value.parse::<u64>().ok()) != Some(offer.archive_length)) {
            return Err("download_length_mismatch");
        }
        let parent = destination.parent().ok_or("download_path_unsafe")?;
        let mut file = tempfile::Builder::new().prefix(".gsw-first-download-").tempfile_in(parent)
            .map_err(|_| "download_write_failed")?;
        let mut hash = Context::new(&SHA256);
        let mut bytes = 0u64;
        while let Some(chunk) = cancel.wait(response.chunk()).await?
            .map_err(|error| if error.is_timeout() { "download_timeout" } else { "download_network_error" })? {
            bytes = bytes.saturating_add(chunk.len() as u64);
            if bytes > offer.archive_length { return Err("download_length_mismatch"); }
            file.write_all(&chunk).map_err(|_| "download_write_failed")?;
            hash.update(&chunk);
        }
        if bytes != offer.archive_length { return Err("download_length_mismatch"); }
        let actual: String = hash.finish().as_ref().iter().map(|byte| format!("{byte:02x}")).collect();
        if actual != offer.archive_sha256 { return Err("download_hash_mismatch"); }
        cancel.check()?;
        file.as_file().sync_all().map_err(|_| "download_write_failed")?;
        cancel.check()?;
        file.persist_noclobber(destination).map_err(|_| "download_publish_failed")?;
        return Ok(());
    }
    Err("download_redirect_limit")
}

pub async fn fetch_initial_archive(profile: &Profile, offer: &Offer, destination: &Path, cancel: Cancellation) -> Result<()> {
    let total = Duration::from_secs(30 * 60);
    let client = https_client(profile, Duration::from_secs(30), total)?;
    tokio::time::timeout(total, async {
        for attempt in 0..3 {
            let outcome = initial_archive_attempt(&client, profile, offer, destination, cancel).await;
            match outcome {
                Err("download_service_unavailable" | "download_timeout" | "download_network_error") if attempt < 2 => {
                    cancel.wait(tokio::time::sleep(Duration::from_millis(500 << attempt))).await?;
                }
                Err("download_service_unavailable") => return Err("download_http_error"),
                _ => return outcome,
            }
        }
        Err("download_network_error")
    }).await.map_err(|_| "download_total_timeout")?
}

#[derive(Clone, Debug)]
pub struct SecureTransport {
    client: reqwest::Client,
    profile: Arc<Profile>,
    metadata_base: String,
    targets_base: String,
    pub raw: Arc<Mutex<BTreeMap<String, Vec<u8>>>>,
    cached: Option<Arc<BTreeMap<String, Vec<u8>>>>,
    cancel: Cancellation,
    pub failure: Arc<Mutex<Option<&'static str>>>,
}
impl SecureTransport {
    pub fn new(profile: Profile, channel: &str, cancel: Cancellation, cached: Option<BTreeMap<String, Vec<u8>>>) -> Result<Self> {
        let channels = profile.channels.get(channel).ok_or("PROFILE_INVALID")?;
        let client = https_client(&profile, Duration::from_secs(5), Duration::from_secs(15))?;
        Ok(Self {
            client, metadata_base: channels.metadata_base.clone(), targets_base: channels.targets_base.clone(),
            profile: Arc::new(profile), raw: Arc::new(Mutex::new(BTreeMap::new())),
            cached: cached.map(Arc::new), cancel, failure: Arc::new(Mutex::new(None)),
        })
    }
    fn classify(&self, url: &Url) -> Result<(&'static str, usize)> {
        self.profile.url(url.as_str())?;
        let filename = url.path().rsplit('/').next().ok_or("URL_REJECTED")?;
        if url.as_str().starts_with(&self.metadata_base) && url.query().is_none() {
            for role in ["root", "timestamp", "snapshot", "targets"] {
                let suffix = format!("{role}.json");
                if filename == suffix || filename.strip_suffix(&format!(".{suffix}"))
                    .is_some_and(|prefix| !prefix.is_empty() && prefix.bytes().all(|b| b.is_ascii_digit())) {
                    return Ok((role, METADATA_LIMIT));
                }
            }
        }
        if url.as_str().starts_with(&self.targets_base) && url.query().is_none()
            && (filename == "release.json" || filename.strip_suffix(".release.json")
                .is_some_and(|hash| crate::policy::is_hex(hash, 64))) {
            return Ok(("release", DESCRIPTOR_LIMIT));
        }
        Err("URL_REJECTED")
    }
    async fn download(&self, original: Url) -> Result<Option<Vec<u8>>> {
        let (role, maximum) = self.classify(&original)?;
        if let Some(cached) = &self.cached {
            // No root discovery is possible in offline revalidation: the saved
            // highest accepted root is the supplied trust anchor, not a feed.
            if role == "root" { return Ok(None); }
            return Ok(cached.get(role).cloned());
        }
        let mut url = original;
        for redirects in 0..=3 {
            self.cancel.check()?;
            self.profile.url(url.as_str())?;
            let response = self.cancel.wait(self.client.get(url.clone()).header("Accept-Encoding", "identity").send()).await?
                .map_err(|_| "NETWORK_FAILED")?;
            if response.status().is_redirection() {
                if redirects == 3 { return Err("REDIRECT_REJECTED"); }
                let location = response.headers().get(reqwest::header::LOCATION).ok_or("REDIRECT_REJECTED")?
                    .to_str().map_err(|_| "REDIRECT_REJECTED")?;
                url = url.join(location).map_err(|_| "REDIRECT_REJECTED")?;
                continue;
            }
            if response.status() == reqwest::StatusCode::NOT_FOUND { return Ok(None); }
            if response.status() != reqwest::StatusCode::OK { return Err("NETWORK_FAILED"); }
            if response.content_length().is_some_and(|n| n > maximum as u64)
                || response.headers().get(reqwest::header::CONTENT_ENCODING).is_some_and(|v| v.as_bytes() != b"identity") {
                return Err("RESPONSE_LIMIT");
            }
            let mut response = response;
            let mut bytes = Vec::new();
            while let Some(chunk) = self.cancel.wait(response.chunk()).await?.map_err(|_| "NETWORK_FAILED")? {
                if bytes.len().saturating_add(chunk.len()) > maximum { return Err("RESPONSE_LIMIT"); }
                bytes.extend_from_slice(&chunk);
            }
            let value = strict_json(&bytes, maximum).map_err(|_| "JSON_REJECTED")?;
            if role != "release" { validate_metadata(&value, role)?; }
            self.raw.lock().map_err(|_| "INTERNAL_ERROR")?.insert(role.to_owned(), bytes.clone());
            return Ok(Some(bytes));
        }
        Err("REDIRECT_REJECTED")
    }
}
#[async_trait]
impl Transport for SecureTransport {
    async fn fetch(&self, url: Url) -> std::result::Result<TransportStream, TransportError> {
        match self.download(url).await {
            Ok(Some(bytes)) => Ok(Box::pin(stream::once(async move { Ok(Bytes::from(bytes)) }))),
            Ok(None) => Err(TransportError::new(TransportErrorKind::FileNotFound, "redacted")),
            Err(code) => {
                if let Ok(mut failure) = self.failure.lock() { *failure = Some(code); }
                Err(TransportError::new(TransportErrorKind::Other, "redacted"))
            }
        }
    }
}
