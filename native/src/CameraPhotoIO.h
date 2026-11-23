#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace gsw {

// Only construct from a verified model/data binding or the scoped review
// report. A display name or a bare cameras.json is never a read permission.
struct CameraPhotoReference final {
  QString datasetRoot;
  QString relativePath;
  QString sha256;
  friend bool operator==(const CameraPhotoReference &, const CameraPhotoReference &) = default;
};

enum class CameraPhotoStatus { Available, Unlinked, Missing, Changed, Unreadable, Cancelled };
struct CameraPhotoRead final {
  QImage pixels;
  CameraPhotoStatus status = CameraPhotoStatus::Unlinked;
};

[[nodiscard]] bool validCameraPhotoDigest(const QString &digest);
[[nodiscard]] bool validCameraPhotoRelativePath(const QString &path);
[[nodiscard]] QHash<QString, CameraPhotoReference> cameraPhotoReferences(const QJsonObject &review);
[[nodiscard]] CameraPhotoRead readCameraPhoto(const CameraPhotoReference &reference, QSize limit,
    const std::function<bool()> &cancelled = {});
[[nodiscard]] QString cameraPhotoStatusText(CameraPhotoStatus status);

} // namespace gsw
