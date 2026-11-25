<div lang="en">

# Get Gaussian Splatting Workbench

[English](#english) · <span lang="zh-CN">[简体中文](#简体中文)</span> · <span lang="ja">[日本語](#日本語)</span>

Windows x64 · Native preview · [Back to the project](README.md)

<div lang="en">

## English

Gaussian Splatting Workbench was previously named Gaussian Scene Workbench. The **GSW** abbreviation and existing executable filenames, settings keys and repository identifiers remain unchanged for compatibility.

### Download the preview

**[Download the managed starter (Windows x64, ZIP)](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/gsw-windows-x64-preview-start.zip)**

[SHA-256](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/SHA256SUMS) · [Release notes (f4b5886)](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview.20261120.1) · [Classic Setup (.exe, manual updates)](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)

Compare the starter ZIP's SHA-256 with `SHA256SUMS`, then extract the ZIP into a new empty folder and open `bin/Gaussian Scene Workbench Launcher.exe`. Do not extract it over an existing installation. GitHub's source ZIP is not a ready-to-run build; see the [native build guide](native/README.md) to build from source.

### Before you start

- **Existing models:** import supported PLY / SPZ files after installing the desktop.
- **Reconstruction / training:** configure the separate [Windows runtime](docs/WINDOWS_NATIVE_RUNTIME.md) and [COLMAP](docs/COLMAP_SETUP.md) dependencies.
- **Installation:** keep the full application, DLLs, plugins, notices and matching source together. Choose a writable location; prefer a non-system drive when available. C:-only machines are supported.
- **Security:** this preview does not have a Windows code signature. The SHA-256 check verifies file integrity, not publisher identity. Windows may warn or block it under your policy; keep Windows protection enabled.

### Updating

1. Open **Help → Software updates…** and select **Preview**.
2. Keep **Check automatically in the background (no popups)** enabled, and turn on **Download automatically and install on normal exit**.
3. GSW downloads and checks the update in the background. It checks again and applies the update only on normal exit; cancelling the exit leaves the current version unchanged. Open the same launcher next time to use the updated version. The previous version is kept for rollback.

`gsw-windows-x64-managed.zip` is the updater's payload, not the first-install package. Classic Setup requires manual updates and is not automatically converted to a managed installation. See the [changelog](CHANGELOG.md) and [update and rollback guide](docs/NATIVE_UPDATES.md).

</div>

<div lang="zh-CN">

## 简体中文

<span lang="en">Gaussian Splatting Workbench</span> 原名为 <span lang="en">Gaussian Scene Workbench</span>。**GSW** 缩写及既有程序文件名、设置键和仓库标识保持不变，以保留兼容性。

### 下载预览版

**[下载 Windows x64 托管首装包（ZIP）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/gsw-windows-x64-preview-start.zip)**

[SHA-256 校验](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/SHA256SUMS) · [发布说明（f4b5886）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview.20261120.1) · [传统安装包（.exe，手动更新）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)

用 `SHA256SUMS` 核对首装 ZIP 的 SHA-256。校验通过后，将 ZIP 解压到新的空目录，再打开 `bin/Gaussian Scene Workbench Launcher.exe`。不要覆盖已有安装。GitHub 的源码 ZIP 不能直接运行；从源码构建请参阅[原生构建指南](native/README.md)。

### 使用前确认

- **已有模型**：安装桌面端后可导入受支持的 PLY / SPZ。
- **重建／训练**：另行配置 [Windows 运行环境](docs/WINDOWS_NATIVE_RUNTIME.md)和 [COLMAP](docs/COLMAP_SETUP.md)。
- **安装位置**：保留完整程序、DLL、插件、声明和对应源码。选择可写目录，有非系统盘时优先使用；只有 C: 的机器也可使用。
- **安全策略**：此预览版没有 Windows 代码签名。SHA-256 只校验文件完整性，不认证发布者身份。Windows 可能警告或按策略阻止，请保留系统防护。

### 更新方式

1. 打开「帮助 → 软件更新…」，选择「预览版」。
2. 保留「后台自动检查（不弹出窗口）」，开启「自动下载并在正常退出时安装」。
3. GSW 在后台下载并校验更新，只在正常退出时重新校验并应用。取消退出不会切换版本；下次用原启动器打开即可使用新版，旧版保留以供回退。

`gsw-windows-x64-managed.zip` 是应用内更新使用的 ZIP，不是首装包。传统安装包仍须手动更新，不会自动迁移为托管安装。另见[更新日志](CHANGELOG.zh-CN.md)和[更新与回退说明](docs/NATIVE_UPDATES.md)。

</div>

<div lang="ja">

## 日本語

<span lang="en">Gaussian Splatting Workbench</span> の旧名称は <span lang="en">Gaussian Scene Workbench</span> です。互換性のため、**GSW** の略称と既存の実行ファイル名・設定キー・リポジトリー識別子は維持します。

### プレビュー版の入手

**[Windows x64 管理型スタートパックをダウンロード（ZIP）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/gsw-windows-x64-preview-start.zip)**

[SHA-256 チェックサム](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/SHA256SUMS) · [リリースノート（f4b5886）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview.20261120.1) · [従来のセットアップ（.exe、手動更新）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261120.1/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)

スタートパックの SHA-256 を `SHA256SUMS` と照合し、新しい空フォルダーに ZIP を展開して `bin/Gaussian Scene Workbench Launcher.exe` を起動します。既存のインストール先には上書きしないでください。GitHub のソース ZIP から直接アプリは起動できません。ソースからビルドする場合は[ネイティブビルド手順](native/README.md)を参照してください。

### 利用前の確認

- **既存モデル**：デスクトップ導入後、対応する PLY / SPZ を取り込めます。
- **再構築・学習**：別途 [Windows 実行環境](docs/WINDOWS_NATIVE_RUNTIME.md)と [COLMAP](docs/COLMAP_SETUP.md) を構成します。
- **導入先**：DLL・プラグイン・通知・対応ソースを含め全体を保持します。書込み可能な場所を選び、利用できれば非システムドライブを優先。C: のみの端末も対象です。
- **安全性**：このプレビュー版には Windows のコード署名がありません。SHA-256 はファイルの完全性を確認するもので、発行元の身元を証明するものではありません。Windows が警告またはブロックする場合があります。Windows の保護は有効にしたままにしてください。

### 更新

1. 「ヘルプ → ソフトウェアの更新…」を開き、「プレビュー版」を選びます。
2. 「バックグラウンドで自動確認（ポップアップなし）」を有効にしたまま、「自動ダウンロードし、通常終了時にインストール」を有効にします。
3. GSW はバックグラウンドで更新をダウンロードして検証し、通常終了時にもう一度検証してから適用します。終了をキャンセルすると現在の版が維持されます。次回も同じランチャーから起動すると最新版が使えます。ロールバック用に旧版を保持します。

`gsw-windows-x64-managed.zip` はアプリ内更新用で、初回導入用ではありません。従来のセットアップ版は手動更新が必要で、管理型への自動移行は行いません。[変更履歴](CHANGELOG.ja.md)と[更新・ロールバック手順](docs/NATIVE_UPDATES.md)も参照してください。

</div>

---

[License scope](native/LICENSE.md) · [Third-party notices](THIRD_PARTY_LICENSES.md) · [Source distribution](docs/NATIVE_SOURCE_DISTRIBUTION.md)


</div>
