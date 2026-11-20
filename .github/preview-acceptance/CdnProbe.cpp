// SPDX-License-Identifier: GPL-3.0-or-later
// Remote test executable only. Run the exact product download implementation
// against known bytes from a pre-existing immutable public release. No
// configurable URL, executable launch, client credentials or TLS overrides.
#include "NativeGithubPreviewFeed.h"
#include "NativeActivationFiles.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() != 2) return 2;
  try {
    const QDir root(app.arguments()[1]);
    if (!root.exists() || !root.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) return 2;
    const QString base = QStringLiteral("https://github.com/fubukisama/GaussianSplattingWorkbench/releases/download/v0.3.9-native-preview.20261118/");
    const QString name = QStringLiteral("Gaussian-Scene-Workbench-0.3.9-native-preview-windows-x64-setup.exe");
    const gsw::preview::Asset setup{name, base + name, QStringLiteral("b4707992b476c0354d561a20d000349ca8be3759ca536aa289222f5656b19bce"),113901550};
    const gsw::preview::Asset checksum{name + ".sha256", base + name + ".sha256", QStringLiteral("b0d2ebb2ed4e98986f96fe175ee9e569604a2cd650ade752ffcd85e8fa996cd5"),134};
    const auto quiet = [](qint64,qint64){};
    gsw::preview::download(checksum, root.filePath("checksum.txt"), quiet);
    gsw::preview::download(setup, root.filePath("setup.data"), quiet);
    const auto proof = gsw::activation::read(root.filePath("checksum.txt"), 1024);
    if (!proof.startsWith(setup.sha256.toLatin1())) return 2;
    auto wrong = checksum; wrong.sha256.fill(QLatin1Char('0'),64);
    try { gsw::preview::download(wrong,root.filePath("wrong.data"),quiet); return 2; }
    catch (const gsw::activation::Failure &e) {
      if (e.code != QLatin1String("preview_package_digest_mismatch") || QFileInfo::exists(root.filePath("wrong.data"))) return 2;
    }
    std::printf("REAL_CDN_OK bytes=113901550 sha256=%s corruptRefused=true executed=false\n", setup.sha256.toLatin1().constData());
    return 0;
  } catch (const gsw::activation::Failure &e) { std::fprintf(stderr,"%s\n",qPrintable(e.code)); }
    catch (...) { std::fprintf(stderr,"unexpected readback failure\n"); }
  return 2;
}
