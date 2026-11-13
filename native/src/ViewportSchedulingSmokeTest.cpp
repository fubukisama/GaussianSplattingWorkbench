#include "ViewportSchedulingSmokeTest.h"
#include "MainWindow.h"
#include "NativeViewport.h"
#include "WorkspaceDocument.h"
#include "ViewportStatusControlsSmokeTest.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QTemporaryDir>
#include <QVariant>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <functional>

namespace gsw {
namespace {
bool waitUntil(const std::function<bool()> &predicate, int timeout = 7000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QEventLoop loop;
    QTimer::singleShot(20, &loop, &QEventLoop::quit);
    loop.exec();
  }
  return predicate();
}

void dispatchFor(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

bool fixture(const QString &path, int kind) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  const bool mesh = kind == 1;
  const bool gaussian = kind >= 2;
  QByteArray ply = "ply\nformat ascii 1.0\nelement vertex ";
  ply += mesh ? "4\n" : "441\n";
  ply += "property float x\nproperty float y\nproperty float z\n";
  if (gaussian) {
    ply += "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\nproperty float opacity\nproperty float scale_0\nproperty float scale_1\n";
    if (kind == 2) ply += "property float scale_2\n";
    ply += "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\n";
  } else ply += "property uchar red\nproperty uchar green\nproperty uchar blue\n";
  if (mesh) ply += "element face 2\nproperty list uchar int vertex_indices\n";
  ply += "end_header\n";
  const auto vertex = [&](float x, float y) {
    QByteArray data = QString("%1 %2 0 ").arg(x).arg(y).toUtf8();
    if (!gaussian) data += "225 100 30\n";
    else if (kind == 2) data += "1 -1 -1 3 -3.5 -3.5 -3.5 1 0 0 0\n";
    else data += "1 -1 -1 3 -3.5 -3.5 1 0 0 0\n";
    return data;
  };
  if (mesh) {
    ply += vertex(-1, -1) + vertex(1, -1) + vertex(-1, 1) + vertex(1, 1);
    ply += "3 0 1 2\n3 2 1 3\n";
  } else for (int y = -10; y <= 10; ++y)
    for (int x = -10; x <= 10; ++x) ply += vertex(x / 10.0F, y / 10.0F);
  return file.write(ply) == ply.size();
}

bool hasZeroIntervalTimer(const NativeViewport &viewport) {
  for (const auto *timer : viewport.findChildren<QTimer *>())
    if (timer->isActive() && timer->interval() == 0) return true;
  return false;
}

bool sharedPresentationSemantics(MainWindow &window, const NativeViewport &viewport) {
  QLabel *renderer = nullptr;
  for (auto *label : window.findChildren<QLabel *>())
    if (label->property("gswStatusRole").toString() == QStringLiteral("renderer")) renderer = label;
  auto *hud = viewport.findChild<QLabel *>(QStringLiteral("viewportStatusLabel"));
  const QString canonical = viewport.viewportStatusText();
  const QString performance = viewport.viewportPerformanceText();
  // The HUD may intentionally elide its visible caption on narrow windows.
  // Its actual accessible label retains the same full presentation as the
  // main status; neither surface computes a separate stale FPS string.
  const bool good = renderer && renderer->isVisible() && hud && hud->isVisible() &&
      !hud->text().isEmpty() && !canonical.isEmpty() && !performance.isEmpty() &&
      renderer->text() == canonical && canonical.contains(performance) &&
      hud->accessibleName() == canonical;
  if (!good)
    qCritical() << "VIEWPORT_PERFORMANCE_STATE FAIL: main status and actual HUD share presentation semantics"
                << "main" << (renderer ? renderer->text() : QString())
                << "HUD visible/accessibility" << (hud ? hud->text() : QString())
                << (hud ? hud->accessibleName() : QString())
                << "canonical/performance" << canonical << performance;
  return good;
}

bool idlePerformanceLabelRegression(MainWindow &window, NativeViewport &viewport, int kind) {
  const auto check = [](bool ok, const char *message) {
    if (!ok) qCritical() << "VIEWPORT_PERFORMANCE_STATE FAIL:" << message;
    return ok;
  };
  QLabel *renderer = nullptr;
  for (auto *label : window.findChildren<QLabel *>())
    if (label->property("gswStatusRole").toString() == QStringLiteral("renderer")) renderer = label;
  auto *showFps = window.findChild<QAction *>(QStringLiteral("showViewportFrameRateAction"));
  auto *uploadTimer = viewport.findChild<QTimer *>(QStringLiteral("viewportUploadRefreshTimer"));
  auto *pollTimer = viewport.findChild<QTimer *>(QStringLiteral("trainingPreviewPollTimer"));
  if (!check(renderer && renderer->isVisible() && renderer->width() > 0 &&
      showFps && showFps->isChecked() && uploadTimer && pollTimer,
      "actual user-visible renderer performance label exists")) return false;
  quint64 compositions = 0, freshPresentations = 0, metricUpdates = 0;
  quint64 uploadWakeups = 0, pollWakeups = 0;
  quint64 lastRenderedSerial = viewport.renderedFrameSerial();
  double lastFps = 0.0, lastMilliseconds = 0.0;
  const auto presentation = QObject::connect(&viewport, &QOpenGLWidget::frameSwapped, &viewport, [&] {
    ++compositions;
    const quint64 serial = viewport.renderedFrameSerial();
    if (serial != lastRenderedSerial) { lastRenderedSerial = serial; ++freshPresentations; }
  });
  const auto metrics = QObject::connect(&viewport, &NativeViewport::frameMetricsChanged, &viewport,
      [&](double fps, double milliseconds) { ++metricUpdates; lastFps = fps; lastMilliseconds = milliseconds; });
  const auto upload = QObject::connect(uploadTimer, &QTimer::timeout, &viewport, [&] { ++uploadWakeups; });
  const auto poll = QObject::connect(pollTimer, &QTimer::timeout, &viewport, [&] { ++pollWakeups; });
  const auto disconnect = qScopeGuard([&] {
    QObject::disconnect(presentation);
    QObject::disconnect(metrics);
    QObject::disconnect(upload);
    QObject::disconnect(poll);
    showFps->setChecked(true);
  });
  const QPoint start = viewport.rect().center();
  const QString idle = QCoreApplication::translate("Workbench", "空闲");
  const QString evidence = QDir(qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR", QDir::tempPath())).filePath(
      QStringLiteral("viewport-performance-state-%1/mode-%2")
          .arg(qApp->property("gswUiTheme").toString()).arg(kind));
  if (!check(QDir().mkpath(evidence) && window.screen(), "owned evidence directory and screen")) return false;
  const auto capture = [&](const QString &name) {
    return window.screen()->grabWindow(window.winId()).save(QDir(evidence).filePath(name));
  };
  const auto mouse = [&](QEvent::Type type, const QPoint &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(point), QPointF(viewport.mapToGlobal(point)), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &event);
  };
  const auto counts = [&] {
    return std::array<quint64, 8>{viewport.renderedFrameSerial(), freshPresentations, metricUpdates,
        viewport.gaussianAttributeUploads(), viewport.gaussianOrderUploads(), viewport.gaussianShUploads(),
        uploadWakeups, pollWakeups};
  };
  // Allow each real input frame to present before sending the next, so the
  // comparison is not a measurement of runner-dependent event coalescing.
  const auto pan = [&] {
    mouse(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
    QPoint end = start;
    const auto release = qScopeGuard([&] {
      mouse(QEvent::MouseButtonRelease, end, Qt::MiddleButton, Qt::NoButton);
    });
    for (int step = 1; step <= 8; ++step) {
      const auto previous = metricUpdates;
      end = start + QPoint(step * 2, step % 2);
      mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::MiddleButton);
      if (!waitUntil([&] { return metricUpdates > previous; }, 1400)) return false;
    }
    return true;
  };
  const auto delta = [&](const auto &before) {
    auto after = counts();
    for (std::size_t i = 0; i < after.size(); ++i) after[i] -= before[i];
    return after;
  };
  viewport.resetCamera();
  dispatchFor(80);
  if (!check(waitUntil([&] { return viewport.viewportPerformanceText() == idle; }, 2500),
      "baseline camera settles before each measured input sequence")) return false;
  const auto targetBefore = viewport.viewTarget();
  const auto beforeVisible = counts();
  if (!check(pan(), "all eight real pan inputs present")) return false;
  const auto visibleWork = delta(beforeVisible);
  const QString activeText = viewport.viewportPerformanceText();
  const QRegularExpression fpsPattern(QStringLiteral("^[0-9]+\\.[0-9]{2} FPS \\([0-9]+\\.[0-9]{2} ms\\)$"));
  const QString expected = QStringLiteral("%1 FPS (%2 ms)")
      .arg(lastFps, 0, 'f', 2).arg(lastMilliseconds, 0, 'f', 2);
  if (!check(viewport.viewTarget() != targetBefore && visibleWork[1] >= 8 &&
      visibleWork[1] == visibleWork[2] && viewport.presentedFrameIntervalCount() > 0 &&
      std::isfinite(lastFps) && lastFps > 0.0 && std::isfinite(lastMilliseconds) && lastMilliseconds > 0.0 &&
      fpsPattern.match(activeText).hasMatch() && activeText == expected &&
      viewport.viewportDetailsText().contains(activeText),
      "fresh presented frames each emit once and the same two-decimal snapshot reaches the details")) return false;
  if (!sharedPresentationSemantics(window, viewport) ||
      !check(capture(QStringLiteral("after-real-pan.png")), "capture actual active viewport without repainting it")) return false;
  const auto targetAfterPan = viewport.viewTarget();
  // The idle verdict must arise from the real event/presentation loop. Do not
  // call update(), grabFramebuffer(), or repeatedly capture while waiting:
  // those would manufacture viewport work and conceal the stale-live-FPS bug.
  const bool becameIdle = waitUntil([&] { return renderer->text().contains(idle); }, 2500);
  const QString stoppedText = renderer->text();
  qInfo() << "VIEWPORT_PERFORMANCE_STATE active/stopped label" << activeText << stoppedText
          << "fresh presentations/metric updates" << freshPresentations << metricUpdates;
  if (!becameIdle || !renderer->isVisible()) {
    capture(QStringLiteral("after-input-stops.png"));
    return check(false, "after input stops the visible renderer label says idle instead of retaining live numeric FPS");
  }
  const auto idlePresentations = freshPresentations;
  dispatchFor(300);
  const bool remainsIdle = renderer->text().contains(idle) && viewport.viewTarget() == targetAfterPan &&
      freshPresentations <= idlePresentations + 1 && !hasZeroIntervalTimer(viewport);
  // Capture after the verdict, so evidence collection cannot itself create
  // work in the interval used to reject a counter-only presentation loop.
  if (!check(capture(QStringLiteral("after-input-stops.png")), "capture real idle viewport")) return false;
  if (!check(remainsIdle,
      "idle state remains visible without a counter-only redraw loop or camera movement")) return false;
  if (!sharedPresentationSemantics(window, viewport)) return false;

  // Exercise actual Qt top-level composition, not an artificial emission of
  // frameSwapped. The status widget is outside the viewport's GL FBO.
  const auto compositionBefore = compositions;
  const auto labelOnly = counts();
  const auto labelSamples = viewport.presentedFrameIntervalCount();
  renderer->update();
  if (!check(waitUntil([&] { return compositions > compositionBefore; }, 1500) &&
      counts() == labelOnly && viewport.presentedFrameIntervalCount() == labelSamples &&
      viewport.viewportPerformanceText() == idle,
      "recomposing only the status label does not render or count a viewport frame")) return false;

  viewport.resetCamera();
  dispatchFor(80);
  if (!check(waitUntil([&] { return viewport.viewportPerformanceText() == idle; }, 2500) &&
      viewport.viewTarget() == targetBefore, "same fixture and camera before hiding FPS")) return false;
  const auto beforeToggle = counts();
  const auto beforeToggleSamples = viewport.presentedFrameIntervalCount();
  showFps->setChecked(false);
  dispatchFor(80);
  if (!check(viewport.viewportPerformanceText().isEmpty() && counts() == beforeToggle &&
      viewport.presentedFrameIntervalCount() == beforeToggleSamples,
      "hiding FPS does not redraw, upload, poll, or reset the samples")) return false;
  const auto beforeHidden = counts();
  if (!check(pan(), "same real pan still presents with the display disabled")) return false;
  const auto hiddenWork = delta(beforeHidden);
  const auto logWork = [&](const char *mode, const auto &work) {
    qInfo() << "VIEWPORT_FPS_WORK" << kind << mode
            << "render/present/metric" << work[0] << work[1] << work[2]
            << "attribute/order/SH" << work[3] << work[4] << work[5]
            << "uploadWake/pollWake" << work[6] << work[7]
            << "intervals" << viewport.presentedFrameIntervalCount();
  };
  logWork("shown", visibleWork);
  logWork("hidden", hiddenWork);
  if (!check(visibleWork == hiddenWork && hiddenWork[6] == 0 && hiddenWork[7] == 0 &&
      viewport.viewTarget() == targetAfterPan && !hasZeroIntervalTimer(viewport) &&
      viewport.viewportPerformanceText().isEmpty() && !renderer->text().contains(QStringLiteral("FPS")) &&
      !viewport.viewportDetailsText().contains(QCoreApplication::translate("Workbench", "最近测得帧率：%1 FPS").arg(QString{})),
      "same camera script and visible/hidden FPS have identical GL, presentation, upload and polling work")) return false;
  const auto hiddenSamples = viewport.presentedFrameIntervalCount();
  const auto hiddenCounts = counts();
  showFps->setChecked(true);
  if (!check(fpsPattern.match(viewport.viewportPerformanceText()).hasMatch() &&
      viewport.presentedFrameIntervalCount() == hiddenSamples && counts() == hiddenCounts,
      "restoring FPS immediately uses the live samples without requesting another frame")) return false;
  if (!check(waitUntil([&] { return viewport.viewportPerformanceText() == idle; }, 2500),
      "restored metrics return naturally to idle")) return false;
  return true;
}
}

bool runViewportSchedulingSmokeTest(MainWindow &window) {
  auto *viewport = window.findChild<NativeViewport *>();
  auto *document = window.findChild<WorkspaceDocument *>();
  QTemporaryDir temporary;
  const auto check = [](bool good, const char *message) {
    if (!good) qCritical() << "VIEWPORT_SCHEDULING FAIL:" << message;
    return good;
  };
  if (!check(viewport && document && temporary.isValid(), "owned viewport and temporary workspace")) return false;
  quint64 timerCallbacks = 0;
  quint64 presentedFrames = 0;
  quint64 freshPresentations = 0;
  quint64 lastPresentedRenderSerial = viewport->renderedFrameSerial();
  double latestFps = 0.0, latestMilliseconds = 0.0;
  QList<QMetaObject::Connection> timerConnections;
  for (auto *timer : viewport->findChildren<QTimer *>())
    if (timer->interval() == 0)
      timerConnections.append(QObject::connect(timer, &QTimer::timeout, viewport, [&] { ++timerCallbacks; }));
  const auto framesConnection = QObject::connect(viewport, &NativeViewport::frameMetricsChanged,
      viewport, [&](double fps, double milliseconds) {
        ++presentedFrames;
        latestFps = fps;
        latestMilliseconds = milliseconds;
      });
  const auto presentationConnection = QObject::connect(viewport, &QOpenGLWidget::frameSwapped,
      viewport, [&] {
        const quint64 serial = viewport->renderedFrameSerial();
        if (serial != lastPresentedRenderSerial) {
          lastPresentedRenderSerial = serial;
          ++freshPresentations;
        }
      });
  const bool originallyMaximized = window.isMaximized();
  QString error;
  bool passed = true;
  for (int kind = 0; kind < 4 && passed; ++kind) {
    const QString root = QDir(temporary.path()).filePath(QString("workspace-%1").arg(kind));
    const QString path = QDir(temporary.path()).filePath(QString("scheduling-%1.ply").arg(kind));
    if (!QDir().mkpath(root) || !fixture(path, kind) ||
        !document->createUntitled(root, "Viewport scheduling smoke", &error) ||
        !document->addScenePath(path, &error)) { passed = false; break; }
    const auto loaded = [&] {
      return kind == 1 ? viewport->meshRenderingAvailable()
          : kind >= 2 ? viewport->gaussianRenderingAvailable() : viewport->selectableModelAvailable();
    };
    const auto expectedMode = kind == 0 ? NativeViewport::RenderMode::Points
        : kind == 1 ? NativeViewport::RenderMode::Mesh : NativeViewport::RenderMode::Gaussians;
    if (!check(waitUntil([&] { return loaded() && viewport->renderMode() == expectedMode; }) &&
        viewport->sourceHasSurfelAttributes() == (kind == 3),
        "actual point/mesh/3DGS/2DGS mode loaded, not a point fallback")) { passed = false; break; }
    (void)viewport->grabFramebuffer();
    dispatchFor(400);
    // Exact scheduling contract, not a hardware FPS or power-use estimate.
    if (!check(!hasZeroIntervalTimer(*viewport), "loaded static geometry does not keep a zero-interval timer alive")) { passed = false; break; }
    const auto idleCallbacks = timerCallbacks;
    dispatchFor(120);
    if (!check(timerCallbacks == idleCallbacks, "idle zero-interval callbacks have stopped")) { passed = false; break; }
    if (!idlePerformanceLabelRegression(window, *viewport, kind)) { passed = false; break; }
    const float beforeZoom = viewport->viewDistance();
    const auto beforeFrames = presentedFrames;
    const auto beforePresentations = freshPresentations;
    const auto beforeRenderSerial = viewport->renderedFrameSerial();
    const QPointF center = viewport->rect().center();
    QWheelEvent wheel(center, viewport->mapToGlobal(center.toPoint()), {}, {0, 120},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    QElapsedTimer inputStopped;
    inputStopped.start();
    if (!check(viewport->viewDistance() != beforeZoom && waitUntil([&] { return presentedFrames > beforeFrames; }),
               "camera input still schedules a presented frame")) { passed = false; break; }
    dispatchFor(120);
    if (!check(!hasZeroIntervalTimer(*viewport), "camera interaction settles back to on-demand redraw")) { passed = false; break; }
    if (!check(freshPresentations > beforePresentations && viewport->renderedFrameSerial() > beforeRenderSerial &&
        presentedFrames > beforeFrames && std::isfinite(latestFps) && latestFps >= 0.0 &&
        std::isfinite(latestMilliseconds) && latestMilliseconds >= 0.0,
        "discrete wheel input completes a newly rendered/presented image and emits a fresh metric, not just FBO composition")) {
      passed = false; break;
    }
    if (!sharedPresentationSemantics(window, *viewport)) { passed = false; break; }
    const auto targetAfterWheel = viewport->viewTarget();
    const auto distanceAfterWheel = viewport->viewDistance();
    const auto orbitAfterWheel = viewport->viewOrbitAngles();
    const bool projectionAfterWheel = viewport->orthographicProjection();
    const QString idle = QCoreApplication::translate("Workbench", "空闲");
    // Observe natural event processing only: no update(), framebuffer grab or
    // screenshot is allowed to manufacture work while deciding idle/loop state.
    if (!check(waitUntil([&] { return viewport->viewportPerformanceText() == idle; }, 2500),
        "discrete navigation returns to the shared idle presentation after input stops")) { passed = false; break; }
    if (inputStopped.elapsed() < 350) dispatchFor(350 - static_cast<int>(inputStopped.elapsed()));
    const auto idlePresentations = freshPresentations;
    const auto idleMetrics = presentedFrames;
    const auto idleRenderSerial = viewport->renderedFrameSerial();
    dispatchFor(300);
    if (!check(viewport->viewportPerformanceText() == idle &&
        viewport->viewTarget() == targetAfterWheel && viewport->viewDistance() == distanceAfterWheel &&
        viewport->viewOrbitAngles() == orbitAfterWheel && viewport->orthographicProjection() == projectionAfterWheel &&
        freshPresentations <= idlePresentations + 1 && presentedFrames <= idleMetrics + 1 &&
        viewport->renderedFrameSerial() <= idleRenderSerial + 1 && !hasZeroIntervalTimer(*viewport),
        "after at least 350 ms without input, idle stays stable with no counter-only GL/metric loop or camera change")) {
      passed = false; break;
    }
    if (!sharedPresentationSemantics(window, *viewport)) { passed = false; break; }
    qInfo() << "VIEWPORT_PERFORMANCE_STATE discrete wheel" << kind
            << "fresh images/metric updates" << freshPresentations - beforePresentations << presentedFrames - beforeFrames
            << "post-idle GL/images/metrics" << viewport->renderedFrameSerial() - idleRenderSerial
            << freshPresentations - idlePresentations << presentedFrames - idleMetrics
            << "status" << viewport->viewportStatusText();
    const QString evidence = QDir(qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR", QDir::tempPath())).filePath(
        QStringLiteral("viewport-performance-state-") + qApp->property("gswUiTheme").toString());
    if (!check(QDir().mkpath(evidence) && window.grab().save(QDir(evidence).filePath(
        QStringLiteral("after-wheel-idle-%1.png").arg(kind))),
        "retain real discrete-input idle evidence only after the verdict")) { passed = false; break; }
    window.showMinimized();
    if (!check(waitUntil([&] { return window.isMinimized(); }), "test-owned window minimizes")) { passed = false; break; }
    dispatchFor(60);
    const auto minimizedCallbacks = timerCallbacks;
    const auto minimizedSamples = viewport->presentedFrameIntervalCount();
    const auto minimizedMetrics = presentedFrames;
    dispatchFor(120);
    if (!check(!hasZeroIntervalTimer(*viewport) && timerCallbacks == minimizedCallbacks &&
        viewport->viewportPerformanceText() == idle &&
        viewport->presentedFrameIntervalCount() == minimizedSamples && presentedFrames == minimizedMetrics,
        "minimization stops samples and zero-interval callbacks, not just painting")) { passed = false; break; }
    // Queue a real new source while hidden. Its pending GPU work must resume
    // after restoration instead of requiring another user gesture.
    const QString resume = QDir(temporary.path()).filePath(QString("resume-%1.ply").arg(kind));
    if (!fixture(resume, kind) || !document->addScenePath(resume, &error)) { passed = false; break; }
    dispatchFor(100);
    if (!check(!hasZeroIntervalTimer(*viewport), "hidden source loading cannot restart a zero-interval redraw loop")) { passed = false; break; }
    const auto beforeRestore = presentedFrames;
    if (originallyMaximized) window.showMaximized(); else window.showNormal();
    if (!check(waitUntil([&] { return !window.isMinimized() && loaded() && presentedFrames > beforeRestore; }),
               "restoring resumes real queued source upload and presentation")) { passed = false; break; }
    dispatchFor(400);
    if (!check(!hasZeroIntervalTimer(*viewport), "restored upload drains then becomes idle")) { passed = false; break; }
    qInfo() << "VIEWPORT_SCHEDULING matrix" << kind << "timer callbacks" << timerCallbacks << "presented frames" << presentedFrames;
  }
  if (window.isMinimized()) {
    if (originallyMaximized) window.showMaximized(); else window.showNormal();
  }
  QObject::disconnect(framesConnection);
  QObject::disconnect(presentationConnection);
  // The callbacks must not retain stack references after returning to the app.
  for (const auto &connection : timerConnections) QObject::disconnect(connection);
  document->createUntitled(temporary.path(), "Empty", &error);
  if (passed) passed = runViewportStatusControlsSmokeTest(window);
  if (passed) qInfo() << "VIEWPORT_SCHEDULING PASS: idle, input wake-up, minimization and restore-upload across point/mesh/3DGS/2DGS";
  return passed;
}
}
