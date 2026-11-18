#pragma once

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

namespace gsw {

struct ObservationFileIntegrity {
  QByteArray sha256;
  int count = 0;
  int rowFloats = 0;
};

// One admission lifetime per optimizer run. File generations and IPC slot
// generations are different namespaces and never compared to each other.
class TrainingObservationAdmission final {
public:
  TrainingObservationAdmission() = default;
  TrainingObservationAdmission(QString run, QString backend, QString dataset, QString output)
      : mRun(std::move(run)), mBackend(std::move(backend)), mDataset(std::move(dataset)),
        mOutput(std::move(output)) {}

  [[nodiscard]] bool isBound() const { return !mRun.isEmpty(); }

  bool acceptFile(const QJsonObject &frame, const QString &path, int iteration,
                  qint64 sourceCount, ObservationFileIntegrity &file) {
    file = {};
    if (!isBound()) return frame.isEmpty();
    const int generation = frame.value(QStringLiteral("generation")).toInt();
    const int count = frame.value(QStringLiteral("previewCount")).toInt();
    const int floats = mBackend == QStringLiteral("2dgs") ? 13 : 14;
    const auto digest = frame.value(QStringLiteral("sha256")).toString();
    bool timeOk = false;
    const auto timestamp = frame.value(QStringLiteral("producerMonotonicNs")).toString();
    const auto producerTime = timestamp.toULongLong(&timeOk);
    if (frame.value(QStringLiteral("transport")).toString() != QStringLiteral("atomic_ply") ||
        frame.value(QStringLiteral("iteration")).toInt(-1) != iteration || iteration <= mFileIteration ||
        generation <= mFileGeneration || count <= 0 || count > 500'000 || count > sourceCount ||
        frame.value(QStringLiteral("sourceCount")).toInteger(-1) != sourceCount ||
        frame.value(QStringLiteral("rowFloats")).toInt() != floats || !hex(digest, 64) ||
        !timeOk || !producerTime || timestamp != QString::number(producerTime) ||
        producerTime <= mFileProducerTime) return false;
    const auto root = QFileInfo(mOutput).canonicalFilePath();
    const auto canonical = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || canonical.isEmpty()) return false;
    const auto parts = QDir(root).relativeFilePath(canonical).split(QLatin1Char('/'));
    if (parts.size() != 4 || parts[0] != QStringLiteral(".gsw") || parts[1] != QStringLiteral("previews") ||
        !hex(parts[2], 32) || parts[3] != QStringLiteral("preview_%1.ply").arg(generation, 6, 10, QLatin1Char('0')))
      return false;
    if (!acceptStream(frame)) return false;
    mFileIteration = iteration;
    mFileGeneration = generation;
    mFileProducerTime = producerTime;
    file = {QByteArray::fromHex(digest.toLatin1()), count, floats};
    return true;
  }

  bool acceptGpu(const QJsonObject &stream) {
    return !isBound() ? stream.isEmpty() : mBackend == QStringLiteral("3dgs") && acceptStream(stream);
  }

private:
  static bool hex(const QString &value, int length) {
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-f]+\\z"));
    return value.size() == length && pattern.match(value).hasMatch();
  }

  bool acceptStream(const QJsonObject &message) {
    const auto frame = message.value(QStringLiteral("frame")).toObject();
    const auto source = message.value(QStringLiteral("datasetIdentity"));
    const auto data = source.toString();
    if (!hex(mRun, 32) || message.value(QStringLiteral("runId")).toString() != mRun ||
        message.value(QStringLiteral("schema")).toString() != QStringLiteral("gsw.training_observation/1") ||
        message.value(QStringLiteral("backend")).toString() != mBackend ||
        !source.isString() || (!data.isEmpty() && !hex(data, 64)) || (!mDataset.isEmpty() && data != mDataset) ||
        !hex(message.value(QStringLiteral("trainingIdentity")).toString(), 64) ||
        frame.value(QStringLiteral("coordinates")).toString() != QStringLiteral("training") ||
        frame.value(QStringLiteral("units")).toString() != QStringLiteral("u")) return false;
    if (!data.isEmpty() && !frame.value(QStringLiteral("canonicalToTraining")).toObject()
                                 .value(QStringLiteral("applied")).toBool()) return false;
    QJsonObject identity;
    for (const auto &key : {QStringLiteral("schema"), QStringLiteral("runId"), QStringLiteral("backend"),
                           QStringLiteral("datasetIdentity"), QStringLiteral("trainingIdentity"), QStringLiteral("frame")})
      identity.insert(key, message.value(key));
    if (!mStream.isEmpty()) return mStream == identity;
    mStream = std::move(identity);
    return true;
  }

  QString mRun, mBackend, mDataset, mOutput;
  QJsonObject mStream;
  int mFileIteration = -1, mFileGeneration = 0;
  quint64 mFileProducerTime = 0;
};

} // namespace gsw
