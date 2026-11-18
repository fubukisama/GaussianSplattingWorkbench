#include "TrainingIsolationSmokeTest.h"
#include "AppLanguage.h"
#include "NativeViewport.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace gsw {
namespace {
bool waitFor(const std::function<bool()> &condition) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!condition() && elapsed.elapsed() < 10000) {
    QEventLoop loop;
    QTimer::singleShot(10, &loop, &QEventLoop::quit);
    loop.exec();
  }
  return condition();
}
QString writePoints(const QTemporaryDir &directory, const QString &name, int count) {
  QFile file(directory.filePath(name + QStringLiteral(".ply")));
  if (!file.open(QIODevice::WriteOnly)) return {};
  file.write("ply\nformat ascii 1.0\nelement vertex " + QByteArray::number(count) +
             "\nproperty float x\nproperty float y\nproperty float z\n"
             "property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n");
  for (int index = 0; index < count; ++index)
    file.write(QByteArray::number((index % 5) * .1) + " " +
               QByteArray::number((index / 5) * .1) + " 0 231 47 193\n");
  return file.fileName();
}
}

bool runTrainingIsolationSmokeTest(NativeViewport &viewport) {
  QTemporaryDir directory;
  if (!directory.isValid()) return false;
  const auto check = [](bool okay, const char *message) {
    std::fprintf(stderr, "Training isolation %s: %s\n", okay ? "PASS" : "FAIL", message);
    std::fflush(stderr);
    if (!okay) qCritical() << "Training isolation FAIL:" << message;
    return okay;
  };
  SceneObject first;
  first.id = QStringLiteral("reference-first");
  first.path = writePoints(directory, QStringLiteral("first"), 15);
  first.vertexCount = 15;
  first.translation = {1.25F, -2.5F, .75F};
  first.rotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 30);
  first.scale = {1.5F, 2.F, .5F};
  SceneObject second;
  second.id = QStringLiteral("reference-second");
  second.path = writePoints(directory, QStringLiteral("second"), 20);
  second.vertexCount = 20;
  second.translation = {-3.F, 1.F, 2.F};
  second.rotation = QQuaternion::fromAxisAndAngle(1, 0, 0, -20);
  second.scale = {.75F, .5F, 1.25F};
  const QString preview = writePoints(directory, QStringLiteral("task-preview"), 30);
  if (first.path.isEmpty() || second.path.isEmpty() || preview.isEmpty()) return false;
  viewport.setSceneObjects({first, second}, first.id);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == first.path && viewport.renderedPointCount() == 15;
      }), "first reference is loaded through the real viewport")) return false;
  if (!check(viewport.activateSceneObject(second.id) && waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == second.path && viewport.renderedPointCount() == 20;
      }), "second reference is independently loaded")) return false;
  viewport.activateSceneObject(first.id);
  if (!check(viewport.focusModel(), "the original reference is framed through the normal navigation interface")) return false;
  const QImage referenceFrame = viewport.grabFramebuffer();
  const auto referenceTarget = viewport.viewTarget();
  const auto referenceDistance = viewport.viewDistance();
  const auto referenceAngles = viewport.viewOrbitAngles();
  const auto referenceGpuBytes = viewport.resourceBudgetStatus().managedGpuBytes;
  if (!check(referenceGpuBytes > 0, "reference display allocations are accounted before training")) return false;
  QVector<QPoint> referencePixels;
  for (int y = referenceFrame.height() / 4; y < referenceFrame.height() * 3 / 4; ++y) {
    for (int x = referenceFrame.width() / 4; x < referenceFrame.width() * 3 / 4; ++x) {
      const QRgb pixel = referenceFrame.pixel(x, y);
      if (qRed(pixel) > 120 && qBlue(pixel) > 100 && qGreen(pixel) < 100 &&
          qRed(pixel) - qGreen(pixel) > 60 && qBlue(pixel) - qGreen(pixel) > 40)
        referencePixels.append({x, y});
    }
  }
  std::fprintf(stderr, "Reference display: %lld model-colored pixels, %lld managed GPU bytes\n",
               static_cast<long long>(referencePixels.size()), static_cast<long long>(referenceGpuBytes));
  std::fflush(stderr);
  if (!check(!referencePixels.isEmpty(), "the reference fixture has visible model-colored pixels in the real framebuffer")) return false;
  const QStringList intensiveStages{QStringLiteral("train"), QStringLiteral("mesh"), QStringLiteral("texture")};
  for (int stageIndex = 0; stageIndex < intensiveStages.size(); ++stageIndex) {
    const auto &stage = intensiveStages[stageIndex];
    viewport.beginProcessingPreview();
    viewport.setProcessingStage(stage, 0, 100, 0);
    std::fprintf(stderr, "Training priority first-frame stage: %s\n", stage.toUtf8().constData());
    std::fflush(stderr);
    if (!check(waitFor([&] {
          viewport.grabFramebuffer();
          return viewport.resourceBudgetStatus().managedGpuBytes < referenceGpuBytes;
        }), "a GPU-intensive stage yields reference display storage before the first task frame")) return false;
    const QImage waitingFrame = viewport.grabFramebuffer();
    int retainedModelPixels = 0;
    if (waitingFrame.size() == referenceFrame.size()) {
      for (const QPoint &point : referencePixels) {
        const QRgb before = referenceFrame.pixel(point);
        const QRgb waiting = waitingFrame.pixel(point);
        if (std::abs(qRed(before) - qRed(waiting)) <= 25 &&
            std::abs(qGreen(before) - qGreen(waiting)) <= 25 &&
            std::abs(qBlue(before) - qBlue(waiting)) <= 25) ++retainedModelPixels;
      }
    }
    if (!check(!viewport.hasProcessingPreview() && viewport.sceneObjectCount() == 2 &&
               viewport.activeSceneId() == first.id && retainedModelPixels * 2 >= referencePixels.size(),
               "waiting for the first task frame keeps a static visible reference image, not a blank viewport")) return false;
    viewport.setProcessingPreviewVisible(false);
    if (!check(waitFor([&] {
          viewport.grabFramebuffer();
          return viewport.resourceBudgetStatus().managedGpuBytes >= referenceGpuBytes &&
              viewport.renderedPointCount() == 15 && viewport.scenePath() == first.path;
        }), "explicitly viewing the reference restores its GPU storage before any task frame")) return false;
    if (!check(viewport.modelTranslation() == first.translation &&
               viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale &&
               viewport.viewTarget() == referenceTarget && viewport.viewDistance() == referenceDistance &&
               viewport.viewOrbitAngles() == referenceAngles,
               "first-frame waiting and reference restoration preserve object transforms and camera")) return false;
    viewport.finishProcessingPreview(stageIndex == 2, stageIndex == 0, stageIndex == 1);
    if (!check(viewport.hasEditableScene() && viewport.scenePath() == first.path &&
               viewport.sceneObjectCount() == 2 && viewport.modelTranslation() == first.translation &&
               viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale,
               "success, cancellation and pause without a task snapshot leave the original model usable")) return false;
  }
  viewport.beginProcessingPreview();
  viewport.setPreviewScene(preview, 30);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == preview && viewport.renderedPointCount() == 30;
      }), "new task becomes visible without a trainer or external process")) return false;
  if (!check(viewport.hasProcessingPreview() && viewport.processingPreviewVisible() &&
             viewport.sceneObjectCount() == 2 && viewport.activeSceneId() == first.id,
             "the task display is separate from persisted reference identities")) return false;
  if (!check(viewport.resourceBudgetStatus().managedGpuBytes < referenceGpuBytes,
             "showing the task releases reference display GPU storage")) return false;
  const QPointF center = viewport.rect().center();
  QWheelEvent zoom(center, viewport.mapToGlobal(center.toPoint()), {}, {0, -120},
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&viewport, &zoom);
  const auto previewTarget = viewport.viewTarget();
  const auto previewDistance = viewport.viewDistance();
  const auto previewAngles = viewport.viewOrbitAngles();
  viewport.setProcessingPreviewVisible(false);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == first.path && viewport.renderedPointCount() == 15 &&
            viewport.resourceBudgetStatus().managedGpuBytes >= referenceGpuBytes;
      }), "viewing references restores their real display buffers")) return false;
  if (!check(!viewport.processingPreviewVisible() && viewport.hasProcessingPreview() &&
             viewport.modelTranslation() == first.translation &&
             viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale &&
             viewport.viewTarget() == referenceTarget && viewport.viewDistance() == referenceDistance &&
             viewport.viewOrbitAngles() == referenceAngles,
             "reference view and transforms are not replaced by task navigation")) return false;
  viewport.setProcessingPreviewVisible(true);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == preview && viewport.renderedPointCount() == 30;
      }), "the retained task preview can be shown again")) return false;
  if (!check(viewport.viewTarget() == previewTarget && viewport.viewDistance() == previewDistance &&
             viewport.viewOrbitAngles() == previewAngles,
             "task navigation is remembered independently from the reference view")) return false;
  auto *previewAction = viewport.window()->findChild<QAction *>(QStringLiteral("taskPreviewAction"));
  if (!check(previewAction && previewAction->isEnabled() && previewAction->isChecked(),
             "the real task-preview action reflects the visible task layer")) return false;
  const QString initialLanguage = AppLanguage::current();
  QSet<QString> actionLabels;
  QSet<QString> actionTooltips;
  for (const auto &language : AppLanguage::supported()) {
    AppLanguage::apply(language, false);
    actionLabels.insert(previewAction->text());
    actionTooltips.insert(previewAction->toolTip());
    if (!check(previewAction->text() == QCoreApplication::translate("Workbench", "任务预览") &&
               previewAction->isEnabled() && previewAction->isChecked() &&
               viewport.scenePath() == preview && viewport.activeSceneId() == first.id &&
               viewport.sceneObjectCount() == 2 && viewport.viewTarget() == previewTarget &&
               viewport.viewDistance() == previewDistance && viewport.viewOrbitAngles() == previewAngles,
               "language selection refreshes the real action without rebuilding the task or reference views")) return false;
  }
  if (!check(actionLabels.size() == 3 && actionTooltips.size() == 3,
             "the task-preview action and help are distinct in Chinese, English and Japanese")) return false;
  previewAction->trigger();
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return !previewAction->isChecked() && !viewport.processingPreviewVisible() &&
            viewport.scenePath() == first.path && viewport.renderedPointCount() == 15;
      }), "triggering the real action switches to the original scene")) return false;
  if (!check(viewport.modelTranslation() == first.translation &&
             viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale,
             "the user-facing switch keeps the original model transform")) return false;
  previewAction->trigger();
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return previewAction->isChecked() && viewport.processingPreviewVisible() &&
            viewport.scenePath() == preview && viewport.renderedPointCount() == 30;
      }), "triggering the real action again returns to the task preview")) return false;
  AppLanguage::apply(initialLanguage, false);
  viewport.setProcessingStage(QStringLiteral("train"), 1, 30000, 0);
  int presentedFrames = 0;
  const auto frameConnection = QObject::connect(&viewport, &NativeViewport::frameMetricsChanged,
      &viewport, [&] { ++presentedFrames; });
  QEventLoop frameWindow;
  QTimer::singleShot(1200, &frameWindow, &QEventLoop::quit);
  frameWindow.exec();
  QObject::disconnect(frameConnection);
  std::fprintf(stderr, "Training live frames: %d completed frames in 1200 ms\n", presentedFrames);
  std::fflush(stderr);
  if (!check(presentedFrames > 0, "the visible task continues presenting real completed frames")) return false;
  if (!check(presentedFrames <= 45,
             "training observation is capped near 30 FPS instead of competing at unlimited refresh")) return false;
  const QString intermediatePreview = writePoints(directory, QStringLiteral("task-preview-intermediate"), 35);
  const QString latestPreview = writePoints(directory, QStringLiteral("task-preview-latest"), 40);
  auto *pausePreviewAction = viewport.window()->findChild<QAction *>(QStringLiteral("pausePreviewAction"));
  if (!check(pausePreviewAction && pausePreviewAction->isEnabled() &&
             pausePreviewAction->isCheckable() && !pausePreviewAction->isChecked(),
             "the real pause-preview action is available independently of training pause")) return false;
  const QImage beforePausedFrame = viewport.grabFramebuffer();
  QVector<QPoint> taskPixels;
  for (int y = beforePausedFrame.height() / 4; y < beforePausedFrame.height() * 3 / 4; ++y) {
    for (int x = beforePausedFrame.width() / 4; x < beforePausedFrame.width() * 3 / 4; ++x) {
      const QRgb pixel = beforePausedFrame.pixel(x, y);
      if (qRed(pixel) > 120 && qBlue(pixel) > 100 && qGreen(pixel) < 100 &&
          qRed(pixel) - qGreen(pixel) > 60 && qBlue(pixel) - qGreen(pixel) > 40)
        taskPixels.append({x, y});
    }
  }
  if (!check(!taskPixels.isEmpty(), "the task fixture has real visible geometry before preview pause")) return false;
  const auto pausedTarget = viewport.viewTarget();
  const auto pausedDistance = viewport.viewDistance();
  const auto pausedAngles = viewport.viewOrbitAngles();
  int pausedReads = 0;
  const auto pausedReadConnection = QObject::connect(&viewport, &NativeViewport::sceneLoadStarted,
      &viewport, [&](const QString &) { ++pausedReads; });
  pausePreviewAction->trigger();
  QSet<QString> pausedActionLabels;
  QSet<QString> pausedActionTooltips;
  QSet<QString> pausedOverlayLabels;
  for (const auto &language : AppLanguage::supported()) {
    AppLanguage::apply(language, false);
    pausedActionLabels.insert(pausePreviewAction->text());
    pausedActionTooltips.insert(pausePreviewAction->toolTip());
    pausedOverlayLabels.insert(viewport.processingPreviewDetail());
    if (!check(pausePreviewAction->isChecked() && viewport.processingPreviewPaused() &&
               pausePreviewAction->text() == QCoreApplication::translate("Workbench", "暂停预览") &&
               viewport.scenePath() == preview && viewport.sceneObjectCount() == 2 &&
               viewport.viewTarget() == pausedTarget && viewport.viewDistance() == pausedDistance &&
               viewport.viewOrbitAngles() == pausedAngles,
               "live language changes refresh the real pause action without resuming observation or changing its camera")) return false;
  }
  if (!check(pausedActionLabels.size() == 3 && pausedActionTooltips.size() == 3 &&
             pausedOverlayLabels.size() == 3,
             "pause-preview captions, help and status are localized in all three languages")) return false;
  AppLanguage::apply(initialLanguage, false);
  viewport.setPreviewScene(intermediatePreview, 35);
  viewport.setProcessingStage(QStringLiteral("train"), 2, 30000, 0);
  viewport.setPreviewScene(latestPreview, 40);
  viewport.setPreviewScene(directory.filePath(QStringLiteral("expired-preview.ply")), 50);
  QEventLoop pausedWindow;
  QTimer::singleShot(180, &pausedWindow, &QEventLoop::quit);
  pausedWindow.exec();
  const QImage pausedFrame = viewport.grabFramebuffer();
  int retainedTaskPixels = 0;
  if (pausedFrame.size() == beforePausedFrame.size()) {
    for (const QPoint &point : taskPixels) {
      const QRgb before = beforePausedFrame.pixel(point);
      const QRgb paused = pausedFrame.pixel(point);
      if (std::abs(qRed(before) - qRed(paused)) <= 25 &&
          std::abs(qGreen(before) - qGreen(paused)) <= 25 &&
          std::abs(qBlue(before) - qBlue(paused)) <= 25) ++retainedTaskPixels;
    }
  }
  QObject::disconnect(pausedReadConnection);
  if (!check(pausePreviewAction->isChecked() && viewport.processingPreviewPaused() && pausedReads == 0 &&
             viewport.scenePath() == preview && viewport.renderedPointCount() == 30 &&
             retainedTaskPixels * 10 >= taskPixels.size() * 9 &&
             viewport.processingPreviewLabel().contains(QStringLiteral("2 / 30000")) &&
             viewport.viewTarget() == pausedTarget && viewport.viewDistance() == pausedDistance &&
             viewport.viewOrbitAngles() == pausedAngles,
             "pausing preview freezes the real task geometry without parsing newer snapshots or stopping training progress")) return false;
  pausePreviewAction->trigger();
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return !pausePreviewAction->isChecked() && !viewport.processingPreviewPaused() && viewport.scenePath() == latestPreview &&
            viewport.renderedPointCount() == 40;
      }) && viewport.viewTarget() == pausedTarget && viewport.viewDistance() == pausedDistance &&
          viewport.viewOrbitAngles() == pausedAngles && viewport.sceneObjectCount() == 2,
             "resuming observation skips superseded and expired candidates and shows the latest real frame with the same camera")) return false;
  viewport.finishProcessingPreview(false, true);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == first.path && viewport.renderedPointCount() == 15;
      }), "cancelling a task restores the original active model, not its last preview")) return false;
  if (!check(viewport.sceneObjectCount() == 2 && viewport.activeSceneId() == first.id &&
             viewport.modelTranslation() == first.translation &&
             viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale,
             "the original active identity and transform survive cancellation")) return false;
  previewAction->trigger();
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return previewAction->isChecked() && viewport.scenePath() == latestPreview &&
            viewport.renderedPointCount() == 40 && !viewport.hasEditableScene();
      }), "a completed task observation remains available from the real action")) return false;
  const auto terminalPreviewTarget = viewport.viewTarget();
  const auto terminalPreviewDistance = viewport.viewDistance();
  const auto terminalPreviewAngles = viewport.viewOrbitAngles();
  viewport.setSceneObjects({first, second}, first.id);
  if (!check(viewport.scenePath() == latestPreview && viewport.processingPreviewVisible() &&
             previewAction->isChecked() && viewport.sceneObjectCount() == 2 &&
             viewport.activeSceneId() == first.id && !viewport.hasEditableScene() &&
             viewport.viewTarget() == terminalPreviewTarget &&
             viewport.viewDistance() == terminalPreviewDistance &&
             viewport.viewOrbitAngles() == terminalPreviewAngles,
             "synchronizing the unchanged project cannot interrupt a manually selected terminal preview")) return false;
  viewport.setProcessingPreviewPaused(true);
  viewport.clearProcessingPreview();
  if (!check(!viewport.hasProcessingPreview() && !viewport.processingPreviewVisible() &&
             !viewport.processingPreviewPaused() &&
             !pausePreviewAction->isEnabled() && !pausePreviewAction->isChecked() &&
             !previewAction->isEnabled() && !previewAction->isChecked() &&
             viewport.processingPreviewLabel().isEmpty() && viewport.processingPreviewDetail().isEmpty(),
             "an explicit project-context reset clears only the transient task view and its user action")) return false;
  // Another project may link exactly the same models and retain their stable
  // IDs. Explicit context reset must still remove the old task observation.
  viewport.setSceneObjects({first, second}, first.id);
  if (!check(waitFor([&] {
        viewport.grabFramebuffer();
        return viewport.scenePath() == first.path && viewport.renderedPointCount() == 15 &&
            viewport.resourceBudgetStatus().managedGpuBytes >= referenceGpuBytes;
      }), "the new context shows the original references even when their IDs and paths are unchanged")) return false;
  if (!check(viewport.sceneObjectCount() == 2 && viewport.activeSceneId() == first.id &&
             viewport.hasEditableScene() && viewport.modelTranslation() == first.translation &&
             viewport.modelRotation() == first.rotation && viewport.modelScale() == first.scale &&
             viewport.viewTarget() == referenceTarget && viewport.viewDistance() == referenceDistance &&
             viewport.viewOrbitAngles() == referenceAngles && !viewport.hasProcessingPreview(),
             "clearing task context preserves editable reference identities, transforms and camera")) return false;
  if (!check(viewport.activateSceneObject(second.id) && viewport.scenePath() == second.path &&
             viewport.modelTranslation() == second.translation &&
             viewport.modelRotation() == second.rotation && viewport.modelScale() == second.scale,
             "the other reference model and transform also survive cancellation")) return false;
  viewport.setSceneObjects({}, {});
  std::fprintf(stderr, "TRAINING_ISOLATION PASS: separate task/reference views, GPU surrender and cancellation retention\n");
  std::fflush(stderr);
  qInfo() << "TRAINING_ISOLATION PASS: separate task/reference views, GPU surrender and cancellation retention";
  return true;
}

bool runLichtFeldPreviewSmokeTest(NativeViewport &viewport) {
  QTemporaryDir directory;
  const auto check = [](bool okay, const char *message) {
    std::fprintf(stderr, "LichtFeld preview %s: %s\n", okay ? "PASS" : "FAIL", message);
    std::fflush(stderr);
    return okay;
  };
  if (!directory.isValid()) return false;
  auto *action = viewport.window()->findChild<QAction *>(QStringLiteral("lichtFeldTrainingPreviewAction"));
  if (!check(action && action->isCheckable() && !action->isChecked(),
             "the real window exposes a default-off session preview choice")) return false;
  const auto fixture = [&](const QString &name, bool surfel) -> QString {
    QFile file(directory.filePath(name + QStringLiteral(".ply")));
    QByteArray bytes = "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
        "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\nproperty float opacity\n"
        "property float scale_0\nproperty float scale_1\n";
    if (!surfel) bytes += "property float scale_2\n";
    bytes += "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\nend_header\n";
    bytes += surfel ? "0 0 0 2 -2 -2 6 4 4 1 0 0 0\n" : "0 0 0 2 -2 -2 6 4 4 4 1 0 0 0\n";
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    return file.fileName();
  };
  const QString gaussians = fixture(QStringLiteral("owned-3dgs-observation"), false);
  const QString surfels = fixture(QStringLiteral("owned-2dgs-observation"), true);
  const QString referencePath = fixture(QStringLiteral("owned-3dgs-reference"), false);
  if (gaussians.isEmpty() || surfels.isEmpty() || referencePath.isEmpty()) return false;
  const auto reference = [&] {
    SceneObject model;
    model.id = QStringLiteral("owned-reference-3dgs");
    model.path = referencePath;
    model.vertexCount = 1;
    model.translation = {0.125F, -0.25F, 0.5F};
    return model;
  }();
  // Only the disposable runner window is resized; the managed application and
  // user fixtures are never used. The 340-device-pixel tail must be on screen.
  viewport.window()->resize(1520, 920);
  auto grid = viewport.viewportGridSettings();
  grid.gridVisible = false;
  grid.axesVisible = false;
  if (!viewport.setViewportGridSettings(grid)) return false;
  viewport.setShowObservationTrackball(false);
  const auto frameReady = [&](const QString &path) {
    return waitFor([&] {
      viewport.grabFramebuffer();
      return viewport.scenePath() == path && viewport.gaussianRenderingAvailable() &&
          viewport.renderedPointCount() == 1;
    });
  };
  const auto redExcess = [](const QImage &frame, int offset) {
    const QPoint center(frame.width() / 2 + offset, frame.height() / 2);
    if (!frame.rect().adjusted(5, 5, -5, -5).contains(center)) return -1000.0;
    double sum = 0.0;
    for (int y = center.y() - 5; y <= center.y() + 5; ++y)
      for (int x = center.x() - 5; x <= center.x() + 5; ++x) {
        const QRgb color = frame.pixel(x, y);
        sum += qRed(color) - std::max(qGreen(color), qBlue(color));
      }
    return sum / 121.0;
  };
  const auto translated = [](const char *source) { return QCoreApplication::translate("Workbench", source); };
  viewport.setSceneObjects({reference}, reference.id);
  if (!check(frameReady(referencePath), "the reference Gaussian loads in the real native renderer")) return false;
  viewport.beginProcessingPreview();
  viewport.setProcessingStage(QStringLiteral("train"), 7, 80, 9);
  viewport.setPreviewScene(gaussians, 1);
  if (!check(frameReady(gaussians) && viewport.hasProcessingPreview(),
             "the independent task buffer is ready")) return false;
  viewport.resetCamera();
  viewport.setRenderMode(NativeViewport::RenderMode::Gaussians);
  const auto viewTarget = viewport.viewTarget();
  const auto viewDistance = viewport.viewDistance();
  const QImage legacy = viewport.grabFramebuffer();
  action->trigger();
  const QImage ported = viewport.grabFramebuffer();
  const QString evidenceRoot = qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR");
  if (!evidenceRoot.isEmpty()) {
    const QDir evidence(evidenceRoot);
    if (!(QDir().mkpath(evidenceRoot) &&
          legacy.save(evidence.filePath(QStringLiteral("lichtfeld-legacy.png"))) &&
          ported.save(evidence.filePath(QStringLiteral("lichtfeld-experimental.png"))))) return false;
  }
  const double oldCenter = redExcess(legacy, 0), newCenter = redExcess(ported, 0);
  const double oldTail = redExcess(legacy, 340), newTail = redExcess(ported, 340);
  std::fprintf(stderr, "LichtFeld preview device-pixel samples: size %dx%d, centre %.2f -> %.2f, tail %.2f -> %.2f\n",
               ported.width(), ported.height(), oldCenter, newCenter, oldTail, newTail);
  if (!check(action->isChecked() && legacy.size() == ported.size() &&
                 oldCenter > 120 && std::abs(oldCenter - newCenter) <= 3 &&
                 oldTail > 40 && newTail > 5 && oldTail - newTail >= 25 &&
                 viewport.viewTarget() == viewTarget && viewport.viewDistance() == viewDistance,
             "the same active 3DGS frame has a bounded opacity tail without moving its centre or camera")) return false;

  const QString priorLanguage = AppLanguage::current();
  QSet<QString> captions, explanations;
  for (const auto &language : AppLanguage::supported()) {
    if (!AppLanguage::apply(language, false)) return false;
    captions.insert(action->text());
    explanations.insert(viewport.processingPreviewDetail());
    if (!check(action->isChecked() && viewport.scenePath() == gaussians &&
                   viewport.viewTarget() == viewTarget && viewport.viewDistance() == viewDistance &&
                   viewport.processingPreviewDetail().contains(translated("LichtFeld 投影（实验） · 周期快照")),
               "the localized visible route is the actual file-observation renderer and keeps its view")) return false;
  }
  AppLanguage::apply(priorLanguage, false);
  if (!check(captions.size() == 3 && explanations.size() == 3, "three different locales render immediately")) return false;

  auto *pause = viewport.window()->findChild<QAction *>(QStringLiteral("pausePreviewAction"));
  if (!pause || !pause->isEnabled()) return false;
  pause->trigger();
  viewport.setProcessingStage(QStringLiteral("train"), 8, 80, 10);
  viewport.setPreviewScene(surfels, 1);
  if (!check(viewport.processingPreviewPaused() && viewport.scenePath() == gaussians &&
                 viewport.processingPreviewLabel().contains(QStringLiteral("8 / 80")),
             "pausing the picture preserves the old observation while progress advances")) return false;
  pause->trigger();
  if (!check(frameReady(surfels) && viewport.sourceHasSurfelAttributes() && action->isChecked() &&
                 viewport.processingPreviewDetail().contains(translated("原生 2DGS 表面元投影 · 周期快照")),
             "resume uses the newest real two-scale snapshot and keeps the native surfel renderer")) return false;
  const double surfelOn = redExcess(viewport.grabFramebuffer(), 0);
  action->trigger();
  const double surfelOff = redExcess(viewport.grabFramebuffer(), 0);
  if (!check(surfelOn > 100 && std::abs(surfelOn - surfelOff) <= 2 && !action->isChecked(),
             "switching the experiment on/off does not affect surfel pixels")) return false;
  action->trigger();
  viewport.finishProcessingPreview(false, true);
  if (!check(frameReady(referencePath) && viewport.activeSceneId() == reference.id &&
                 viewport.modelTranslation() == reference.translation && viewport.hasEditableScene(),
             "cancellation restores the editable reference with its original ID and transform")) return false;
  const double referenceOn = redExcess(viewport.grabFramebuffer(), 340);
  action->trigger();
  const double referenceOff = redExcess(viewport.grabFramebuffer(), 340);
  if (!check(referenceOn > 20 && std::abs(referenceOn - referenceOff) <= 2,
             "the experiment does not change the reference Gaussian renderer")) return false;
  viewport.setSceneObjects({}, {});
  return true;
}
}
