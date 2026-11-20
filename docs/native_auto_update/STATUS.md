# Auto-update delivery status

Status is intentionally split: whole-updater implementation, full isolated
acceptance, local managed delivery and public online operation remain incomplete.
On 2026-10-30 the user explicitly selected a narrower closeout: an independent
e141 research-preview file bundle, without completing the whole updater. That
file-only preview is now assembled and verified; it is not managed activation,
fullAcceptance, a public release or production signing/channel authority.

预览文件包已完成；公开自动更新仍关闭，现有安装和快捷方式未切换。
独立プレビューファイルは完成しました。公開自動更新は無効で、既存環境は切り替えていません。
See the [three-language preview handoff](PREVIEW_E141_20261030.md) for exact
source, archive digest, local readback and unfinished gates.

## Restricted first installation (2026-11-20)

The last failing job in the November 13 updater run now passes. The full
[Native Auto Update run 44689230561, attempt 1](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/44689230561)
passed **10/10 jobs** on a push of `347ebe2ecd75ec2e62728931c7f9d6be13f08423`
(tree `0dc8a75ca5a64d0b50bef6528b2370d6d1f0fa3f`). This is the source tested below.

- **Fresh install:** In the [restricted Windows job](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/44689230561/job/136130783016),
  the medium-integrity client, with no enabled administrator group or discoverable
  developer tools, downloaded the signed test archive once and returned
  `BOOTSTRAP_COMMITTED` (exit 0). Independent readback checked the active pointer,
  online receipt, 7 bootstrap files and 34 version files against the expected bytes.
- **First launch:** After the refusal checks, the installed stable launcher started
  the executable in the installed `fixture-a` slot. Its Qt window was shown, its
  viewport painted, and the process exited normally. This is a separate launch
  observation; the restricted-token evidence above belongs to the installer.
- **Refusals:** An occupied destination was preserved. A changed bootstrap manifest,
  wrong signing key, forbidden archive redirect and corrupt archive each returned
  exit 2 without creating a destination or leaving a staging directory. The wrong
  key was rejected before any archive request; the valid installation stayed intact.

The [first-install evidence ZIP](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/44689230561/artifacts/14607379785)
has SHA-256 `069b9aba96abac20f7d8cb710c18d86fe57fab5c6297473d00f8687e2c21277e`;
the installed fixture EXE has SHA-256 `879237ba452db295073e5b0d8e0f6bb9ec1ddf747f6f44751097ec28edf0c85a`.
A [separate automatic-update job](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/44689230561/job/136130783057)
showed the same stable entry starting B after A's normal exit, with user data
unchanged. These are isolated Qt fixtures, not a production publisher signature,
a physical clean OS, protocol-1 migration, or a change to the locally installed app.

中文：隔离 Windows 已验证非管理员首装、从固定入口实际打开窗口，以及坏包不留下可用安装。
这不是正式发布，也未替换本机版本。

日本語：隔離した Windows 環境で、管理者権限なしの新規インストール、固定ランチャーからの
実ウィンドウの起動、不正なパッケージの拒否を確認しました。本番配信や既存環境の更新ではありません。

## Signed automatic TLS diagnosis — 2026-11-13

The [original failure](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/41045738943)
is reproduced in the [source-bound diagnostic](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43387777747/job/131760226143),
attempt 1, commit `567495e00534836a9d1e9765cf3bfdc6100db198`.
The real Controller's Helper loads app-local Schannel, receives only the leaf
certificate and reports `QSslError::UnableToGetIssuerCertificate` before sending
the archive GET. Signed metadata had already reached the verifier. The same
Helper and installation context with the inherited SDK plugin path use external
OpenSSL and prepare B; removing only that path reproduces the Schannel failure.
These standalone probes leave the active pointer unchanged and do not replace
the failing automatic verdict. The diagnostic artifact is `14019611479`
(outer SHA-256 `824c5d36b1660f210c5bfc13396296e71370615ee46e0d03dac3995caecb9864`).

The isolated HTTPS server now presents its ephemeral issuer with the leaf.
The issuer is trusted only through the explicit fixture CA; presentation
alone grants no trust. The production Helper, downloader, TLS/hostname
verification, TUF/signature rules, and Windows certificate store are unchanged.
The Schannel-specific download contract verifies both acceptance with the
fixture CA and rejection without it. The real Controller must still activate
B through the original background flow and prove both executable hashes,
the same launcher, continued A-window identity and unchanged protected data.
This is test-fixture repair, not an application version update or public rollout.
See [Qt's pinned Schannel chain handling](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/tls/schannel/qtls_schannel.cpp#L2127-L2234).

On the exact repair `5375f0154d34502cab367cff845bc1efe322c651`,
tree `1e9ff04170bfee05a1ab6f7b13a17eef2c14f553`, the [real automatic flow](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43390113088/job/131767723609)
passed in run `43390113088` attempt 1. The Controller used the owned packaged
runtime without the SDK plugin path, downloaded signed B over real HTTPS,
prepared and activated it; the same stable entry opened B while A retained
its original window/process and foreground state. Bootstrap, A slot, project
and paused-state sentinel bytes were unchanged. Evidence artifact `14020995479`
(outer SHA-256 `9ad3028e0c0f1df0556830386522afdf66d20fdb912a2cfa967a043a322bebbb`)
records A EXE SHA-256 `da889ec2df93531ffcaa8f8bcf300a7c9d7c82fa9d01374da5b0e24500ee67e5`
and B EXE SHA-256 `30a60319f35fc5a7ea833b7856cde3bbbb199e4e4ee2863323989726f42dd1e4`.
This verifies the isolated Qt fixture, not GPU training, first installation,
interrupted preparation recovery, or a formal GSW package.

The [separate HTTPS component job](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43390113088/job/131767723684)
remains **FAIL** (3/4 CTests). Its default TLS suite and explicit Schannel
untrusted-certificate refusal passed, but the Schannel positive payload case
reported `download_network_error`, not the original issuer error. The reason
for this distinct result is not established; do not drop the case or infer
that the positive contract passed. Artifact `14020343484` retains the logs and
JUnit result. The [restricted first-bootstrap job](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43390113088/job/131767723626)
also remains **FAIL**: `0xC0000142` before main, no child JSON. These gates
prevent full isolated acceptance even with the automatic lifecycle row green.

中文：修正隔离 HTTPS 夹具的证书链；生产校验、公开发布与本机安装均未改变。
远端真实 A→B 已通过；Schannel 独立下载正例与首次引导仍未通过，不能交付正式版本。
日本語：隔離 HTTPS フィクスチャの証明書チェーンを修正しました。本番の検証、公開、ローカルのインストールは変更していません。
リモートの A→B は成功しましたが、Schannel の単独ダウンロード正例と初回起動は未合格です。

### Schannel payload and restricted bootstrap follow-up

[Diagnostic run 43392860614](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43392860614)
at `e881372e427c89b69a657bce466429f0284b3740` observed encryption and valid
HTTP 200/206 headers before Schannel received `RemoteHostClosedError` on a
partial payload. The original attempts closed in 52–94 ms, not at the
1,500 ms idle timeout; a longer diagnostic bound did not remove the close.
The fixture now sends a bounded orderly TLS shutdown for normal responses,
while the intentional interruption endpoints still close abruptly.
[Native contracts at 9d60f1c](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43393720684/job/131779513106),
attempt 1, passed all four CTests, including full Schannel bytes/length/hash,
strong-validator resume and refusal of an untrusted issuer. Artifact
`14021708902` has outer SHA-256
`a40d01afd19f27f80208a77ae8a4e0c5ae33f7e4507d81c8a73fed793d05b12d`.
The production downloader, timeout/retry bounds, trust and signature rules
are unchanged; this repairs the remote HTTPS fixture, not a public release.
The [actual automatic A→B flow](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43393720684/job/131779513213)
also passed at the same commit and attempt. Artifact `14021134481`
(outer SHA-256 `2204a128ff2eedfd14be11f980dc1bbf037b8eba6ceed60d30ffeba0ebc1b826`)
records the same stable entry starting B, A remaining alive and unchanged
bootstrap/user data. The observed executable SHA-256 values are
`1deb63ea719548e4449c96a2b371cc1d327af0f5e6b1cd6ac0e7483ea0d42b59` (A) and
`9a34ce0581256185046754bc57800ec41285cb123b5e8881b24d5520fcf5fe94` (B).

The same restricted token and private desktop in the diagnostic reproduced
`0xC0000142` with `CREATE_NO_WINDOW`, but reached the marker with
`DETACHED_PROCESS`. On [the actual bootstrap endpoint](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/43393720684/job/131779513228)
at `9d60f1c58ad257c57db8ab96ff6ce8a504931475`, the detached process reached
main and returned `BOOTSTRAP_NOT_IMPLEMENTED` (exit 2), with the original
restricted medium-integrity identity, no administrator or developer tooling,
and no target mutation. At that commit the `BOOTSTRAP_COMMITTED` assertion
still failed: the installer was unimplemented, not blocked before main.
Artifact `14021818124` has outer SHA-256
`fde60eac1e961342dfa9334cf21789153a712191fc42ea7c05b4f1d62252f83d`.
No first installation, migration, production signing, or local delivery is
inferred. See [Windows process creation flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)
and [Python TLS shutdown](https://docs.python.org/3.12/library/ssl.html#ssl.SSLSocket.unwrap).

中文：在 11 月 13 日的版本中，Schannel 完整下载、断流续传和拒绝未受信证书已通过。受限首装端点能启动，但当时还没有安装实现。
日本語：11 月 13 日時点で、Schannel の完全なダウンロード、切断後の再開、未信頼証明書の拒否は合格。制限付き初回導入は起動できましたが、インストールは当時未実装でした。

| Evidence | Exact source | Result | Scope |
| --- | --- | --- | --- |
| [e141 exact-source preview](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40840529308) / [Engine/2](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40840529279) | `e141f382cbedab53c8ea62e7ba9f8ea0e99c4094` | Native Windows all3jobs succeed, including175/175CTest; official disabled Engine/2, Security40840529288 and Installer40840529338 succeed. Provider-digest-bound App+Engine files are independently merged/read back:33660files,16092canonical source files. ZIP550800277B / SHA c28456b9a749abf6161b3fcd16aa7bc50f716aca99a6852b3e552ae3b8fab884 | preview files only; NOT_CONFIGURED, no local payload execution or existing installation/shortcut mutation |
| [e141 remaining updater boundary](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40840529315) | `e141f382cbedab53c8ea62e7ba9f8ea0e99c4094` | 8of10jobs pass. Actual latest-health identity-CAS, signed recovery chooser/three languages and2rollback permission/sharing cases now pass. Automatic UI still reports download_tls_failed; restricted first-bootstrap still exits0xC0000142 before main | overall updater workflow FAIL, retained unchanged; no complete updater/first-install/migration/GPU-resume claim |
| [First contracts](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40656542985) | `78579110cd54d533f7ff4c9f50eae54fe71b769e` | Expected RED: real HTTPS returns `download_not_implemented`; archive/gate CLIs not yet present | isolated, Windows Server2022, Qt6.8.3, Python3.12 |
| [Activation RED](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40658445549) | `9cc904a2a7576b93d31fe14f76a6aa1670013631` | `activation_not_implemented` asserted on the actual native call | isolated |
| [First download/archive/gate GREEN](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40658378285) | `547b2378f52c12241fbf48b99d4f469f9813dccb` | HTTPS succeeds; hidden source archive and unapproved-policy refusal pass; activation still RED | isolated |
| [Activation GREEN and resume RED](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40659572459) | `b4e46584b0601b6e2fcb372b66b60b683d868771` | Native full archive publishes B and preserves A/bootstrap; cancel-resume contract fails at retained-prefix assertion | isolated, no signed end-to-end claim |
| [First trust preparation](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40660699882) | `c50bfde` | Real fixture issuer succeeds; Rust dependency build hits deep Windows include path, before trust behavior | isolated infrastructure failure, not signature RED |
| [Five component groups GREEN](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40718734156) | `2e0cac42283619f96aa3864eacfe412608005d86` | TUF11 tests including real expiry; stable-entry, UI7 tests including no foreground change, native contracts and source/policy fixtures pass | isolated; not formal GSW installation/health or public channel acceptance |
| [Developer and downgrade regression](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40745705669) | `82e7132` | Six groups pass; actual source closure still fails, with a bounded metadata diagnostic | isolated; overall failure, not delivery approval |
| [Current confirmed source boundary](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40800050974) | `72472bb21c015c880b3e43af217c7afccfd8f35d` | Six groups pass. Actual registry closure reaches independent raw-archive checks but rejects the standard Cargo vendor projection; the original archive remains retained | isolated; source closure NOT passed |
| [Actual full source closure](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40811574832) | `8e9dcb8898343fae67e29dce6cffea3683264660` | Source closure PASS:188 registry archives,3209 exact Git files,12515 original registry files,12446 standard vendor files,69 filtered metadata files retained in raw archives. Normal approved redirect/UI/trust/developer tests pass. Health has genuine lifecycle failures under diagnosis; profile functionality passes but evidence upload needs workspace plumbing | isolated; not delivery approval or application acceptance |
| [Health and retry diagnostic boundary](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40817655279) | `9f939b3e25bc2a064a81aa5be615961651e932c7` | Original A-B-badC-A lifecycle passes; health alone is UNCONFIRMED. Retained traces show Qt construction at2.374s, painted readiness at6.004s and normal exit0 at6.157s, after the fixture-only1.5s pipe observation had expired. Actual HTTPS503 retry is RED with `download_http_error`; source/UI/trust/developer/policy groups pass | isolated diagnostic evidence; fixes require a new remote GREEN |
| [Live window and owned CPU worker](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40830109112) | `e004cfe359a74ae0d427cb3c49c1f9840c5237e9` | Seven jobs pass, including real old-A/new-B overlap, retained actual-worker birth/image/lineage,46ticks during signed installation, full disk-space UI language rows and corrupt-pointer recovery entry/cancel. Source closure stops at a fixture fresh-directory collision; first bootstrap remains a pre-main restricted-token lab failure | isolated Qt/CPU/entry scope, not whole-packet, formal GSW, GPU resume or local managed delivery |
| [Expanded real behavior boundary](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40834717543) | `0ec86155d8e5480bb9a7b9ad23e583120c8d8944` | Actual188-registry/source closure passes with3267 exact Git files. New semantic REDs identify older A overwriting B health, absent trusted Recovery chooser and automatic B download TLS failure. Bootstrap remains a lab pre-main failure, not an installer capability verdict | isolated; corrections/diagnostics need their own next exact-source results, no final delivery approval |
| [Compile-time profile evidence](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40817655279/job/123262413639) | `9f939b3e25bc2a064a81aa5be615961651e932c7` | Default/configured probes4tests and unsafe-input rejection35subcases pass, with the corrected in-workspace evidence upload | isolated developer probes only; no production package configured |
| [Official disabled engine component](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40817655291) | `9f939b3e25bc2a064a81aa5be615961651e932c7` | Official Helper default returns NOT_CONFIGURED; full component ZIP, exact Git/raw-crate source, fresh merge and badZIP/wrongsource/runtime-conflict rejection pass. Uploaded artifact12873258497 is retained | component acceptance only; merge uses a non-executable owned app fixture, not formal GSW startup |
| [Health/503 consistency GREEN](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40818699252) | `e48d535683d5d5269e422971e53c7ea104f9fd4e` | All8 component jobs pass, including authenticated fixture health, bounded real503 retry, source closure and profile evidence upload. Independent official disabled Engine run40818699154 also passes | isolated components; applicable packet interruption/resource/recovery/first-migration/live gaps remain |
| [Resource failures and interruption RED](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40820051072) | `7ca9d14d790d2a6f9f950199935b90d813b74891` | Real ACL refusal/retry, held-current refusal/retry and actual53,628,928B available on a fresh64MiB D-backedVHD pass without changing old files. Interrupted HTTPS body is RED at `download_network_error`; four process-fault cases are RED because the separately compiled fixture checkpoints are not yet connected | partial SPACE activation evidence; not download/rollback-space coverage or whole-packet acceptance |
| [Actual interruption recovery and next Range boundary](https://github.com/fubukisama/Gaussian-Scene-Workbench/actions/runs/40820732194) | `722ffd9e005cfe21d4af22f27119d3df4b9a0c82` | Four real process-termination/cross-process-lock/independent app-local console-slot-probe cases pass. Interrupted HTTPS retry passes.13 download rows pass; changed-ETag and invalid-Content-Range rows are semantic RED. Health positive plus missing/late/wrong-peer neutral fixture cases pass | shared-core developer-admission/Qt-fixture scope; not formal GSW, online first installation or GPU optimizer acceptance |
| [Independent entry and capacity boundary](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40824539270) | `de8eeb6b547fdbe2640b2156a77e57ddd5bfe85b` | Recovery entry/cancel and six lifecycle/health cases pass. Read-only startup-lock and known-length download preflight have actual REDs. Live B installation now succeeds, but the CPU harness incorrectly equates a Windows venv redirector PID with its interpreter PID. Bootstrap exits before main with no JSON; this is a lab failure, not bootstrap capability RED | scoped synthetic fixtures; no completed chooser, first installation, GPU resume or local managed delivery |
| [Capacity/lock GREEN and diagnostic limits](https://github.com/fubukisama/GaussianSplattingWorkbench/actions/runs/40827898599) | `9db10d37bb0e9d2ce60d38d91c573dab530a6d2e` | Native contracts and real low-space download preflight pass; default/profile/trust/policy pass. Download-space UI row passes all3languages; second data row collides in fixture initialization. OS evidence confirms controller→venv shim→actual CPU worker. Source-only guard still pins the pre-rename repository name; closure algorithm is not reached. Bootstrap token is verified but child exits0xC0000142 before main | fixes/diagnostic follow-ups pending; not final exact-source acceptance |

Health confirmation/recovery, normal redirect/retry and compile-time production
profile configuration now have independent first-RED slices. Their queued or
running checks are not PASS evidence. Production profile syntax probes are
separate developer products and never configure a production library/package.

The health consistency fix removes the fixture-only1.5s observation shortcut
and uses the existing product15s bound in both builds. It does not increase a
product limit, move the clock origin, change the fixture100/150ms lifecycle or
25s test bound, or weaken process-birth/image/window/nonce/ACK checks. Missing
ACK, startup beyond the bound and normal exit without proof remain neutral
UNCONFIRMED outcomes, not evidence of a bad version or automatic rollback.

The earlier Qt A/B lifecycle fixture uses real windows and the real stable
launcher/helper, but its scope is explicitly not the formal GSW renderer or
3DGS/2DGS full optimizer-state training acceptance. No such quality or backend
claim is inferred from a lifecycle or UI test.

Applicable isolated gaps remain separate from public authorization: the remaining
first-install path/platform matrix and authenticated migration of existing bootstraps,
interrupted prepared-state restart recovery, concurrent capacity loss, permanently hung-helper
close and remaining full matrices. Real contention/termination, bounded resource
cases, Qt/CPU continuity and the test-root recovery chooser have scoped GREEN
evidence; they do not establish formal GSW/GPU optimizer acceptance. The completed
research-preview file bundle is not DEL-01 managed delivery and does not upgrade
existing current/shortcut entry points.

Production is not configured: no production root, approved complete release
policy, protected signing environment or public channel promotion. Ordinary
main pushes run isolated CI only. Local GSW/training has not been launched,
restarted, terminated or modified by these acceptance runs.
