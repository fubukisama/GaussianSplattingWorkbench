#include "CameraPhotoIO.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QSet>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <algorithm>

namespace gsw {
namespace {
constexpr qint64 kLimitBytes = 256 * 1024 * 1024;
constexpr qint64 kLimitPixels = 64'000'000;

bool linked(const QFileInfo &info) {
  if (info.isSymLink()) return true;
#ifdef Q_OS_WIN
  const auto attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()));
  if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
  return false;
}

bool containedFile(const QString &root, const QString &relative) {
  if (!validCameraPhotoRelativePath(relative) || !QDir::isAbsolutePath(root)) return false;
#ifdef Q_OS_WIN
  constexpr auto pathCase = Qt::CaseInsensitive;
#else
  constexpr auto pathCase = Qt::CaseSensitive;
#endif
  const QString base = QDir(root).absolutePath();
  for (QString ancestor = base;;) {
    const QFileInfo info(ancestor);
    if (linked(info)) return false;
    const auto parent = info.absoluteDir().absolutePath();
    if (parent == ancestor) break;
    ancestor = parent;
  }
  if (QDir(base).canonicalPath().compare(base, pathCase) != 0) return false;
  QString path = base;
  for (const auto &part : relative.split(QLatin1Char('/'))) {
    path = QDir(path).filePath(part);
    if (linked(QFileInfo(path))) return false;
  }
  const QFileInfo info(path);
  return info.isFile() && info.canonicalFilePath().startsWith(
      base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/'), pathCase);
}
} // namespace

bool validCameraPhotoDigest(const QString &digest) {
  if (digest.size() != 64) return false;
  for (const auto ch : digest)
    if ((ch < QLatin1Char('0') || ch > QLatin1Char('9')) &&
        (ch < QLatin1Char('a') || ch > QLatin1Char('f'))) return false;
  return true;
}

bool validCameraPhotoRelativePath(const QString &path) {
  if (path.isEmpty() || path.size() > 32767 || path.contains(QLatin1Char('\\')) ||
      path.contains(QLatin1Char(':')) || path.contains(QChar::Null) || QDir::isAbsolutePath(path)) return false;
  for (const auto &part : path.split(QLatin1Char('/')))
    if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) return false;
  return true;
}

QHash<QString, CameraPhotoReference> cameraPhotoReferences(const QJsonObject &review) {
  const auto root = review.value(QStringLiteral("dataset")).toString();
  const auto rows = review.value(QStringLiteral("rows")).toArray();
  const auto snapshots = review.value(QStringLiteral("snapshots")).toArray();
  if (review.value(QStringLiteral("ready")) != QJsonValue(true) ||
      review.value(QStringLiteral("schema")).toString() != QStringLiteral("gsw.reconstruction_review/1") ||
      !validCameraPhotoDigest(review.value(QStringLiteral("identity")).toString()) ||
      !QDir::isAbsolutePath(root) || rows.size() > 20000 || snapshots.size() > 100000) return {};
  QHash<QString, QString> hashes;
  QSet<QString> duplicatePaths;
  for (const auto &value : snapshots) {
    const auto record = value.toObject();
    const auto path = record.value(QStringLiteral("path")).toString();
    const auto hash = record.value(QStringLiteral("sha256")).toString();
    if (!validCameraPhotoRelativePath(path) || !validCameraPhotoDigest(hash)) continue;
    if (hashes.contains(path)) duplicatePaths.insert(path);
    else hashes.insert(path, hash);
  }
  for (const auto &path : duplicatePaths) hashes.remove(path);
  QHash<QString, CameraPhotoReference> result;
  QSet<QString> seenIds;
  for (const auto &value : rows) {
    const auto row = value.toObject();
    const auto id = row.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) continue;
    if (seenIds.contains(id)) { result.remove(id); continue; }
    seenIds.insert(id);
    const auto path = row.value(QStringLiteral("imagePath")).toString();
    const auto it = hashes.constFind(path);
    if (it != hashes.cend()) result.insert(id, {root, path, *it});
  }
  return result;
}

CameraPhotoRead readCameraPhoto(const CameraPhotoReference &reference, const QSize limit,
                               const std::function<bool()> &cancelled) {
  const auto stopped = [&] { return cancelled && cancelled(); };
  if (stopped()) return {{}, CameraPhotoStatus::Cancelled};
  if (!validCameraPhotoDigest(reference.sha256) ||
      !validCameraPhotoRelativePath(reference.relativePath)) return {};
  const auto path = QDir(reference.datasetRoot).filePath(reference.relativePath);
  if (!containedFile(reference.datasetRoot, reference.relativePath))
    return {{}, QFileInfo::exists(path) ? CameraPhotoStatus::Unreadable : CameraPhotoStatus::Missing};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > kLimitBytes)
    return {{}, CameraPhotoStatus::Unreadable};
  // Decode the exact immutable bytes that passed the hash, not a reopened
  // file or bytes read after a same-size in-place replacement.
  QCryptographicHash hash(QCryptographicHash::Sha256);
  QByteArray bytes;
  bytes.reserve(file.size());
  while (!file.atEnd()) {
    if (stopped()) return {{}, CameraPhotoStatus::Cancelled};
    const auto block = file.read(std::min(qint64(1024 * 1024), kLimitBytes + 1 - bytes.size()));
    if (block.isEmpty() || bytes.size() + block.size() > kLimitBytes) return {{}, CameraPhotoStatus::Unreadable};
    hash.addData(block);
    bytes.append(block);
  }
  if (stopped()) return {{}, CameraPhotoStatus::Cancelled};
  if (QString::fromLatin1(hash.result().toHex()) != reference.sha256 ||
      !containedFile(reference.datasetRoot, reference.relativePath))
    return {{}, CameraPhotoStatus::Changed};
  QBuffer source(&bytes);
  if (!source.open(QIODevice::ReadOnly)) return {{}, CameraPhotoStatus::Unreadable};
  QImageReader reader(&source);
  reader.setAutoTransform(true);
  const auto dimensions = reader.size();
  if (!dimensions.isValid() || dimensions.isEmpty() || qint64(dimensions.width()) * dimensions.height() > kLimitPixels)
    return {{}, CameraPhotoStatus::Unreadable};
  reader.setScaledSize(dimensions.boundedTo(dimensions.scaled(limit, Qt::KeepAspectRatio)));
  auto image = reader.read();
  if (stopped()) return {{}, CameraPhotoStatus::Cancelled};
  if (image.isNull()) return {{}, CameraPhotoStatus::Unreadable};
  return {std::move(image), CameraPhotoStatus::Available};
}

QString cameraPhotoStatusText(const CameraPhotoStatus status) {
  switch (status) {
  case CameraPhotoStatus::Available: return {};
  case CameraPhotoStatus::Missing:
    return QCoreApplication::translate("Workbench", "对应原图找不到。");
  case CameraPhotoStatus::Changed:
    return QCoreApplication::translate("Workbench", "对应原图已变化，未使用替换文件。");
  case CameraPhotoStatus::Unreadable:
    return QCoreApplication::translate("Workbench", "原图不可读、已变化或超过预览预算。");
  case CameraPhotoStatus::Cancelled:
    return {};
  default:
    return QCoreApplication::translate("Workbench", "没有可核实的原图关联。");
  }
}

} // namespace gsw
