# Fresh managed installation / 受管版首次安装 / 管理版の新規インストール

## English

The separate Windows x64 carrier installs only to a new per-user location whose
parent already exists. Its build pins the SHA-256 of `bootstrap/bootstrap.json`
beside the executable. The distributor must independently authenticate the
carrier and its provenance: a hash from the same download does not authenticate
the publisher. The kit lists the stable launcher, the correctly configured
native update Helper and their deployed runtime files. No Git, Python, GitHub
account, external trust arguments or elevation are needed by the carrier.

The carrier verifies and copies the kit into a private sibling directory, runs
the same native Helper for `--install stable`, and reads back the resulting
active pointer, executable digest and online trust receipt. Only then does it
rename the complete root into place. It does not overwrite an existing root,
create shortcuts, launch the app or migrate a protocol-1 installation. A hard
interruption may leave its owned staging directory, not a visible half-install
at the requested destination. Open the installed `bin/Gaussian Scene Workbench Launcher.exe`
only when you want to start the app.

## 简体中文

独立的 Windows x64 首装程序只接受当前用户可写、父目录已存在的新位置。程序编译时
固定同目录 `bootstrap/bootstrap.json` 的 SHA-256；发行方仍须通过独立可信渠道验证
首装程序及其来源，同一次下载提供的哈希不能认证发布者。引导包包含固定启动器、已配置
的原生更新助手及其运行库。首装程序无需管理员权限、Git、Python 或 GitHub 账号，也
不接受额外指定的信任源。

程序先将已核验的引导文件复制到同一父目录下的专用临时目录，运行原生更新助手的签名
下载和安装流程，读回活动指针、实际 EXE 哈希及在线验收记录。全部通过才将整个目录
一次移到目标位置。不覆盖已有目录，不创建快捷方式，不启动软件，不迁移协议 1 的旧安装。
进程被强制中断时可能残留专用临时目录，目标位置不会出现半成品。需要启动时再打开安装
目录下的 `bin/Gaussian Scene Workbench Launcher.exe`。

## 日本語

独立した Windows x64 の新規インストーラーは、親ディレクトリーが存在するユーザー用の
新規パスだけを受け付けます。実行ファイルと同じ場所の `bootstrap/bootstrap.json` を
ビルド時の SHA-256 で固定します。インストーラーとその出所は独立した信頼経路で確認する
必要があり、同じダウンロードに含まれるハッシュだけでは発行元を認証できません。
ブートストラップには固定ランチャー、設定済みの更新ヘルパー、実行ライブラリーを含めます。
管理者権限、Git、Python、GitHub アカウントや外部の信頼設定引数は不要です。

検証済みのファイルを同じ親ディレクトリー内に一時配置し、ネイティブ更新ヘルパーで
署名付きのダウンロードとインストールを行います。対象バージョン、実行ファイルの
ダイジェスト及びオンライン受入記録を読戻し、すべて成功した時点で新規パスに移動します。
既存の導入先は上書きせず、ショートカット作成、アプリ起動、プロトコル 1 の移行は行いません。
強制終了で一時ディレクトリーが残っても、指定パスに未完了のインストールは表示されません。
起動するときは導入先の `bin/Gaussian Scene Workbench Launcher.exe` を開きます。

## Build and acceptance / 构建与验收 / ビルドと受入

```powershell
# Build only after the bootstrap kit has been independently authorized.
cmake -S native/first-install -B D:/build/gsw-first-install -A x64 `
  "-DGSW_FIRST_INSTALL_MANIFEST_SHA256=$authorizedBootstrapManifestSha256"
cmake --build D:/build/gsw-first-install --config Release
& 'D:/delivery/GSW Native First Install.exe' --install 'D:/Apps/GSW-New'
```

`gsw.native_first_install/1` is a machine-readable JSON result. Exit code 0 and
`BOOTSTRAP_COMMITTED` denote a newly committed root. A refusal returns exit 2,
including `BOOTSTRAP_DESTINATION_OCCUPIED` for any existing destination. A
`BOOTSTRAP_HELPER_FAILED` record can include the native Helper's machine code.

The remote `isolated-first-bootstrap` job uses its own test trust and Qt packages.
It checks the restricted medium-integrity client, then independently verifies
every bootstrap and version file, the online receipt, and an actual Qt fixture
window launched through the installed stable entry. It checks occupied-root,
changed-manifest and corrupted-download refusals. This is not Authenticode or
public release acceptance, a physical clean OS test, or evidence of protocol-1
migration. No production pin, signing key or public feed is provided here.

远端作业只验证隔离测试身份、完整文件和真实 Qt 夹具窗口，不代表签名发行、物理净机
验收或旧安装迁移；本变更不提供生产引导 pin、签名密钥或公开更新源。

遠隔ジョブは隔離したテスト用の信頼設定と実際の Qt ウィンドウを検証します。公開の
署名付き配布、実機のクリーン OS、旧版移行の受入ではありません。本番用の固定値、
署名鍵、公開更新元は含めません。
