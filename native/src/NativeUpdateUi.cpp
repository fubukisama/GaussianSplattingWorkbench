// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeUpdateUi.h"
#include "AppLanguage.h"
#include "NativeInstallation.h"
#include "NativeGithubPreviewFeed.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace gsw {
namespace {
QString text(const char *source) { return QCoreApplication::translate("Workbench", source); }
constexpr int startupCheckDelayMs = 10'000;
constexpr int checkIntervalMs = 4 * 60 * 60 * 1000;
constexpr int exitInstallWaitMs = 30'000;
class UpdateController final : public QObject {
public:
  explicit UpdateController(QWidget *window) : QObject(window), mWindow(window) {
    setObjectName(QStringLiteral("nativeUpdateController"));
    QString applicationPath = QCoreApplication::applicationFilePath();
#ifdef GSW_UPDATER_UI_FIXTURE_BUILD
    // Compiled only into the distinct remote UI fixture target. The installed
    // application has no environment-based root/helper/verification override.
    const auto fixtureApplication = qEnvironmentVariable("GSW_UPDATE_UI_FIXTURE_APPLICATION");
    if (!fixtureApplication.isEmpty()) applicationPath = fixtureApplication;
#endif
    mRoot = managedInstallationForApplication(applicationPath);
    const QDir bin(QFileInfo(applicationPath).absolutePath());
    mPreviewHelper = bin.filePath(QString::fromLatin1(preview::helperName));
    QDir version(bin);
    if (version.cdUp()) mRunningBuild = version.dirName();
    if (QFileInfo(mPreviewHelper).isFile() && bin.dirName() == QLatin1String("bin")) {
      if (!mRoot.isEmpty()) {
        QFile boot(QDir(mRoot).filePath(QStringLiteral("bootstrap.json")));
        if (boot.size() <= 16384 && boot.open(QIODevice::ReadOnly))
          mPreviewSupported = QJsonDocument::fromJson(boot.read(16384)).object().value(QStringLiteral("protocol")).toInt() == 1;
      } else {
        const auto ini = version.filePath(QStringLiteral("gsw-installation.ini"));
        if (QFileInfo(ini).isFile() &&
            QSettings(ini,QSettings::IniFormat).value(QStringLiteral("Installation/AppId")).toString() == QLatin1String(preview::product)) {
          mRoot = version.absolutePath(); mFixedSetup = true; mPreviewSupported = true;
          mRunningBuild = QCoreApplication::applicationVersion();
        }
      }
      for (const auto &name : {QStringLiteral(".update-trust-anchor"), QStringLiteral("updates/trust")}) {
        const QFileInfo item(QDir(mRoot).filePath(name));
        if (item.exists() || item.isSymLink()) mPreviewSupported = false;
      }
    }
    mPrepared = !mFixedSetup && !mRoot.isEmpty() && QFileInfo(preparedMarker()).isFile();
    AppLanguage::onChanged(this, [this] { refresh(); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
        [this] { installOnAcceptedExit(); });
    auto *timer = new QTimer(this);
    timer->setInterval(checkIntervalMs);
    connect(timer, &QTimer::timeout, this, [this] { automaticCheck(); });
    QTimer::singleShot(startupCheckDelayMs, this, [this, timer] { automaticCheck(); timer->start(); });
  }
  ~UpdateController() override { releaseHelper(true); }
  void setCloseAccepted(bool accepted) { mCloseAccepted = accepted; }
  void showDialog() {
    if (!mDialog) createDialog();
    refresh(); mDialog->show(); // Only an explicit Help action reaches here.
  }
private:
  void releaseHelper(bool cancel) {
    if (!mProcess) return;
    auto *process = mProcess.data();
    mProcess.clear();
    if (cancel) process->write("cancel\n");
    process->closeWriteChannel();
    disconnect(process, nullptr, this, nullptr);
    // The destructing window must not kill a helper during atomic installation.
    process->setParent(nullptr);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
  }
  QString channel() const {
    const auto value = QSettings().value(QStringLiteral("updates/channel"), mPreviewSupported ? QStringLiteral("preview") : QStringLiteral("stable")).toString();
    return value == QLatin1String("preview") ? value : QStringLiteral("stable");
  }
  bool githubPreview() const { return mPreviewSupported && channel() == QLatin1String("preview"); }
  QString preparedMarker() const {
    return QDir(mRoot).filePath(githubPreview() ? QStringLiteral(".gsw-github-preview/prepared.json") : QStringLiteral("updates/prepared.json"));
  }
  bool automaticPreparationEnabled() const {
    return !mFixedSetup && QSettings().value(QStringLiteral("updates/automaticCheck"), true).toBool() &&
        QSettings().value(QStringLiteral("updates/prepareForNextLaunch"), false).toBool();
  }
  void installOnAcceptedExit() {
    if (mFixedSetup || !mCloseAccepted || mProcess || mRoot.isEmpty() || !mPrepared ||
        !(automaticPreparationEnabled() || (githubPreview() && mManualQueued))) return;
    if (QDateTime::currentDateTimeUtc() < QSettings().value(QStringLiteral("updates/deferredUntil")).toDateTime()) return;
    // The on-disk marker is an intent, not trust evidence. Pass the exact
    // build to the existing helper: it must re-admit the signed cached offer,
    // check the full archive and acquire the normal installation lock.
    const auto markerPath = preparedMarker();
    const QFileInfo markerInfo(markerPath);
    if (!markerInfo.isFile() || markerInfo.isSymLink()) return;
    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly)) return;
    const auto bytes = marker.read(16 * 1024 + 1);
    marker.close();
    if (bytes.size() > 16 * 1024) return;
    const auto prepared = QJsonDocument::fromJson(bytes).object();
    const auto selectedChannel = channel();
    const auto build = prepared.value(QStringLiteral("buildId")).toString();
    const auto markerSchema = githubPreview() ? QStringLiteral("gsw.github_preview_prepared/1") : QStringLiteral("gsw.prepared_update/1");
    if (prepared.value(QStringLiteral("schema")).toString() != markerSchema ||
        prepared.value(QStringLiteral("channel")).toString() != selectedChannel ||
        build.isEmpty() || build == mRunningBuild ||
        QSettings().value(QStringLiteral("updates/skipped/") + selectedChannel).toStringList().contains(build)) return;
    begin(QStringLiteral("--apply-prepared"), false, build);
    // Only after the last user window accepted close, allow the local (no
    // download) installation to finish before the ordinary next launch.
    // Do not run a nested GUI event loop or kill an installation on timeout.
    if (mProcess && !mProcess->waitForFinished(exitInstallWaitMs)) releaseHelper(false);
  }
  void automaticCheck() {
    if (mProcess || !QSettings().value(QStringLiteral("updates/automaticCheck"), true).toBool()) return;
    const auto now = QDateTime::currentDateTimeUtc();
    if (now < QSettings().value(QStringLiteral("updates/deferredUntil")).toDateTime()) return;
    begin(QStringLiteral("--check"), true);
  }
  void begin(const QString &operation, bool background = false, const QString &expectedBuild = {}) {
    if (mProcess) return;
    mLastOperation = operation; mPhase = QStringLiteral("checking"); mCode.clear();
    mReceived = 0; mTotal = 0;
    if (operation == QLatin1String("--prepare")) mPrepared = false;
    mRequestChannel = channel(); mBuffer.clear(); mProtocolError = false;
    if (mRoot.isEmpty()) { mCode = QStringLiteral("BOOTSTRAP_REQUIRED"); mPhase = QStringLiteral("failed"); refresh(); return; }
    const bool fromGithub = githubPreview();
    if (mFixedSetup && !fromGithub) { mCode = QStringLiteral("NOT_CONFIGURED"); mPhase = QStringLiteral("failed"); refresh(); return; }
    if (mFixedSetup && operation != QLatin1String("--check")) {
      mCode = QStringLiteral("preview_setup_manual_only"); mPhase = QStringLiteral("failed"); refresh(); return;
    }
    const auto helper = fromGithub ? mPreviewHelper : QDir(mRoot).filePath(QString::fromLatin1("bin/Gaussian Scene Workbench Update Helper.exe"));
    if (!QFileInfo(helper).isFile()) { mCode = QStringLiteral("BOOTSTRAP_REQUIRED"); mPhase = QStringLiteral("failed"); refresh(); return; }
    if (operation == QLatin1String("--check")) QSettings().setValue(QStringLiteral("updates/lastAttempt"), QDateTime::currentDateTimeUtc());
    auto *process = new QProcess(this);
    mProcess = process;
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
      mBuffer += process->readAllStandardOutput();
      if (mBuffer.size() > 4 * 1024 * 1024) { protocolFailure(); return; }
      for (auto end = mBuffer.indexOf('\n'); end >= 0; end = mBuffer.indexOf('\n')) {
        const auto line = mBuffer.left(end); mBuffer.remove(0, end + 1);
        QJsonParseError error;
        const auto record = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !record.isObject() ||
            record.object().value(QStringLiteral("schema")).toString() != QLatin1String("gsw.update_event/1")) { protocolFailure(); return; }
        if (mProtocolError) continue;
        const auto event = record.object();
        mPhase = event.value(QStringLiteral("phase")).toString();
        mCode = event.value(QStringLiteral("code")).toString();
        if (mPhase == QLatin1String("available")) mOffer = event.value(QStringLiteral("offer")).toObject();
        if (mPhase == QLatin1String("prepared")) mPrepared = true;
        if (mPhase == QLatin1String("current")) { mOffer = {}; mPrepared = false; }
        if (mPhase == QLatin1String("activated")) mPrepared = false;
        if (event.contains(QStringLiteral("receivedBytes"))) {
          mReceived = event.value(QStringLiteral("receivedBytes")).toDouble();
          mTotal = event.value(QStringLiteral("totalBytes")).toDouble();
        }
        refresh();
      }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
      if (error != QProcess::FailedToStart) return;
      mProcess = nullptr; process->deleteLater(); mPhase = QStringLiteral("failed"); mCode = QStringLiteral("HELPER_START_FAILED"); refresh();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process, operation, background, fromGithub](int code, QProcess::ExitStatus exit) {
      if (mProcess != process) return;
      mProcess = nullptr; process->deleteLater();
      const auto expectedPhase = operation == QLatin1String("--check") ? QStringLiteral("available")
          : operation == QLatin1String("--prepare") ? QStringLiteral("prepared") : QStringLiteral("activated");
      const bool previewFinal = fromGithub &&
          operation == QLatin1String("--check") && mPhase == QLatin1String("current");
      if (exit == QProcess::NormalExit && code == 0 && !previewFinal &&
          (!mBuffer.trimmed().isEmpty() || mPhase != expectedPhase ||
           (operation == QLatin1String("--check") && mOffer.value(QStringLiteral("buildId")).toString().isEmpty()))) {
        mProtocolError = true; mCode = QStringLiteral("UPDATE_PROTOCOL_INVALID");
      }
      if (exit != QProcess::NormalExit || code != 0 || mProtocolError) {
        if (operation == QLatin1String("--prepare")) mPrepared = false;
        mPhase = QStringLiteral("failed"); if (mCode.isEmpty()) mCode = QStringLiteral("HELPER_FAILED");
      }
      refresh();
      const auto skipped = QSettings().value(QStringLiteral("updates/skipped/") + mRequestChannel).toStringList();
      const bool automatic = background && automaticPreparationEnabled() && channel() == mRequestChannel;
      if (exit == QProcess::NormalExit && code == 0 && !mProtocolError && mPhase != QLatin1String("failed") && automatic) {
        if (operation == QLatin1String("--check") && mPhase == QLatin1String("available") && mOffer.value(QStringLiteral("buildId")).toString() != mRunningBuild &&
            !skipped.contains(mOffer.value(QStringLiteral("buildId")).toString()))
          QTimer::singleShot(0, this, [this] { begin(QStringLiteral("--prepare"), true); });
      }
    });
    QStringList arguments{operation, mRequestChannel};
    if (!expectedBuild.isEmpty()) arguments.append(expectedBuild);
    if (fromGithub && operation == QLatin1String("--apply-prepared"))
      arguments.append(QString::number(QCoreApplication::applicationPid()));
    process->setProgram(helper); process->setArguments(arguments);
    process->setWorkingDirectory(mRoot); process->start(); refresh();
  }
  void protocolFailure() {
    mProtocolError = true; mCode = QStringLiteral("UPDATE_PROTOCOL_INVALID"); mPhase = QStringLiteral("failed");
    mBuffer.clear(); if (mProcess) mProcess->write("cancel\n"); refresh();
  }
  void createDialog() {
    auto *dialog = new QDialog(mWindow); mDialog = dialog;
    dialog->setObjectName(QStringLiteral("nativeUpdateDialog")); dialog->resize(700, 550);
    AppLanguage::bind(dialog, "windowTitle", AppLanguage::source("软件更新"));
    auto *layout = new QVBoxLayout(dialog);
    mRunning = new QLabel(dialog); mRunning->setObjectName(QStringLiteral("nativeUpdateRunningBuild"));
    mRunning->setTextFormat(Qt::PlainText); mRunning->setWordWrap(true); layout->addWidget(mRunning);
    // Version strings and helper codes are data, never QLabel rich text.
    mStatus = new QLabel(dialog); mStatus->setObjectName(QStringLiteral("nativeUpdateStatus"));
    mStatus->setTextFormat(Qt::PlainText); mStatus->setWordWrap(true); layout->addWidget(mStatus);
    mOrigin = AppLanguage::text(new QLabel(dialog), AppLanguage::source("来源：项目官方 GitHub Releases。预览安装包可能没有 Windows 发布者签名。"));
    mOrigin->setWordWrap(true); mOrigin->setTextFormat(Qt::PlainText); layout->addWidget(mOrigin);
    mManual = AppLanguage::text(new QLabel(dialog), AppLanguage::source("向导安装版只检查预览更新。请从官方发布页下载并手动运行安装程序；如需退出时自动更新，请使用托管预览启动包。"));
    mManual->setWordWrap(true); mManual->setTextFormat(Qt::PlainText);
    mManual->setObjectName(QStringLiteral("nativeUpdateManualSetup")); layout->addWidget(mManual);
    mProgress = new QProgressBar(dialog); mProgress->setRange(0, 1000); layout->addWidget(mProgress);
    mNotes = new QPlainTextEdit(dialog); mNotes->setObjectName(QStringLiteral("nativeUpdateNotes")); mNotes->setReadOnly(true); layout->addWidget(mNotes);
    auto *form = new QFormLayout;
    mChannel = new QComboBox(dialog); mChannel->addItems({QString(), QString()});
    mChannel->setObjectName(QStringLiteral("nativeUpdateChannel"));
    mChannel->setItemData(0, QStringLiteral("stable")); mChannel->setItemData(1, QStringLiteral("preview"));
    AppLanguage::bindComboItem(mChannel, 0, AppLanguage::source("稳定版"));
    AppLanguage::bindComboItem(mChannel, 1, AppLanguage::source("预览版"));
    mChannel->setCurrentIndex(channel() == QLatin1String("preview") ? 1 : 0);
    form->addRow(AppLanguage::text(new QLabel(dialog), AppLanguage::source("更新频道")), mChannel);
    layout->addLayout(form);
    connect(mChannel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
      QSettings().setValue(QStringLiteral("updates/channel"), mChannel->currentData()); mOffer = {}; mManualQueued = false;
      mPrepared = !mFixedSetup && !mRoot.isEmpty() && QFileInfo(preparedMarker()).isFile(); refresh();
    });
    auto *check = AppLanguage::text(new QCheckBox(dialog), AppLanguage::source("后台自动检查（不弹出窗口）"));
    check->setChecked(QSettings().value(QStringLiteral("updates/automaticCheck"), true).toBool());
    connect(check, &QCheckBox::toggled, this, [this](bool value) { QSettings().setValue(QStringLiteral("updates/automaticCheck"), value); refresh(); }); layout->addWidget(check);
    auto *prepare = AppLanguage::text(new QCheckBox(dialog), AppLanguage::source("自动下载并在正常退出时安装"));
    prepare->setChecked(QSettings().value(QStringLiteral("updates/prepareForNextLaunch"), false).toBool());
    prepare->setVisible(!mFixedSetup);
    connect(prepare, &QCheckBox::toggled, this, [this](bool value) { QSettings().setValue(QStringLiteral("updates/prepareForNextLaunch"), value); refresh(); }); layout->addWidget(prepare);
    auto *buttons = new QGridLayout; layout->addLayout(buttons);
    int buttonIndex = 0;
    const auto button = [dialog, buttons, &buttonIndex](const char *source) { auto *b = new QPushButton(dialog); AppLanguage::bind(b, "text", source); buttons->addWidget(b, buttonIndex / 3, buttonIndex % 3); ++buttonIndex; return b; };
    mCheck = button(AppLanguage::source("检查更新")); connect(mCheck, &QPushButton::clicked, this, [this] { begin(QStringLiteral("--check")); });
    mDownload = button(AppLanguage::source("下载更新")); connect(mDownload, &QPushButton::clicked, this, [this] { begin(QStringLiteral("--prepare")); });
    mApply = button(AppLanguage::source("下次启动使用此版本")); connect(mApply, &QPushButton::clicked, this, [this] {
      if (githubPreview()) { mManualQueued = true; mPhase = QStringLiteral("preview_queued"); refresh(); }
      else begin(QStringLiteral("--apply-prepared"));
    });
    mDownload->setVisible(!mFixedSetup);
    mApply->setVisible(!mFixedSetup);
    mReleasePage = button(AppLanguage::source("打开官方发布页"));
    mReleasePage->setObjectName(QStringLiteral("nativeUpdateManualReleasePage"));
    connect(mReleasePage, &QPushButton::clicked, this, [] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/fubukisama/GaussianSplattingWorkbench/releases")));
    });
    mCancel = button(AppLanguage::source("取消下载")); connect(mCancel, &QPushButton::clicked, this, [this] { if (mProcess) { mProcess->write("cancel\n"); mPhase = QStringLiteral("cancelling"); refresh(); } });
    mRetry = button(AppLanguage::source("重试")); connect(mRetry, &QPushButton::clicked, this, [this] { begin(mLastOperation.isEmpty() ? QStringLiteral("--check") : mLastOperation); });
    mSkip = button(AppLanguage::source("跳过此构建")); connect(mSkip, &QPushButton::clicked, this, [this] {
      auto skipped = QSettings().value(QStringLiteral("updates/skipped/") + channel()).toStringList();
      skipped.removeAll(mOffer.value(QStringLiteral("buildId")).toString()); skipped.prepend(mOffer.value(QStringLiteral("buildId")).toString());
      while (skipped.size() > 100) skipped.removeLast(); QSettings().setValue(QStringLiteral("updates/skipped/") + channel(), skipped);
      mPhase = QStringLiteral("skipped"); refresh();
    });
    auto *later = button(AppLanguage::source("稍后提醒")); connect(later, &QPushButton::clicked, this, [this] {
      QSettings().setValue(QStringLiteral("updates/deferredUntil"), QDateTime::currentDateTimeUtc().addDays(1)); mDialog->hide();
    });
    auto *close = button(AppLanguage::source("关闭")); connect(close, &QPushButton::clicked, dialog, &QDialog::hide);
    mCheck->setObjectName(QStringLiteral("nativeUpdateCheck")); mDownload->setObjectName(QStringLiteral("nativeUpdateDownload"));
    mApply->setObjectName(QStringLiteral("nativeUpdateApply")); mCancel->setObjectName(QStringLiteral("nativeUpdateCancel"));
    mRetry->setObjectName(QStringLiteral("nativeUpdateRetry")); mSkip->setObjectName(QStringLiteral("nativeUpdateSkip"));
    later->setObjectName(QStringLiteral("nativeUpdateLater")); check->setObjectName(QStringLiteral("nativeUpdateAutomaticCheck"));
    prepare->setObjectName(QStringLiteral("nativeUpdateAutomaticPrepare"));
  }
  void refresh() {
    if (!mDialog) return;
    mOrigin->setVisible(githubPreview());
    mManual->setVisible(mFixedSetup && githubPreview());
    mReleasePage->setVisible(mFixedSetup && githubPreview());
    mRunning->setText(text(AppLanguage::source("正在运行的构建：%1")).arg(mRoot.isEmpty() ? text(AppLanguage::source("开发构建（未托管安装）")) : mRunningBuild));
    QString status;
    if (mPhase == QLatin1String("preview_queued")) status = text(AppLanguage::source("已安排在正常退出时安装 GitHub 预览版。"));
    else if (mPhase == QLatin1String("current")) status = text(AppLanguage::source("官方 GitHub 发布中没有适用于此安装的更新预览版。"));
    else if (mCode == QLatin1String("NOT_CONFIGURED")) status = text(AppLanguage::source("尚未配置生产更新信任；不会接受未签名更新。"));
    else if (mCode == QLatin1String("download_insufficient_space") || mCode == QLatin1String("activation_insufficient_space")) status = text(AppLanguage::source("磁盘可用空间不足，当前版本未更改。请释放磁盘空间后重试。"));
    else if (mCode == QLatin1String("BOOTSTRAP_REQUIRED") || mCode == QLatin1String("activation_bootstrap_migration_required")) status = text(AppLanguage::source("需要使用可信安装器升级启动组件；当前程序和工程未改变。"));
    else if (mPhase == QLatin1String("failed")) status = text(AppLanguage::source("更新未完成或尚未确认（%1）。当前窗口与工程不受影响，请重新检查更新状态。")).arg(mCode);
    else if (mPhase == QLatin1String("checking")) status = githubPreview()
        ? text(AppLanguage::source("正在检查项目官方 GitHub 预览发布…")) : text(AppLanguage::source("正在检查已签名的更新信息…"));
    else if (mPhase == QLatin1String("downloading")) status = text(AppLanguage::source("正在下载：%1 / %2 MiB")).arg(mReceived / (1024 * 1024), 0, 'f', 1).arg(mTotal / (1024 * 1024), 0, 'f', 1);
    else if (mPhase == QLatin1String("verifying")) status = text(AppLanguage::source("正在验证签名和文件完整性…"));
    else if (mPhase == QLatin1String("staging")) status = text(AppLanguage::source("正在准备独立的新版本目录…"));
    else if (mPhase == QLatin1String("prepared")) status = automaticPreparationEnabled()
        ? text(AppLanguage::source("更新已下载并验证，将在正常退出时安装。"))
        : text(AppLanguage::source("更新已下载并验证，可以准备下次启动。"));
    else if (mPhase == QLatin1String("activated")) status = text(AppLanguage::source("下次启动时生效；当前窗口和任务将继续运行。"));
    else if (mPhase == QLatin1String("cancelling")) status = githubPreview()
        ? text(AppLanguage::source("正在取消；不完整的下载不会用于安装。")) : text(AppLanguage::source("正在取消，保留可安全续传的数据…"));
    else if (mPhase == QLatin1String("skipped")) status = text(AppLanguage::source("自动更新已跳过此构建；仍可手动下载。"));
    else if (mOffer.value(QStringLiteral("buildId")).toString() == mRunningBuild && !mOffer.isEmpty()) status = text(AppLanguage::source("当前正在运行此构建。"));
    else if (!mOffer.isEmpty()) status = text(AppLanguage::source("可用版本：%1 · 构建 %2")).arg(mOffer.value(QStringLiteral("displayVersion")).toString(), mOffer.value(QStringLiteral("buildId")).toString());
    else status = text(AppLanguage::source("更新检查不影响当前工程和训练；不会自动重启软件。"));
    mStatus->setText(status);
    const auto notes = mOffer.value(QStringLiteral("releaseNotes")).toObject();
    const auto releaseText = notes.value(AppLanguage::current()).toString(notes.value(QStringLiteral("en_US")).toString());
    if (mNotes->toPlainText() != releaseText) mNotes->setPlainText(releaseText);
    const bool busy = mProcess != nullptr;
    mChannel->setEnabled(!busy); mCheck->setEnabled(!busy); mDownload->setEnabled(!busy && !mOffer.isEmpty() && mOffer.value(QStringLiteral("buildId")).toString() != mRunningBuild);
    mApply->setEnabled(!busy && mPrepared); mRetry->setEnabled(!busy && mPhase == QLatin1String("failed"));
    mCancel->setEnabled(busy && mPhase != QLatin1String("staging")); mSkip->setEnabled(!busy && !mOffer.isEmpty());
    if (mPhase == QLatin1String("downloading") && mTotal > 0) { mProgress->setRange(0, 1000); mProgress->setValue(int(qBound(0.0, mReceived / mTotal * 1000.0, 1000.0))); }
    else if (busy) mProgress->setRange(0, 0);
    else { mProgress->setRange(0, 1000); mProgress->setValue(mPhase == QLatin1String("activated") || mPrepared ? 1000 : 0); }
  }
  QPointer<QWidget> mWindow;
  QPointer<QDialog> mDialog;
  QPointer<QProcess> mProcess;
  QLabel *mRunning = nullptr, *mStatus = nullptr;
  QLabel *mOrigin = nullptr, *mManual = nullptr;
  QPlainTextEdit *mNotes = nullptr;
  QComboBox *mChannel = nullptr;
  QProgressBar *mProgress = nullptr;
  QPushButton *mCheck = nullptr, *mDownload = nullptr, *mApply = nullptr, *mCancel = nullptr, *mRetry = nullptr, *mSkip = nullptr;
  QPushButton *mReleasePage = nullptr;
  QString mRoot, mRunningBuild, mRequestChannel, mLastOperation, mPhase, mCode;
  QByteArray mBuffer;
  QJsonObject mOffer;
  double mReceived = 0, mTotal = 0;
  QString mPreviewHelper;
  bool mPrepared = false, mProtocolError = false, mCloseAccepted = false;
  bool mPreviewSupported = false, mFixedSetup = false, mManualQueued = false;
};
}
void installNativeUpdateUi(QMenu *helpMenu, QWidget *window) {
  if (!helpMenu || !window || window->findChild<QObject *>(QStringLiteral("nativeUpdateController"))) return;
  auto *controller = new UpdateController(window);
  auto *action = AppLanguage::text(helpMenu->addAction(QString()), AppLanguage::source("软件更新…"));
  action->setObjectName(QStringLiteral("nativeUpdateAction"));
  QObject::connect(action, &QAction::triggered, controller, [controller] { controller->showDialog(); });
}
void setNativeUpdateCloseAccepted(QWidget *window, bool accepted) {
  if (!window) return;
  const auto name = QStringLiteral("nativeUpdateController");
  if (auto *controller = dynamic_cast<UpdateController *>(window->findChild<QObject *>(name, Qt::FindDirectChildrenOnly)))
    controller->setCloseAccepted(accepted);
}
} // namespace gsw
