// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeGithubPreviewFeed.h"
#include "NativeActivationCore.h"
#include "NativeUnanchoredBootstrap.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <cstdio>

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
    if (!layout.managed && !check) fail("preview_setup_manual_only");
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
    if (check) {
      emitEvent(QStringLiteral("available"), layout.managed ? QStringLiteral("GITHUB_PREVIEW") : QStringLiteral("GITHUB_PREVIEW_MANUAL"),
                {{QStringLiteral("offer"),offer}});
      return 0;
    }
    ensureDirectory(cache);
    NativeInstallationLock cacheLock(cache);
    if (!cacheLock.locked()) fail("preview_busy");
    const auto &asset = release.archive;
    const auto file = QDir(cache).filePath(asset.sha256 + QStringLiteral(".zip"));
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
    const auto active = locate();
    if (!active.managed || active.root != layout.root || active.slot != layout.slot)
      fail("preview_inactive_version");
    requireUnanchoredBootstrap(layout.root);
    const auto result = NativeActivationCore::publish(layout.root, file, release.offer, activationLock,
        NativeActivationCore::Admission::GithubPreview, release.manifestHash);
    if (!result.ok) throw Failure{result.code};
    QFile::remove(marker);
    emitEvent(QStringLiteral("activated"), QStringLiteral("NEXT_LAUNCH"));
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
