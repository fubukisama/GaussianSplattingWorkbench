// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <functional>

namespace gsw::preview {
inline constexpr auto product = "Fubukisama.GaussianSceneWorkbench.Native";
inline constexpr auto helperName = "GSW GitHub Preview Update.exe";
inline constexpr auto manifestName = "gsw-windows-x64-preview.json";
inline constexpr auto archiveName = "gsw-windows-x64-managed.zip";
struct Asset { QString name, url, sha256; qint64 size = 0; };
struct Release {
  bool newer = false;
  qint64 sequence = 0;
  QString tag, build, manifestHash, display;
  Asset setup, archive;
  QByteArray offer;
};
// Authority comes only from the immutable release on the hard-coded public
// repository. No file, environment variable or CLI argument supplies a feed in
// production. Authenticode and the independent, signed protocol-2 path are not involved.
Release resolve(qint64 runningSequence);
using Progress = std::function<void(qint64, qint64)>;
void download(const Asset &, const QString &destination, const Progress &);
} // namespace gsw::preview
