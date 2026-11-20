// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeGithubPreviewFeed.h"
#include "NativeActivationCore.h"
#include "NativeUnanchoredBootstrap.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <atomic>
#include <cstdio>
#include <shlobj.h>
#include <shellapi.h>

namespace gsw {
using namespace activation;
namespace {
struct Layout { QString root, slot; bool managed = false; };
void emitEvent(const QString &phase, const QString &code = {}, QJsonObject info = {}) {
  info.insert(QStringLiteral("schema"), QStringLiteral("gsw.update_event/1"));
  info.insert(QStringLiteral("phase"), phase); info.insert(QStringLiteral("code"), code);
  auto data = QJsonDocument(info).toJson(QJsonDocument::Compact); data.append('\n');
  std::fwrite(data.constData(), 1, size_t(data.size()), stdout); std::fflush(stdout);
}
bool existsOrUnsafe(const QString &path) {
  const auto w = updatepaths::nativeApiPath(path).toStdWString();
  if (GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
  const DWORD err = GetLastError();
  return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
}
void refuseTrust(const QString &root) {
  for (const auto &item : {QStringLiteral(".update-trust-anchor"), QStringLiteral("updates/trust")})
    if (existsOrUnsafe(QDir(root).filePath(item))) fail("preview_trusted_root_forbidden");
}
Layout locate() {
  QDir dir(QCoreApplication::applicationDirPath());
  if (!ordinary(dir.path()) || dir.dirName() != QLatin1String("bin") || !dir.cdUp()) fail("preview_layout_unknown");
  const auto parent = dir.absolutePath();
  const auto ini = dir.filePath(QStringLiteral("gsw-installation.ini"));
  if (existsOrUnsafe(ini)) {
    if (!ordinary(ini) || existsOrUnsafe(dir.filePath(QStringLiteral("current.json"))) ||
        existsOrUnsafe(dir.filePath(QStringLiteral("bootstrap.json")))) fail("preview_layout_unknown");
    refuseTrust(parent);
    QSettings marker(ini, QSettings::IniFormat);
    if (marker.value(QStringLiteral("Installation/AppId")).toString() != QLatin1String(preview::product))
      fail("preview_layout_unknown");
    return {parent, {}, false};
  }
  const auto version = dir.dirName();
  if (!dir.cdUp() || dir.dirName() != QLatin1String("versions") || !dir.cdUp()) fail("preview_layout_unknown");
  const auto root = dir.absolutePath();
  refuseTrust(root);
  if (existsOrUnsafe(dir.filePath(QStringLiteral("gsw-installation.ini")))) fail("preview_layout_unknown");
  const auto boot = parse(read(dir.filePath(QStringLiteral("bootstrap.json")), 16384));
  if (boot.at("schema") != "gsw.native_bootstrap/1" || boot.at("protocol") != 1) fail("preview_trusted_root_forbidden");
  const auto active = parse(read(dir.filePath(QStringLiteral("current.json")), 16384));
  if (active.at("schema") != "gsw.native_active_version/1" || string(active.at("version")) != version)
    fail("preview_inactive_version");
  return {root, parent, true};
}
bool cancelled() {
  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (!input || input == INVALID_HANDLE_VALUE || GetFileType(input) != FILE_TYPE_PIPE) return false;
  DWORD pending = 0;
  return PeekNamedPipe(input, nullptr, 0, nullptr, &pending, nullptr) && pending != 0;
}
void markInternetDownload(const QString &file, const QString &source) {
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) fail("preview_windows_policy_blocked");
  IAttachmentExecute *attachment = nullptr;
  HRESULT result = CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&attachment));
  if (SUCCEEDED(result)) {
    const auto local = QDir::toNativeSeparators(file).toStdWString(), origin = source.toStdWString();
    result = attachment->SetLocalPath(local.c_str());
    if (SUCCEEDED(result)) result = attachment->SetSource(origin.c_str());
    if (SUCCEEDED(result)) result = attachment->Save();
    attachment->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  if (FAILED(result)) fail("preview_windows_policy_blocked");
}
void scheduleSetup(const Layout &layout, const preview::Asset &asset, const QString &path, DWORD waitingPid) {
  if (!waitingPid) fail("preview_exit_identity_required");
  QSettings registered(QString::fromLatin1("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Fubukisama.GaussianSceneWorkbench.Native_is1"),
                       QSettings::Registry64Format);
  const auto registeredRoot = QDir::cleanPath(QDir::fromNativeSeparators(registered.value(QStringLiteral("InstallLocation")).toString()));
  if (registeredRoot.compare(QDir::cleanPath(layout.root), Qt::CaseInsensitive)) fail("preview_setup_registration_mismatch");
  const auto installed = parse(read(QDir(layout.root).filePath(QStringLiteral("installer-payload.json"))));
  QFile oldApp; heldRead(oldApp, QDir(layout.root).filePath(QString::fromLatin1("bin/Gaussian Scene Workbench.exe")));
  if (installed.at("schema") != "gsw-installer-payload-v1" || string(installed.at("executableSha256")) != hash(oldApp))
    fail("preview_setup_identity_mismatch");
  markInternetDownload(path, asset.url);
  QFile held; heldRead(held, path);
  if (held.size() != asset.size || hash(held) != asset.sha256) fail("preview_package_digest_mismatch");
  const auto image = read(QDir(QCoreApplication::applicationDirPath()).filePath(QString::fromLatin1("GSW Preview Setup Waiter.exe")), 4 * 1024 * 1024);
  const auto waiterName = QFileInfo(path).dir().filePath(QStringLiteral("apply-") + digest(image) + QStringLiteral(".exe"));
  if (!existsOrUnsafe(waiterName)) durableWrite(waiterName, image, false);
  QFile waiter; heldRead(waiter, waiterName);
  if (hash(waiter) != digest(image)) fail("preview_waiter_invalid");
  struct Handle { HANDLE h{}; ~Handle(){ if(h) CloseHandle(h); } };
  Handle file, app{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, waitingPid)};
  if (!app.h && GetLastError() != ERROR_INVALID_PARAMETER) fail("preview_exit_identity_required");
  if (app.h) {
    wchar_t buf[32768]{}; DWORD n = 32768;
    if (!QueryFullProcessImageNameW(app.h, 0, buf, &n) ||
        QDir::fromNativeSeparators(QString::fromWCharArray(buf,int(n))).compare(
            QDir(layout.root).filePath(QString::fromLatin1("bin/Gaussian Scene Workbench.exe")), Qt::CaseInsensitive))
      fail("preview_exit_identity_required");
  }
  Handle helper{OpenProcess(SYNCHRONIZE, TRUE, GetCurrentProcessId())};
  if (!helper.h || !DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(_get_osfhandle(held.handle())),
      GetCurrentProcess(), &file.h, 0, TRUE, DUPLICATE_SAME_ACCESS)) fail("preview_setup_not_started");
  HANDLE inherited[3]{file.h, helper.h, app.h};
  SIZE_T count = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &count);
  std::vector<unsigned char> storage(count);
  auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!InitializeProcThreadAttributeList(attrs, 1, 0, &count)) fail("preview_setup_not_started");
  struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes(){DeleteProcThreadAttributeList(p);} } owner{attrs};
  if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
      (app.h ? 3 : 2) * sizeof(HANDLE), nullptr, nullptr)) fail("preview_setup_not_started");
  const auto program = QDir::toNativeSeparators(waiterName).toStdWString();
  auto command = QStringLiteral("\"%1\" --handoff %2 %3 %4").arg(QString::fromStdWString(program))
      .arg(qulonglong(quintptr(file.h))).arg(qulonglong(quintptr(app.h))).arg(qulonglong(quintptr(helper.h))).toStdWString();
  STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = attrs;
  PROCESS_INFORMATION pi{}; wchar_t system[32768]{};
  if (!GetSystemDirectoryW(system, 32768) || !CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, system, &si.StartupInfo, &pi))
    fail("preview_setup_not_started");
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
}

class NativeGithubPreview {
public:
  static int execute(const QStringList &args) {
    if (args.size() < 3 || args.size() > 5 || args[2] != QLatin1String("preview")) fail("preview_arguments_invalid");
    const auto op = args[1];
    const bool check = op == QLatin1String("--check"), prepare = op == QLatin1String("--prepare");
    if (!check && !prepare && op != QLatin1String("--apply-prepared")) fail("preview_arguments_invalid");
    if ((check || prepare) && args.size() != 3) fail("preview_arguments_invalid");
    const Layout layout = locate();
    QDir root(layout.root);
    const auto cache = root.filePath(QStringLiteral(".gsw-github-preview"));
    const auto marker = QDir(cache).filePath(QStringLiteral("prepared.json"));
#ifdef GSW_PREVIEW_FIXTURE
    // An independently marked earlier installation is needed to exercise the
    // forward transition to the actual candidate; never lower production.
    auto running = qint64(GSW_PREVIEW_SEQUENCE - 1);
#else
    auto running = qint64(GSW_PREVIEW_SEQUENCE);
#endif
    const auto installedSequence = QDir(layout.managed ? layout.slot : layout.root).filePath(QStringLiteral("gsw-preview-sequence.txt"));
    if (existsOrUnsafe(installedSequence)) {
      bool ok = false; const auto sequence = read(installedSequence, 32).trimmed().toLongLong(&ok);
      if (!ok || sequence < 1) fail("preview_installed_sequence_invalid");
      running = qMax(running, sequence);
    }
    const auto release = preview::resolve(running);
    if (!release.newer) {
      if (!check) fail("preview_no_newer_release");
      emitEvent(QStringLiteral("current"), release.sequence ? QStringLiteral("PREVIEW_CURRENT") : QStringLiteral("PREVIEW_NO_RELEASE"));
      return 0;
    }
    const QJsonObject offer{{QStringLiteral("buildId"), release.build}, {QStringLiteral("displayVersion"), release.display},
      {QStringLiteral("channel"), QStringLiteral("preview")}, {QStringLiteral("archiveLength"), layout.managed ? release.archive.size : release.setup.size}};
    if (check) { emitEvent(QStringLiteral("available"), QStringLiteral("GITHUB_PREVIEW"), {{QStringLiteral("offer"),offer}}); return 0; }
    ensureDirectory(cache);
    NativeInstallationLock cacheLock(cache);
    if (!cacheLock.locked()) fail("preview_busy");
    const auto &asset = layout.managed ? release.archive : release.setup;
    const auto file = QDir(cache).filePath(asset.sha256 + (layout.managed ? QStringLiteral(".zip") : QStringLiteral(".exe")));
    const Json expected{{"schema","gsw.github_preview_prepared/1"},{"channel","preview"},{"buildId",release.build.toStdString()},
      {"descriptorSha256",release.manifestHash.toStdString()},{"sha256",asset.sha256.toStdString()}};
    if (prepare) {
      QElapsedTimer timer; timer.start();
      emitEvent(QStringLiteral("downloading"), {}, {{QStringLiteral("totalBytes"),asset.size}});
      preview::download(asset, file, [&](qint64 received, qint64 total) {
        if (cancelled()) fail("preview_cancelled");
        if (received == total || timer.elapsed() >= 200) {
          emitEvent(QStringLiteral("downloading"), {}, {{QStringLiteral("receivedBytes"),received},{QStringLiteral("totalBytes"),total}});
          timer.restart();
        }
      });
      if (cancelled()) fail("preview_cancelled");
      durableWrite(marker, QByteArray::fromStdString(expected.dump()), true);
      emitEvent(QStringLiteral("prepared"), QStringLiteral("PREPARED"), {{QStringLiteral("offer"),offer}}); return 0;
    }
    if (args.size() != 5 || args[3] != release.build || parse(read(marker, 16384)) != expected)
      fail("preview_prepared_identity_changed");
    NativeInstallationLock activationLock(layout.root);
    if (!activationLock.locked()) fail("preview_busy");
    locate(); // The active slot or root type may have changed since the request.
    if (layout.managed) {
      requireUnanchoredBootstrap(layout.root);
      const auto result = NativeActivationCore::publish(layout.root, file, release.offer, activationLock,
          NativeActivationCore::Admission::GithubPreview, release.manifestHash);
      if (!result.ok) throw Failure{result.code};
    } else {
      bool ok = false; const auto pid = args[4].toUInt(&ok);
      if (!ok) fail("preview_exit_identity_required");
      scheduleSetup(layout, asset, file, pid);
    }
    // Fixed-layout installation is not complete until the external installer
    // succeeds after exit. Do not report a scheduled installer as an activation.
    if (layout.managed) QFile::remove(marker);
    emitEvent(layout.managed ? QStringLiteral("activated") : QStringLiteral("scheduled"),
              layout.managed ? QStringLiteral("NEXT_LAUNCH") : QStringLiteral("SETUP_SCHEDULED"));
    return 0;
  }
};
} // namespace gsw

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try { return gsw::NativeGithubPreview::execute(app.arguments()); }
  catch (const gsw::activation::Failure &e) { gsw::emitEvent(QStringLiteral("failed"), e.code); }
  catch (const std::exception &) { gsw::emitEvent(QStringLiteral("failed"), QStringLiteral("preview_invalid_response")); }
  return 2;
}
