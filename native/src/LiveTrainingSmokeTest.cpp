#include "TrainingIsolationSmokeTest.h"
#include "NativeViewport.h"
#include "ProcessSupervisor.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>

namespace gsw {

bool runLiveTrainingSmokeTest(NativeViewport &view, const QString &configPath) {
  QFile cfg(configPath);
  if (!cfg.open(QIODevice::ReadOnly) || cfg.size() > 8192) return false;
  const auto config = QJsonDocument::fromJson(cfg.readAll()).object();
  const QDir root = QFileInfo(cfg).absoluteDir();
  const auto backend = config.value(QStringLiteral("backend")).toString();
  const auto runId = config.value(QStringLiteral("runId")).toString();
  const auto dataset = config.value(QStringLiteral("datasetIdentity")).toString();
  const int total = config.value(QStringLiteral("iterations")).toInt();
  if ((backend != QStringLiteral("3dgs") && backend != QStringLiteral("2dgs")) ||
      runId.size() != 32 || dataset.size() != 64 || total < 100) return false;
  QFile stream(root.filePath(QStringLiteral("observer.log")));
  QFile result(root.filePath(QStringLiteral("viewport.jsonl")));
  if (!stream.open(QIODevice::ReadOnly) || !result.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  const auto line = [&](const QJsonObject &record) {
    result.write(QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n');
    result.flush();
  };
  view.window()->resize(1280, 800);
  auto grid = view.viewportGridSettings();
  grid.gridVisible = false;
  grid.axesVisible = false;
  if (!view.setViewportGridSettings(grid)) return false;
  view.setShowObservationTrackball(false);
  view.setEditToolsLocked(true);
  view.beginProcessingPreview();
  view.setLichtFeldTrainingPreviewEnabled(true);
  view.setProcessingStage(QStringLiteral("train"), 0, total, 0);
  const QImage empty = view.grabFramebuffer();
  if (empty.isNull() || !view.context() || !view.context()->isValid() || view.window()->isActiveWindow()) {
    line({{QStringLiteral("kind"), QStringLiteral("initialization-failed")},
          {QStringLiteral("framebufferAvailable"), !empty.isNull()},
          {QStringLiteral("contextValid"), view.context() && view.context()->isValid()},
          {QStringLiteral("activeWindow"), view.window()->isActiveWindow()}});
    return false;
  }
  view.makeCurrent();
  auto *gl = view.context()->functions();
  const auto renderer = QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
  const auto version = QString::fromLatin1(reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));
  const bool linked = view.startupBaseRenderingAvailable() && view.mGaussianProgram && view.mGaussianProgram->isLinked() &&
      (backend == QStringLiteral("2dgs") || view.lichtFeldTrainingProgram());
  view.doneCurrent();
  if (!linked) {
    line({{QStringLiteral("kind"), QStringLiteral("unsupported-gl")}, {QStringLiteral("glRenderer"), renderer},
          {QStringLiteral("glVersion"), version}});
    return false;
  }
  line({{QStringLiteral("kind"), QStringLiteral("ready")}, {QStringLiteral("glRenderer"), renderer},
        {QStringLiteral("glVersion"), version},
        {QStringLiteral("hidden"), view.window()->testAttribute(Qt::WA_DontShowOnScreen)}});
  const auto centerDigest = [](const QImage &image) {
    const auto crop = image.copy(image.width()/4, image.height()/4, image.width()/2, image.height()/2)
                           .convertToFormat(QImage::Format_RGB32);
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(crop.constBits()), crop.sizeInBytes()),
                                    QCryptographicHash::Sha256).toHex();
  };
  const auto emptyDigest = centerDigest(empty);
  const auto navigate = [&] {
    const QPointF center(view.width() * .44, view.height() * .48);
    const auto drag = [&](Qt::MouseButton button, const QPointF &offset) {
      const QPointF end = center + offset;
      QMouseEvent press(QEvent::MouseButtonPress, center, view.mapToGlobal(center.toPoint()), button, button, Qt::NoModifier);
      QMouseEvent move(QEvent::MouseMove, end, view.mapToGlobal(end.toPoint()), Qt::NoButton, button, Qt::NoModifier);
      QMouseEvent release(QEvent::MouseButtonRelease, end, view.mapToGlobal(end.toPoint()), button, Qt::NoButton, Qt::NoModifier);
      for (auto *event : {&press, &move, &release}) QApplication::sendEvent(&view, event);
    };
    const auto beforeOrbit = view.viewMatrix();
    drag(Qt::LeftButton, {44., 21.});
    const bool orbit = beforeOrbit != view.viewMatrix();
    const auto beforePan = view.viewTarget();
    drag(Qt::MiddleButton, {19., -14.});
    const bool pan = beforePan != view.viewTarget();
    const auto beforeZoom = view.viewDistance();
    QWheelEvent wheel(center, view.mapToGlobal(center.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QApplication::sendEvent(&view, &wheel);
    const bool zoom = beforeZoom != view.viewDistance();
    line({{QStringLiteral("kind"), QStringLiteral("navigation")}, {QStringLiteral("orbit"), orbit},
          {QStringLiteral("pan"), pan}, {QStringLiteral("zoom"), zoom}});
    return orbit && pan && zoom;
  };

  TrainingObservationAdmission admission(runId, backend, dataset, root.filePath(QStringLiteral("model")));
  QHash<QString, QJsonObject> recentFiles;
  QJsonObject gpuStream;
  int metricIteration = 0, latestFileIteration = 0, lastDrawn = 0, count = 0;
  int preNavigation = 0, prePause = 0, pauseFile = 0, resumeAtLeast = 0;
  bool navigation = false, resumed = false, resultOk = false, rejectedForeign = false, pausedShared = false;
  const bool surfel = backend == QStringLiteral("2dgs");
  QByteArray previousDigest;
  QString pausedPath;
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < 110000 && !QFileInfo::exists(root.filePath(QStringLiteral("producer-exit.json")))) {
    QEventLoop events;
    QTimer::singleShot(80, &events, &QEventLoop::quit);
    events.exec();
    if (view.window()->isActiveWindow()) return false;
    while (!stream.atEnd()) {
      const auto offset = stream.pos();
      const auto data = stream.readLine();
      if (!data.endsWith('\n')) { stream.seek(offset); break; }
      constexpr QByteArrayView workerPrefix("[worker-event] ");
      constexpr QByteArrayView ipcPrefix("[gsw-training-gpu-preview] ");
      if (QByteArrayView(data).startsWith(ipcPrefix)) {
        TrainingGpuPreviewDescriptor descriptor;
        if (parseTrainingGpuPreviewDescriptor(QByteArrayView(data).sliced(ipcPrefix.size()), &descriptor) &&
            admission.acceptGpu(descriptor.observationStream)) {
          gpuStream = descriptor.state == TrainingGpuPreviewState::Ready ? descriptor.observationStream : QJsonObject{};
          view.setTrainingGpuPreviewDescriptor(descriptor);
        }
        continue;
      }
      if (!QByteArrayView(data).startsWith(workerPrefix)) continue;
      const auto status = ProcessSupervisor::decodeStatusEvent(data.mid(workerPrefix.size()));
      if (!status || status->state != QStringLiteral("running")) continue;
      metricIteration = std::max(metricIteration, status->iteration.value_or(0));
      view.setProcessingStage(QStringLiteral("train"), metricIteration, total, 100 * metricIteration / total);
      if (status->previewObservation.isEmpty()) continue;
      ObservationFileIntegrity integrity;
      const int iteration = status->previewIteration.value_or(-1);
      if (!rejectedForeign) {
        auto foreign = status->previewObservation;
        foreign.insert(QStringLiteral("runId"), QString(32, QLatin1Char('f')));
        rejectedForeign = !admission.acceptFile(foreign, status->previewPath, iteration,
                                                 status->gaussianCount.value_or(0), integrity);
      }
      if (!admission.acceptFile(status->previewObservation, status->previewPath, iteration,
                                status->gaussianCount.value_or(0), integrity)) continue;
      latestFileIteration = iteration;
      recentFiles.insert(status->previewPath, status->previewObservation);
      if (recentFiles.size() > 8) {
        for (auto it = recentFiles.begin(); it != recentFiles.end();) {
          if (it.key() != view.scenePath() && !QFileInfo::exists(it.key())) it = recentFiles.erase(it);
          else ++it;
        }
      }
      view.setPreviewScene(status->previewPath, status->gaussianCount.value_or(0), integrity);
    }
    if (view.processingPreviewPaused()) {
      if (view.scenePath() != pausedPath) return false;
      if (metricIteration > prePause && (latestFileIteration > pauseFile || pausedShared)) {
        resumeAtLeast = latestFileIteration;
        view.setProcessingPreviewPaused(false);
        line({{QStringLiteral("kind"), QStringLiteral("resume-preview")},
              {QStringLiteral("optimizerIteration"), metricIteration}, {QStringLiteral("latestFileIteration"), resumeAtLeast}});
      }
      continue;
    }
    const auto image = view.grabFramebuffer();
    const bool ipc = view.trainingGpuPreviewForScene() && view.mTrainingGpuPreview.hasFrame();
    auto frame = ipc ? gpuStream : recentFiles.value(view.scenePath());
    const int iteration = ipc ? int(view.mTrainingGpuPreview.iteration()) : frame.value(QStringLiteral("iteration")).toInt();
    if (frame.isEmpty() || iteration <= lastDrawn || iteration >= total || metricIteration >= total ||
        !view.gaussianRenderingAvailable() || image.isNull()) continue;
    const auto projection = view.trainingProjection(view.projectionMatrix(), ipc);
    if (surfel ? projection != NativeViewport::TrainingProjection::Surfel || !view.sourceHasSurfelAttributes()
               : projection != NativeViewport::TrainingProjection::LichtFeld || !view.mLichtFeldTrainingProgram ||
                 view.mLichtFeldTrainingProgramFailed) continue;
    const auto digest = centerDigest(image);
    if (digest == emptyDigest || digest == previousDigest) continue;
    ++count;
    previousDigest = digest;
    lastDrawn = iteration;
    frame.insert(QStringLiteral("kind"), QStringLiteral("drawn"));
    frame.insert(QStringLiteral("iteration"), iteration);
    frame.insert(QStringLiteral("transport"), ipc ? QStringLiteral("gpu_shared_v2") : QStringLiteral("atomic_ply"));
    if (ipc) frame.insert(QStringLiteral("generation"), qint64(view.mTrainingGpuPreview.generation()));
    frame.insert(QStringLiteral("drawCount"), ipc ? qint64(view.mTrainingGpuPreview.pointCount()) : view.renderedPointCount());
    frame.insert(QStringLiteral("backendProjection"), surfel ? QStringLiteral("native_2dgs_surfel") : QStringLiteral("lichtfeld"));
    frame.insert(QStringLiteral("framebufferRoiSha256"), QString::fromLatin1(digest));
    frame.insert(QStringLiteral("optimizerIteration"), metricIteration);
    frame.insert(QStringLiteral("viewerElapsedMs"), elapsed.elapsed());
    if (!image.save(root.filePath(QStringLiteral("drawn-%1.png").arg(count)))) return false;
    line(frame);

    if (count == 1) { navigation = navigate(); preNavigation = std::max(iteration, metricIteration); }
    else if (count == 2) {
      prePause = std::max(metricIteration, iteration);
      pauseFile = latestFileIteration;
      pausedPath = view.scenePath();
      pausedShared = ipc;
      view.setProcessingPreviewPaused(true);
      line({{QStringLiteral("kind"), QStringLiteral("pause-preview")}, {QStringLiteral("optimizerIteration"), prePause}});
    } else {
      resumed = iteration >= resumeAtLeast && metricIteration > prePause;
      resultOk = count >= 3 && navigation && metricIteration > preNavigation && resumed && rejectedForeign;
      if (resultOk || count >= 8) break;
    }
  }
  line({{QStringLiteral("kind"), QStringLiteral("result")}, {QStringLiteral("pass"), resultOk},
        {QStringLiteral("drawn"), count}, {QStringLiteral("navigation"), navigation},
        {QStringLiteral("resumedLatest"), resumed}, {QStringLiteral("rejectedForeign"), rejectedForeign}});
  return resultOk;
}

} // namespace gsw
