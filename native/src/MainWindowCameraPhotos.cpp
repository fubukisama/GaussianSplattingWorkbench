#include "MainWindow.h"

#include "AppLanguage.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace gsw {

void MainWindow::addCalibratedPhotoMenu(QMenu *cameraMenu) {
  auto *menu = AppLanguage::text(cameraMenu->addMenu(QString()), AppLanguage::source("照片叠加"), "title");
  menu->setObjectName(QStringLiteral("calibratedPhotoMenu"));
  const auto add = [menu](const char *name, const char *label) {
    auto *action = AppLanguage::text(menu->addAction(QString()), label);
    action->setObjectName(QString::fromLatin1(name));
    action->setCheckable(true);
    return action;
  };
  mShowCalibratedPhotoAction = add("showCalibratedPhotoAction", AppLanguage::source("显示标定照片"));
  mShowCalibratedPhotoAction->setEnabled(mViewport->cameraCount() > 0);
  AppLanguage::bind(mShowCalibratedPhotoAction, "toolTip", AppLanguage::source("从对应相机比较照片与模型；关闭后返回原视图。标定照片可能已去畸变，与原图不同。"));
  auto *front = add("calibratedPhotoFrontAction", AppLanguage::source("叠在模型前方"));
  auto *behind = add("calibratedPhotoBehindAction", AppLanguage::source("放在模型后方"));
  auto *layers = new QActionGroup(menu);
  layers->addAction(front);
  layers->addAction(behind);
  front->setChecked(true);
  connect(layers, &QActionGroup::triggered, menu, [this,front](QAction *action) {
    mViewport->setCalibratedPhotoInFront(action == front);
  });
  auto *lock = add("lockCalibratedPhotoAction", AppLanguage::source("锁定当前照片"));
  lock->setEnabled(false);
  AppLanguage::bind(lock, "toolTip", AppLanguage::source("锁定当前比较照片；解锁后可随视角切换或选择其他相机。"));
  connect(lock, &QAction::toggled, mViewport, &NativeViewport::setCalibratedPhotoLocked);
  connect(mViewport, &NativeViewport::calibratedPhotoLockChanged, lock, &QAction::setChecked);
  auto *opacity = AppLanguage::text(menu->addMenu(QString()), AppLanguage::source("叠加不透明度"), "title");
  auto *levels = new QActionGroup(opacity);
  for (const int percent : {25, 50, 75, 100}) {
    auto *action = opacity->addAction(QStringLiteral("%1%").arg(percent));
    action->setCheckable(true);
    action->setObjectName(QStringLiteral("calibratedPhotoOpacity%1Action").arg(percent));
    action->setData(percent);
    levels->addAction(action);
    action->setChecked(percent == 50);
  }
  connect(levels, &QActionGroup::triggered, menu, [this](QAction *action) {
    mViewport->setCalibratedPhotoOpacity(action->data().toInt());
  });
  connect(mShowCalibratedPhotoAction, &QAction::toggled, menu, [this,lock](bool shown) {
    lock->setEnabled(shown);
    mViewport->setShowCalibratedPhoto(shown);
    mShowCalibratedPhotoAction->setEnabled(shown || mViewport->cameraCount() > 0);
    refreshCameraPhotoBinding();
  });
}

void MainWindow::createCameraPhotoDock() {
  mCameraPhotoCallbacks = new QObject(this);
  mCameraPhotoDock = new QDockWidget(this);
  mCameraPhotoDock->setObjectName(QStringLiteral("cameraPhotoDock"));
  AppLanguage::bind(mCameraPhotoDock, "windowTitle", AppLanguage::source("相机照片"));
  AppLanguage::bind(mCameraPhotoDock->toggleViewAction(), "text", AppLanguage::source("相机照片"));
  mCameraPhotoDock->toggleViewAction()->setObjectName(QStringLiteral("cameraPhotoListAction"));
  mCameraPhotoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  mCameraPhotoDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
  auto *body = new QWidget(mCameraPhotoDock);
  auto *layout = new QVBoxLayout(body);
  mCameraPhotosList = new QListWidget(body);
  mCameraPhotosList->setObjectName(QStringLiteral("cameraPhotosList"));
  mCameraPhotosList->setUniformItemSizes(true);
  AppLanguage::bind(mCameraPhotosList, "accessibleName", AppLanguage::source("相机照片"));
  layout->addWidget(mCameraPhotosList, 1);
  mCameraPhotoCaption = new QLabel(body);
  mCameraPhotoCaption->setWordWrap(true);
  mCameraPhotoCaption->setTextFormat(Qt::PlainText);
  mCameraPhotoCaption->setObjectName(QStringLiteral("cameraPhotoStatus"));
  layout->addWidget(mCameraPhotoCaption);
  auto *buttons = new QHBoxLayout;
  mCameraLocatePose = AppLanguage::text(new QPushButton(body), AppLanguage::source("定位相机"));
  mCameraLocatePose->setObjectName(QStringLiteral("cameraPhotoLocate"));
  buttons->addWidget(mCameraLocatePose);
  mCameraViewOriginal = AppLanguage::text(new QPushButton(body), AppLanguage::source("查看原图"));
  mCameraViewOriginal->setObjectName(QStringLiteral("cameraPhotoOriginal"));
  buttons->addWidget(mCameraViewOriginal);
  layout->addLayout(buttons);
  auto *scroll = new QScrollArea(body);
  mCameraPhotoImage = new QLabel(scroll);
  mCameraPhotoImage->setObjectName(QStringLiteral("cameraPhotoPixels"));
  mCameraPhotoImage->setAlignment(Qt::AlignCenter);
  scroll->setWidget(mCameraPhotoImage);
  scroll->setWidgetResizable(true);
  layout->addWidget(scroll, 2);
  mCameraPhotoDock->setWidget(body);
  addDockWidget(Qt::RightDockWidgetArea, mCameraPhotoDock);
  mCameraPhotoDock->hide();
  mCameraPhotoReadPool = new QThreadPool(this);
  mCameraPhotoReadPool->setMaxThreadCount(1);
  mCameraPhotoOriginalWatcher = new QFileSystemWatcher(this);
  connect(mCameraPhotoOriginalWatcher, &QFileSystemWatcher::fileChanged, mCameraPhotoCallbacks, [this](const QString &path) {
    const auto ref = mViewport->cameraPhotoReference(mCameraPhotoSelected);
    if (ref.relativePath.isEmpty() || QDir(ref.datasetRoot).filePath(ref.relativePath) != path) return;
    ++*mCameraPhotoViewEpoch;
    mCameraPhotoReadPool->clear();
    mCameraPhotoImage->clear();
    mCameraPhotoPending = false;
    mCameraPhotoStatus = QFileInfo(path).isFile() ? CameraPhotoStatus::Changed : CameraPhotoStatus::Missing;
    refreshCameraPhotoCaption();
  });

  connect(mViewport, &NativeViewport::cameraPhotoContextChanged, mCameraPhotoCallbacks, [this] {
    ++*mCameraPhotoViewEpoch;
    mCameraPhotoReadPool->clear();
    if (!mCameraPhotoOriginalWatcher->files().isEmpty())
      mCameraPhotoOriginalWatcher->removePaths(mCameraPhotoOriginalWatcher->files());
    mCameraPhotoImage->clear();
    mCameraPhotoPending = false;
    if (mCameraPhotoDock->isVisible()) rebuildCameraPhotoList();
  });
  connect(mCameraPhotoDock, &QDockWidget::visibilityChanged, mCameraPhotoCallbacks, [this](bool visible) {
    if (!visible) {
      ++*mCameraPhotoViewEpoch;
      mCameraPhotoReadPool->clear();
      if (!mCameraPhotoOriginalWatcher->files().isEmpty())
        mCameraPhotoOriginalWatcher->removePaths(mCameraPhotoOriginalWatcher->files());
      mCameraPhotoImage->clear();
      mCameraPhotoPending = false;
    }
    refreshCameraPhotoBinding();
    if (visible) rebuildCameraPhotoList();
  });
  connect(mViewport, &NativeViewport::cameraCardActivated, mCameraPhotoCallbacks, [this](qsizetype index, bool original) {
    mCameraPhotoSelected = index;
    if (!original && !mCameraPhotoDock->isVisible()) return;
    if (original) mCameraPhotoDock->show();
    const QSignalBlocker block(mCameraPhotosList);
    mCameraPhotosList->setCurrentRow(int(index));
    if (auto *item = mCameraPhotosList->currentItem()) mCameraPhotosList->scrollToItem(item);
    inspectCameraPhoto(index, original);
  });
  connect(mCameraPhotosList, &QListWidget::currentRowChanged, mCameraPhotoCallbacks, [this](int row) {
    (void)mViewport->highlightCameraPhoto(row);
    inspectCameraPhoto(row, false);
  });
  const auto locate = [this] {
    if (mCameraPhotoSelected >= 0) (void)mViewport->setSourceCameraView(mCameraPhotoSelected);
  };
  connect(mCameraLocatePose, &QPushButton::clicked, mCameraPhotoCallbacks, locate);
  connect(mCameraPhotosList, &QListWidget::itemActivated, mCameraPhotoCallbacks, [locate](QListWidgetItem *) { locate(); });
  connect(mCameraViewOriginal, &QPushButton::clicked, mCameraPhotoCallbacks,
          [this] { inspectCameraPhoto(mCameraPhotoSelected, true); });
  AppLanguage::onChanged(mCameraPhotoCallbacks, [this] {
    if (mCameraPhotoDock->isVisible()) {
      for (int i = 0; i < mCameraPhotosList->count(); ++i)
        if (mViewport->cameraPhotoName(i).isEmpty())
          mCameraPhotosList->item(i)->setText(QCoreApplication::translate("Workbench", "相机 %1").arg(i+1));
    }
    refreshCameraPhotoCaption();
  });
  refreshCameraPhotoCaption();
}

void MainWindow::stopCameraPhotoCallbacks() {
  ++*mCameraPhotoViewEpoch;
  // QWidget teardown can still emit dock visibility signals after the
  // MainWindow members used by those callbacks have been destroyed.
  delete std::exchange(mCameraPhotoCallbacks, nullptr);
  if (mCameraPhotoReadPool) mCameraPhotoReadPool->clear();
}

void MainWindow::rebuildCameraPhotoList() {
  const QSignalBlocker block(mCameraPhotosList);
  mCameraPhotosList->clear();
  for (qsizetype i = 0; i < mViewport->cameraCount(); ++i) {
    const auto name = mViewport->cameraPhotoName(i);
    mCameraPhotosList->addItem(name.isEmpty() ?
        QCoreApplication::translate("Workbench", "相机 %1").arg(i+1) : name);
  }
  if (mCameraPhotoSelected >= mViewport->cameraCount()) mCameraPhotoSelected = -1;
  mCameraPhotosList->setCurrentRow(int(mCameraPhotoSelected));
  if (mCameraPhotoSelected >= 0) (void)mViewport->highlightCameraPhoto(mCameraPhotoSelected);
  inspectCameraPhoto(mCameraPhotoSelected, false);
}

void MainWindow::refreshCameraPhotoCaption() {
  if (!mCameraPhotoCaption) return;
  const auto ref = mViewport->cameraPhotoReference(mCameraPhotoSelected);
  mCameraLocatePose->setEnabled(mCameraPhotoSelected >= 0 && mCameraPhotoSelected < mViewport->cameraCount());
  mCameraViewOriginal->setEnabled(!ref.relativePath.isEmpty() && !mCameraPhotoPending);
  QString caption;
  if (mCameraPhotoInspection || mCameraPhotoPending)
    caption = QCoreApplication::translate("Workbench", "正在核对对应照片…");
  else if (mCameraPhotoSelected < 0)
    caption = QCoreApplication::translate("Workbench", "选择相机以查看对应照片。");
  else {
    caption = ref.relativePath;
    const auto reason = cameraPhotoStatusText(ref.relativePath.isEmpty() ?
        CameraPhotoStatus::Unlinked : mCameraPhotoStatus);
    if (!reason.isEmpty()) caption += (caption.isEmpty() ? QString() : QStringLiteral("\n")) + reason;
    if (mCameraPhotoTaskOriginals && !ref.relativePath.isEmpty())
      caption += QLatin1Char('\n') + QCoreApplication::translate("Workbench",
          "已按此训练任务的数据集核对原图，不使用处理后的训练图。");
    else if (mCameraPhotoLegacy && !ref.relativePath.isEmpty())
      caption += QLatin1Char('\n') + QCoreApplication::translate("Workbench",
          "查看当前重建对应的原图；旧模型未保存训练时的图像校验。");
    else if (!ref.relativePath.isEmpty())
      caption += QLatin1Char('\n') + QCoreApplication::translate("Workbench",
          "按模型记录核对的标定数据集图像；可能不是未处理的相机原片。");
  }
  mCameraPhotoCaption->setText(caption);
}

void MainWindow::inspectCameraPhoto(qsizetype index, const bool original) {
  mCameraPhotoSelected = index;
  const auto epoch = mCameraPhotoViewEpoch;
  const auto generation = ++*epoch;
  mCameraPhotoReadPool->clear();
  if (!mCameraPhotoOriginalWatcher->files().isEmpty())
    mCameraPhotoOriginalWatcher->removePaths(mCameraPhotoOriginalWatcher->files());
  mCameraPhotoImage->clear();
  mCameraPhotoPending = false;
  const auto ref = mViewport->cameraPhotoReference(index);
  mCameraPhotoStatus = ref.relativePath.isEmpty() ? CameraPhotoStatus::Unlinked : CameraPhotoStatus::Available;
  if (ref.relativePath.isEmpty() || !mCameraPhotoDock->isVisible()) {
    refreshCameraPhotoCaption();
    return;
  }
  mCameraPhotoPending = true;
  refreshCameraPhotoCaption();
  auto *watcher = new QFutureWatcher<CameraPhotoRead>(this);
  connect(watcher, &QFutureWatcher<CameraPhotoRead>::finished, mCameraPhotoCallbacks, [this,watcher,generation,ref,index] {
    watcher->deleteLater();
    if (generation != mCameraPhotoViewEpoch->load() || ref != mViewport->cameraPhotoReference(index) ||
        !mCameraPhotoDock->isVisible()) return;
    mCameraPhotoPending = false;
    const auto result = watcher->future().resultCount() > 0 ? watcher->result() : CameraPhotoRead{};
    mCameraPhotoStatus = result.status;
    if (!result.pixels.isNull()) {
      const auto path = QDir(ref.datasetRoot).filePath(ref.relativePath);
      if (mCameraPhotoOriginalWatcher->addPath(path)) {
        mCameraPhotoImage->setPixmap(QPixmap::fromImage(result.pixels));
        mCameraPhotoImage->adjustSize();
      } else {
        mCameraPhotoStatus = QFileInfo(path).isFile() ? CameraPhotoStatus::Unreadable : CameraPhotoStatus::Missing;
      }
    }
    refreshCameraPhotoCaption();
  });
  watcher->setFuture(QtConcurrent::run(mCameraPhotoReadPool, [ref,original,epoch,generation] {
    return readCameraPhoto(ref, original ? QSize(1280,900) : QSize(400,290),
        [epoch,generation] { return generation != epoch->load(); });
  }));
}

} // namespace gsw
