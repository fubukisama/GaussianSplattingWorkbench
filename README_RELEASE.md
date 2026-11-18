<div lang="en">

# Get Gaussian Splatting Workbench

[English](#english) · <span lang="zh-CN">[简体中文](#简体中文)</span> · <span lang="ja">[日本語](#日本語)</span>

Windows x64 · Native preview · [Back to the project](README.md)

<div lang="en">

## English

Gaussian Splatting Workbench was previously named Gaussian Scene Workbench. The **GSW** abbreviation and existing executable filenames, settings keys and repository identifiers remain unchanged for compatibility.

### Download the preview

**[Download Windows x64 Setup (.exe)](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)**

[SHA-256](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe.sha256) · [Release notes](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview)

Run the downloaded Setup EXE. This is the **0.3.9 native preview** from November 17, 2026, not the newest development commit. GitHub's source ZIP is not an installer. To build development changes, see the [native build guide](native/README.md).

### Before you start

- **Existing models:** import supported PLY / SPZ files after installing the desktop.
- **Reconstruction / training:** configure the separate [Windows runtime](docs/WINDOWS_NATIVE_RUNTIME.md) and [COLMAP](docs/COLMAP_SETUP.md) dependencies.
- **Installation:** keep the full application, DLLs, plugins, notices and matching source together. Choose a writable location; prefer a non-system drive when available. C:-only machines are supported.
- **Security:** this Setup is unsigned. Windows may warn or block it under your policy; keep Windows protection enabled.

### Updating

Read the [changelog](CHANGELOG.md) for dated changes and their source references. A changelog entry records development; it does not by itself mean a public release or an installed update.

Managed installations use one stable native launcher and verified version slots. Development builds include a **Help → Software Updates** interface; online checks and downloads require a compatible trusted bootstrap and a configured signed update source. This guide does not announce a publicly deployed update channel. Use the verified download route above unless your distribution explicitly supplies one. See [managed updates](docs/NATIVE_UPDATES.md) for publication, migration and rollback; keep old versions and running sessions intact.

Portable artifacts are separate from Setup and may carry a legacy version in the artifact name. Check `build_manifest.json` and the accepted commit rather than inferring the product version from that label. A downloaded portable artifact must pass its complete file manifests before use; if anything is missing, use a verified Setup or rebuild a complete package—do not disable the check.

</div>

<div lang="zh-CN">

## 简体中文

<span lang="en">Gaussian Splatting Workbench</span> 原名为 <span lang="en">Gaussian Scene Workbench</span>。**GSW** 缩写及既有程序文件名、设置键和仓库标识保持不变，以保留兼容性。

### 下载预览版

**[下载 Windows x64 安装包（.exe）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)**

[SHA-256](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe.sha256) · [发布说明](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview)

直接运行下载的 Setup EXE。这是 2026 年 11 月 17 日构建的 **0.3.9 原生预览版**，不是最新开发提交。GitHub 的源码 ZIP 不是安装包。如需构建开发版，请参阅[原生构建指南](native/README.md)。

### 使用前确认

- **已有模型：**安装桌面端后可导入受支持的 PLY / SPZ。
- **重建／训练：**另行配置 [Windows 运行环境](docs/WINDOWS_NATIVE_RUNTIME.md)和 [COLMAP](docs/COLMAP_SETUP.md)。
- **安装位置：**保留完整程序、DLL、插件、声明和对应源码。选择可写目录，有非系统盘时优先使用；只有 C: 的机器也可使用。
- **安全策略：**此安装包未签名，Windows 可能警告或按策略阻止。请保留系统防护。

### 更新方式

按日期整理的变更与源码参见[更新日志](CHANGELOG.zh-CN.md)。日志条目记录开发变更，本身不表示已公开发布或已安装更新。

托管安装使用固定原生启动器和经过校验的版本目录。开发版已接入**帮助 → 软件更新**界面；联网检查和下载需要兼容的可信引导组件及已配置的签名更新源。本指南不表示已开放公共更新通道；除非所用发行版明确提供该服务，否则仍按上方已验下载路线获取程序。[更新说明](docs/NATIVE_UPDATES.md)记录了发布、迁移和回退流程；保留旧版，不中断运行中的会话。

便携附件与 Setup 是两种产物，附件名称可能仍含旧版本号。以 `build_manifest.json` 和已验提交确认身份，不凭附件标签判断版本。便携包必须通过完整文件清单校验；缺文件时改用已验 Setup 或重新构建完整包，不能关闭校验后继续使用。

</div>

<div lang="ja">

## 日本語

<span lang="en">Gaussian Splatting Workbench</span> の旧名称は <span lang="en">Gaussian Scene Workbench</span> です。互換性のため、**GSW** の略称と既存の実行ファイル名・設定キー・リポジトリー識別子は維持します。

### プレビュー版の入手

**[Windows x64 版をダウンロード（.exe）](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe)**

[SHA-256](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview/Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe.sha256) · [リリースノート](https://github.com/fubukisama/GaussianSplattingWorkbench/releases/tag/v0.3.9-native-preview)

ダウンロードしたセットアップ EXE を実行します。2026年11月17日にビルドした **0.3.9 ネイティブプレビュー**で、最新の開発コミットではありません。GitHub のソース ZIP はインストーラーではありません。開発版は[ネイティブビルド手順](native/README.md)を参照してください。

### 利用前の確認

- **既存モデル：**デスクトップ導入後、対応する PLY / SPZ を取り込めます。
- **再構築・学習：**別途 [Windows 実行環境](docs/WINDOWS_NATIVE_RUNTIME.md)と [COLMAP](docs/COLMAP_SETUP.md) を構成します。
- **導入先：**DLL・プラグイン・通知・対応ソースを含め全体を保持します。書込み可能な場所を選び、利用できれば非システムドライブを優先。C: のみの端末も対象です。
- **安全性：**このセットアップは未署名です。Windows の警告やポリシーによるブロックが発生する場合があります。Windows の保護を無効にしないでください。

### 更新

日付別の変更とソース参照は[変更履歴](CHANGELOG.ja.md)を参照してください。履歴への記載だけで、一般公開やインストール済みの更新を意味するものではありません。

管理方式では固定ネイティブランチャーと検証済みバージョンを使います。開発版には**ヘルプ → ソフトウェア更新**の画面がありますが、オンライン確認・ダウンロードには互換性のある信頼済みブートストラップと、設定済みの署名付き更新元が必要です。本ガイドは一般公開の更新チャンネルの提供を意味しません。利用する配布版が明示的に提供しない限り、上記の検証済み入手経路を使用してください。[更新手順](docs/NATIVE_UPDATES.md)に従い、旧版と起動中のセッションを保持してください。

ポータブル添付はSetupと別物で、添付名に古いバージョンが残る場合があります。`build_manifest.json` と検証済みコミットで確認してください。全ファイルのマニフェストに不足があれば、検証済みSetupまたは完全な再ビルドを使い、検証を無効にしないでください。

</div>

---

[License scope](native/LICENSE.md) · [Third-party notices](THIRD_PARTY_LICENSES.md) · [Source distribution](docs/NATIVE_SOURCE_DISTRIBUTION.md)


</div>
