// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeGithubPreviewFeed.h"
#include "NativeActivationFiles.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUrl>
#include <winhttp.h>
#include <array>
#include <cstdio>
#include <optional>

namespace gsw::preview {
using namespace activation;
namespace {
constexpr auto api = "https://api.github.com/repositories/1286130219/releases?per_page=100";
constexpr auto downloadPrefix = "https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/";
constexpr qint64 metadataLimit = 8 * 1024 * 1024;
constexpr qint64 packageLimit = 2LL * 1024 * 1024 * 1024;
struct Http {
  HINTERNET h{};
  ~Http() { if (h) WinHttpCloseHandle(h); }
};

bool allowed(const QUrl &url, bool metadata) {
  if (!url.isValid() || url.scheme() != QLatin1String("https") || url.port(443) != 443 ||
      url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')) || url.hasFragment()) return false;
  const auto host = url.host();
  if (metadata) return host == QLatin1String("api.github.com");
  return host == QLatin1String("github.com") || host == QLatin1String("release-assets.githubusercontent.com") ||
      host == QLatin1String("objects.githubusercontent.com");
}

using Sink = std::function<void(const char *, qsizetype)>;
void stream(const QString &address, qint64 limit, const Sink &sink) {
  const bool metadata = address == QLatin1String(api);
  const auto original = QUrl(address, QUrl::StrictMode);
  if (!allowed(original, metadata)) fail("preview_url_rejected");
  const auto send = [&](const char *data, qsizetype n, qint64 &received) {
    if (n < 0 || n > limit - received) fail("preview_length_mismatch");
    received += n;
    sink(data, n);
  };
  std::array<char, 65536> buffer{};
  qint64 received = 0;
#ifdef GSW_PREVIEW_FIXTURE
  // Build-time-only substitute of the external transport. No bypass is
  // present in the installed target, not even a runtime switch.
  auto fixture = qEnvironmentVariable("GSW_GITHUB_PREVIEW_FIXTURE");
  const auto locator = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gsw-fixture-issuer.txt"));
  if (fixture.isEmpty() && QFileInfo::exists(locator)) fixture = QString::fromUtf8(read(locator, 4096)).trimmed();
  if (!fixture.isEmpty()) {
    QFile input; heldRead(input, QDir(fixture).filePath(metadata ? QStringLiteral("releases.json") : original.fileName()));
    while (!input.atEnd()) {
      const auto n = input.read(buffer.data(), buffer.size());
      if (n <= 0) fail("preview_network_failed");
      send(buffer.data(), n, received);
    }
    return;
  }
#endif
  Http session{WinHttpOpen(L"GaussianSplattingWorkbench-Preview/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.h || !WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 20000)) fail("preview_network_failed");
  QElapsedTimer deadline; deadline.start();
  QUrl current = original;
  for (int hops = 0; hops <= 3; ++hops) {
    if (!allowed(current, metadata)) fail("preview_redirect_forbidden");
    const auto host = current.host().toStdWString();
    Http connection{WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.h) fail("preview_network_failed");
    auto path = current.path(QUrl::FullyEncoded);
    if (current.hasQuery()) path += QStringLiteral("?") + current.query(QUrl::FullyEncoded);
    const auto target = path.toStdWString();
    Http request{WinHttpOpenRequest(connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH | WINHTTP_FLAG_ESCAPE_DISABLE)};
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!request.h || !WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
      fail("preview_network_failed");
    const wchar_t *headers = metadata ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                                       : L"Accept: application/octet-stream\r\n";
    if (!WinHttpSendRequest(request.h, headers, DWORD(-1), nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) fail("preview_network_failed");
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                             &status, &bytes, nullptr)) fail("preview_network_failed");
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      wchar_t redirect[8192]{}; DWORD size = sizeof(redirect);
      if (hops == 3 || !WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, nullptr, redirect, &size, nullptr))
        fail("preview_redirect_forbidden");
      current = current.resolved(QUrl(QString::fromWCharArray(redirect, int(size / sizeof(wchar_t))),
                                      QUrl::StrictMode));
      continue;
    }
    if (status != 200) fail(status == 403 || status == 429 ? "preview_rate_limited" : "preview_network_failed");
    for (;;) {
      if (deadline.elapsed() > (metadata ? 60000 : 30 * 60 * 1000)) fail("preview_timeout");
      DWORD n = 0;
      if (!WinHttpReadData(request.h, buffer.data(), DWORD(buffer.size()), &n)) fail("preview_network_failed");
      if (!n) return;
      send(buffer.data(), n, received);
    }
  }
  fail("preview_redirect_forbidden");
}
QByteArray bytes(const QString &url, qint64 max) {
  QByteArray out;
  stream(url, max, [&](const char *p, qsizetype n){ out.append(p, n); });
  return out;
}

qint64 sequenceOf(const QString &tag) {
  static const QRegularExpression re(QStringLiteral(
      R"(^v[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}-native-preview\.([0-9]{8})\.([1-9][0-9]?)$)"));
  const auto match = re.match(tag);
  return match.hasMatch() ? match.captured(1).toLongLong() * 100 + match.captured(2).toLongLong() : 0;
}
Asset asset(const Json &release, const QString &tag, const QString &name) {
  const auto &items = release.at("assets");
  if (!items.is_array() || items.size() > 200) fail("preview_assets_invalid");
  std::optional<Asset> match;
  for (const auto &entry : items) {
    if (string(entry.at("name")) != name) continue;
    if (match || entry.at("state") != "uploaded") fail("preview_assets_invalid");
    const auto sum = string(entry.at("digest"));
    if (!sum.startsWith(QLatin1String("sha256:"))) fail("preview_digest_missing");
    const auto h = sum.mid(7); sha(h);
    const auto url = string(entry.at("browser_download_url"));
    if (url != QString::fromLatin1(downloadPrefix) + tag + QLatin1Char('/') + name) fail("preview_url_rejected");
    match = Asset{name, url, h, integer(entry.at("size"), 1, packageLimit)};
  }
  if (!match) fail("preview_asset_missing");
  return *match;
}
}

Release resolve(qint64 runningSequence) {
  const auto list = parse(bytes(QString::fromLatin1(api), metadataLimit), metadataLimit);
  if (!list.is_array() || list.size() > 100) fail("preview_feed_invalid");
  const Json *newest = nullptr;
  Release out;
  for (const auto &item : list) {
    if (item.at("draft") != false || item.at("prerelease") != true) continue;
    const auto tag = string(item.at("tag_name"));
    const auto seq = sequenceOf(tag);
    if (seq && seq == out.sequence) fail("preview_sequence_ambiguous");
    if (seq > out.sequence) { newest = &item; out.sequence = seq; out.tag = tag; }
  }
  if (!newest || out.sequence <= runningSequence) return out;
  if (newest->at("immutable") != true) fail("preview_release_mutable");
  const auto descriptor = asset(*newest, out.tag, QString::fromLatin1(manifestName));
  if (descriptor.size > 256 * 1024) fail("preview_manifest_invalid");
  const auto raw = bytes(descriptor.url, descriptor.size);
  if (raw.size() != descriptor.size || digest(raw) != descriptor.sha256) fail("preview_manifest_digest_mismatch");
  const auto m = parse(raw, 256 * 1024);
  const auto &offer = m.at("offer");
  const auto sequence = integer(m.at("sequence"), 1, 9999999999LL);
  const auto source = string(m.at("sourceCommit"));
  out.build = QStringLiteral("preview-%1-%2").arg(sequence).arg(source.left(12));
  if (m.at("schema") != "gsw.github_preview/1" || m.at("product") != product || m.at("platform") != "windows-x64" ||
      string(m.at("tag")) != out.tag || sequence != out.sequence || !QRegularExpression(QStringLiteral("^[0-9a-f]{40}$")).match(source).hasMatch() ||
      m.at("archive") != archiveName || offer.at("schema") != "gsw.update_offer/1" || offer.at("product") != product ||
      offer.at("platform") != "windows-x64" || offer.at("sourceCommit") != m.at("sourceCommit") ||
      string(offer.at("buildId")) != out.build || offer.at("releaseSequence") != sequence) fail("preview_manifest_invalid");
  const auto setup = string(m.at("setup"));
  if (!QRegularExpression(QStringLiteral(R"(^Gaussian-Scene-Workbench-[A-Za-z0-9.-]{1,64}-windows-x64-setup\.exe$)")).match(setup).hasMatch())
    fail("preview_manifest_invalid");
  out.setup = asset(*newest, out.tag, setup);
  out.archive = asset(*newest, out.tag, QString::fromLatin1(archiveName));
  if (string(offer.at("archiveSha256")) != out.archive.sha256 || offer.at("archiveLength") != out.archive.size)
    fail("preview_archive_identity_mismatch");
  out.display = string(offer.at("displayVersion")).left(120);
  out.offer = QByteArray::fromStdString(offer.dump());
  out.manifestHash = descriptor.sha256; out.newer = true;
  return out;
}

void download(const Asset &asset, const QString &dest, const Progress &progress) {
  if (!ordinary(dest, true)) fail("preview_cache_invalid");
  OutputDirectoryLease parents(nullptr);
  parents.acquire(dest);
  if (QFileInfo::exists(dest)) {
    QFile file; heldRead(file, dest);
    if (file.size() == asset.size && hash(file) == asset.sha256) return;
  }
  QStorageInfo volume(QFileInfo(dest).absolutePath());
  if (!volume.isValid() || volume.bytesAvailable() < asset.size + reserveSpace) fail("download_insufficient_space");
  QSaveFile out(dest);
  out.setDirectWriteFallback(false);
  if (!out.open(QIODevice::WriteOnly)) fail("preview_cache_invalid");
  QCryptographicHash sum(QCryptographicHash::Sha256);
  qint64 total = 0;
  stream(asset.url, asset.size, [&](const char *p, qsizetype n) {
    if (out.write(p, n) != n) fail("preview_write_failed");
    sum.addData(QByteArrayView(p,n)); total += n; progress(total, asset.size);
  });
  if (total != asset.size || QString::fromLatin1(sum.result().toHex()) != asset.sha256) fail("preview_package_digest_mismatch");
  if (!out.commit()) fail("preview_write_failed");
}
} // namespace gsw::preview
