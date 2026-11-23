#include "ReconstructionReviewSmokeTest.h"
#include "ReconstructionReviewDialog.h"
#include "TrainingDialog.h"
#include "MeshGenerationDialog.h"
#include "NativeViewport.h"
#include "AppLanguage.h"
#include "AppTheme.h"
#include "MainWindow.h"
#include "ViewportCamera.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <functional>
namespace gsw {
namespace {
bool wait(const std::function<bool()> &f){QElapsedTimer timer;timer.start();while(!f()&&timer.elapsed()<20000){QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}return f();}
bool check(bool ok,const char *s){std::fprintf(stderr,"Reconstruction review %s: %s\n",ok?"PASS":"FAIL",s);std::fflush(stderr);return ok;}
bool write(const QString &p,const QByteArray &data){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();}
QJsonObject snapshot(const QString &root){QJsonObject result;QDirIterator it(root,QDir::Files,QDirIterator::Subdirectories);while(it.hasNext()){QFile f(it.next());if(!f.open(QIODevice::ReadOnly))return {};result.insert(QDir(root).relativeFilePath(f.fileName()),QString::fromLatin1(QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex()));}return result;}
QString testPhotoName(int index){
  return index==1?QString::fromUtf8("別/frame0.png"):QString::fromUtf8("写真/frame%1.png").arg(index);
}
bool fixture(const QString &root){QDir dir(root);for(const auto &top:{QStringLiteral("input/"),QStringLiteral("images/")})
  if(!dir.mkpath(top+QString::fromUtf8("写真"))||!dir.mkpath(top+QString::fromUtf8("別")))return false;
  if(!write(dir.filePath(QStringLiteral("owned-main.ply")),"ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nend_header\n0 0 0\n1 0 0\n0 1 0\n"))return false;
  QImage photo(64,48,QImage::Format_RGB32);photo.fill(QColor(110,140,180));
  for(int i=0;i<9;++i){photo.fill(QColor(i==1?230:30+i*17,140,180));const auto name=testPhotoName(i);if(!photo.save(dir.filePath(QStringLiteral("input/")+name)))return false;if(i<2&&!photo.save(dir.filePath(QStringLiteral("images/")+name)))return false;}
  for(int group=0;group<2;++group){const QString folder=group==0?QStringLiteral("sparse/0"):QStringLiteral("distorted/sparse/1");if(!dir.mkpath(folder))return false;const QDir model(dir.filePath(folder));
    if(!write(model.filePath(QStringLiteral("cameras.txt")),"91 PINHOLE 64 48 50 55 32 24\n"))return false;
    QByteArray records;for(int i=0;i<(group==0?2:5);++i){records+=QStringLiteral("%1 0.7071067811865476 0 0 0.7071067811865476 %2 0 0 91 %3\n\n").arg(i*7+3).arg(i*.45).arg(testPhotoName(i)).toUtf8();}
    if(!write(model.filePath(QStringLiteral("images.txt")),records))return false;
    QByteArray points;for(int i=0;i<31;++i)points+=QStringLiteral("%1 %2 %3 %4 120 140 160 0.5 3 0 10 0\n").arg(i+1).arg(i*.01).arg((i%4)*.04).arg(2+(i%5)*.1).toUtf8();
    if(!write(model.filePath(QStringLiteral("points3D.txt")),points))return false;
  }return true;
}

bool mainCameraPhotos(const QString &fixtureRoot,const QString &python,const QString &evidence) {
  QTemporaryDir project(QDir(fixtureRoot).filePath(QStringLiteral("gallery-XXXXXX")));
  if(!project.isValid())return false;
  const auto file=project.filePath(QStringLiteral("gallery.gsw.json"));
  // Match the existing saved-project contract, with the manifest beside (not
  // inside) its owned data directory. No Save-As migration is being tested.
  const QDir root(WorkspaceDocument::projectDataRootForFile(file));
  const auto dataset=root.filePath(QStringLiteral("datasets/two-photos"));
  const auto output=root.filePath(QStringLiteral("output/fixture"));
  const auto model=QDir(output).filePath(QStringLiteral("result.ply"));
  if(!QDir().mkpath(output)||!fixture(dataset)||!QFile::copy(QDir(dataset).filePath(QStringLiteral("owned-main.ply")),model))return false;
  QJsonArray cameras;
  for(int i=0;i<2;++i) {
    cameras.append(QJsonObject{{"id",i},{"img_name",testPhotoName(i)},{"width",64},{"height",48},
        {"fx",50},{"fy",55},{"position",QJsonArray{0,i*.45,0}},
        {"rotation",QJsonArray{QJsonArray{0,1,0},QJsonArray{-1,0,0},QJsonArray{0,0,1}}}});
  }
  if(!write(QDir(output).filePath(QStringLiteral("cameras.json")),QJsonDocument(cameras).toJson()))return false;
  WorkspaceDocument document;
  const auto scene=QStringLiteral("gallery-owned-output");
  QString error;
  const auto fixtureStep=[&](bool ok,const char *label) {
    if(!ok)std::fprintf(stderr,"Camera photo fixture setup: %s\n",qPrintable(error));
    return check(ok,label);
  };
  if(!fixtureStep(document.create(root.absolutePath(),&error),"create a disposable main project")||
     !fixtureStep(document.setDatasetPath(dataset,&error),"link its synthetic dataset")||
     !fixtureStep(document.publishGeneratedScene(model,scene,&error),"temporary project owns the synthetic result")||
     !fixtureStep(document.save(file,&error),"manifest uses the supported data directory"))return false;
  GenerationExperiment item;item.id=QStringLiteral("gallery-test");item.pipeline=QStringLiteral("training");
  item.backend=QStringLiteral("3dgs");item.displayName=QFileInfo(model).fileName();
  item.datasetPath=dataset;item.outputRoot=output;item.resultPath=model;item.resultSceneId=scene;
  item.status=QStringLiteral("completed");item.createdAt=item.updatedAt=QDateTime::currentDateTimeUtc();
  if(!check(GenerationHistoryStore(root.absolutePath()).upsert(item),"owned result has a scoped camera/photo association"))return false;
  const auto prior=qgetenv("GAUSSIAN_SPLATTING_CONDA_PREFIX");
  const auto restore=qScopeGuard([prior]{if(prior.isNull())qunsetenv("GAUSSIAN_SPLATTING_CONDA_PREFIX");else qputenv("GAUSSIAN_SPLATTING_CONDA_PREFIX",prior);});
  qputenv("GAUSSIAN_SPLATTING_CONDA_PREFIX",QFileInfo(python).absolutePath().toLocal8Bit());
  MainWindow window;
  window.resize(1050,730);window.show();
  if(!check(window.openProjectFile(file),"open the disposable camera project through the main window"))return false;
  auto *viewport=window.findChild<NativeViewport*>();
  auto *show=window.findChild<QAction*>(QStringLiteral("showCamerasAction"));
  auto *thumbs=window.findChild<QAction*>(QStringLiteral("showCameraPhotosAction"));
  auto *frusta=window.findChild<QAction*>(QStringLiteral("showCameraFrustumsAction"));
  auto *order=window.findChild<QAction*>(QStringLiteral("showCameraRecordOrderAction"));
  auto *gallery=window.findChild<QAction*>(QStringLiteral("cameraPhotoListAction"));
  auto *list=window.findChild<QListWidget*>(QStringLiteral("cameraPhotosList"));
  auto *pixels=window.findChild<QLabel*>(QStringLiteral("cameraPhotoPixels"));
  auto *locate=window.findChild<QPushButton*>(QStringLiteral("cameraPhotoLocate"));
  if(!check(viewport&&show&&thumbs&&frusta&&order&&gallery&&list&&pixels&&locate&&
            wait([&]{return show->isEnabled()&&viewport->cameraCount()==2;}),"real main-window camera controls see the owned poses"))return false;
  show->setChecked(true);thumbs->setChecked(true);frusta->setChecked(true);order->setChecked(false);
  if(!check(thumbs->isChecked()&&frusta->isChecked()&&!order->isChecked(),"main camera modes are independently switchable"))return false;
  frusta->setChecked(false);
  if(!list->isVisible())gallery->trigger();
  if(!check(wait([&]{return list->isVisible()&&list->count()==2;}),"main dock lists the linked camera photos"))return false;
  list->setCurrentRow(1);
  if(!check(wait([&]{return !pixels->pixmap().isNull()&&pixels->pixmap().toImage().pixelColor(0,0)==QColor(230,140,180);}),
            "main dock displays the verified second original and not the same-name neighbor"))return false;
  if(!evidence.isEmpty()&&(!QDir().mkpath(evidence)||!window.grab().save(QDir(evidence).filePath(QStringLiteral("camera-photos-main-window.png")))))return false;
  const auto transform=viewport->modelTranslation();
  locate->click();
  const auto frame=orbitFrame(viewport->viewOrbitAngles());
  const auto center=viewport->viewTarget()+frame.cameraOffsetDirection*viewport->viewDistance();
  return check((center-viewport->sceneCoordinates().localFromGlobal({0,.45,0})).length()<.0001F&&
               viewport->modelTranslation()==transform,"main photo panel locates the exact pose without editing its model");
}
}
bool runReconstructionReviewSmokeTest(const QString &python,const QString &backend) {
  const auto fixtureRoot=qEnvironmentVariable("GSW_RECONSTRUCTION_REVIEW_ROOT",QDir(backend).filePath(QStringLiteral(".tmp/reconstruction-review-fixtures")));
  if(!QDir().mkpath(fixtureRoot))return false;
  QTemporaryDir temp(QDir(fixtureRoot).filePath(QStringLiteral("source-XXXXXX")));if(!temp.isValid()||!fixture(temp.path()))return false;
  const auto before=snapshot(temp.path());
  ReconstructionReviewDialog dialog(temp.path(),python,backend,QProcessEnvironment::systemEnvironment());dialog.show();
  if(!check(wait([&]{return !dialog.busy()&&dialog.report().value(QStringLiteral("ready")).toBool();}),"real read-only process returns a report"))return false;
  const auto report=dialog.report();
  NativeViewport unrelated;unrelated.resize(400,300);unrelated.setScene(QDir(temp.path()).filePath(QStringLiteral("owned-main.ply")),3);unrelated.show();
  if(!check(wait([&]{return unrelated.renderedPointCount()==3;}),"independent main-model fixture loads actual geometry"))return false;
  unrelated.selectModel();unrelated.setModelTranslation(QVector3D(2,3,4));const auto mainPath=unrelated.scenePath();const auto mainIds=unrelated.selectedSceneIds();const auto mainTarget=unrelated.viewTarget();
  if(!check(!unrelated.setReconstructionReviewData(report)&&unrelated.scenePath()==mainPath&&unrelated.selectedSceneIds()==mainIds&&unrelated.modelTranslation()==QVector3D(2,3,4)&&unrelated.viewTarget()==mainTarget,"review data cannot repurpose an existing model viewport or change model state"))return false;
  unrelated.close();
  if(!check(report.value(QStringLiteral("originalCount")).toInt()==9&&report.value(QStringLiteral("registeredCount")).toInt()==2&&report.value(QStringLiteral("unregisteredCount")).toInt()==7&&report.value(QStringLiteral("trainingCount")).toInt()==2,"nine originals, two registered, seven unregistered and two training images remain separate"))return false;
  auto *table=dialog.findChild<QTableView*>(QStringLiteral("reviewImages"));auto *filter=dialog.findChild<QComboBox*>(QStringLiteral("reviewFilter"));auto *groups=dialog.findChild<QComboBox*>(QStringLiteral("reviewGroups"));
  auto *locate=dialog.findChild<QPushButton*>(QStringLiteral("reviewLocateCamera"));auto *restore=dialog.findChild<QPushButton*>(QStringLiteral("reviewRestoreView"));auto *photo=dialog.findChild<QPushButton*>(QStringLiteral("reviewOpenPhoto"));auto *thumb=dialog.findChild<QLabel*>(QStringLiteral("reviewThumbnail"));auto *viewport=dialog.findChild<NativeViewport*>(QStringLiteral("reviewSparseViewport"));
  if(!table||!filter||!groups||!locate||!restore||!photo||!thumb||!viewport)return false;
  if(!check(filter->findData(QStringLiteral("unregistered"))>=0&&groups->findData(QStringLiteral("distorted/sparse/1"))>=0,"filters and components retain exact string user-data identities"))return false;
  filter->setCurrentIndex(filter->findData(QStringLiteral("unregistered")));
  if(!check(table->model()->rowCount()==7,"actual filter exposes all seven unregistered originals"))return false;
  table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);
  const auto originalTarget=viewport->viewTarget();
  if(!check(!viewport->setSourceCameraView(0)&&viewport->viewTarget()==originalTarget,"the generic first-camera shortcut cannot bypass review-row identity checks"))return false;
  if(!check(!locate->isEnabled()&&photo->isEnabled()&&wait([&]{return !thumb->pixmap().isNull();}),"unregistered original previews without a camera pose"))return false;
  if(!check(thumb->pixmap().toImage().pixelColor(0,0)==QColor(64,140,180),"thumbnail reads the selected third original's independent color, not the prepared subset"))return false;
  bool originalOpened=false;QTimer photoCloser;photoCloser.setInterval(10);QObject::connect(&photoCloser,&QTimer::timeout,&dialog,[&]{auto *viewer=dialog.findChild<QDialog*>(QStringLiteral("reviewOriginalImageDialog"));if(!viewer)return;const auto *image=viewer->findChild<QLabel*>(QStringLiteral("reviewOriginalImagePixels"));originalOpened=image&&!image->pixmap().isNull()&&image->pixmap().toImage().pixelColor(0,0)==QColor(64,140,180)&&viewer->property("reviewOriginalPath")==QString::fromUtf8("input/写真/frame2.png");viewer->reject();photoCloser.stop();});photoCloser.start();photo->click();
  if(!check(wait([&]{return originalOpened;}),"View Original opens the actual selected original in an owned native image window"))return false;photoCloser.stop();
  QMetaObject::invokeMethod(table,"doubleClicked",Qt::DirectConnection,Q_ARG(QModelIndex,table->model()->index(0,0)));
  if(!check(viewport->viewTarget()==originalTarget,"double click on an unregistered image cannot move the camera"))return false;
  filter->setCurrentIndex(filter->findData(QStringLiteral("registered")));table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);
  if(!check(locate->isEnabled()&&wait([&]{return viewport->renderedPointCount()==31;}),"registered row has a pose and real sparse geometry is displayed"))return false;
  viewport->setAxisView(NavigationAxis::PositiveZ);
  // Switching to orthographic is immediate; the snap animation is not.
  // Compare real frames only once the independently known +Z axis is reached.
  if(!check(wait([&]{
    return viewport->orthographicProjection()&&
      (orbitFrame(viewport->viewOrbitAngles()).cameraOffsetDirection-QVector3D(0,0,1)).length()<.0002F;
  }),"orthographic view has reached the +Z axis before capturing camera pixels"))return false;
  viewport->setShowCameras(false);
  const QImage cardsOff=viewport->grabFramebuffer();
  viewport->setShowCameras(true);
  const QImage cardsOn=viewport->grabFramebuffer();
  if(!check(!cardsOff.isNull()&&cardsOff.size()==cardsOn.size(),"real viewport captures the same orthographic comparison"))return false;
  const auto overlayEvidence=qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR");
  if(!overlayEvidence.isEmpty()&&(!QDir().mkpath(overlayEvidence)||
     !cardsOff.save(QDir(overlayEvidence).filePath(QStringLiteral("camera-overlay-off.png")))||
     !cardsOn.save(QDir(overlayEvidence).filePath(QStringLiteral("camera-overlay-on.png")))))return false;
  int blue=0,green=0;
  for(int y=cardsOn.height()/4;y<cardsOn.height()*3/4;++y)for(int x=cardsOn.width()/6;x<cardsOn.width()*5/6;++x){
    const QColor a=cardsOn.pixelColor(x,y),b=cardsOff.pixelColor(x,y);
    if(std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue())<30)continue;
    blue+=a.blue()-a.red()>28&&a.blue()-a.green()>14;
    green+=a.green()-a.red()>35&&a.green()-a.blue()>18;
  }
  std::fprintf(stderr,"Reconstruction review camera overlay pixels: blue=%d green=%d\n",blue,green);
  if(!check(blue>=24&&green==0,"default camera overlay shows blue cards without green diagnostic rays"))return false;
  auto *showPhotos=dialog.findChild<QCheckBox*>(QStringLiteral("reviewShowCameraPhotos"));
  if(!check(showPhotos&&showPhotos->isEnabled()&&!showPhotos->isChecked(),"corresponding photos have an independent initially-off control"))return false;
  showPhotos->click();
  const auto coloredPhotoPixels=[&]{
    const QImage image=viewport->grabFramebuffer();
    bool left=false,right=false;
    for(int y=image.height()/4;y<image.height()*3/4;++y)for(int x=image.width()/6;x<image.width()*5/6;++x) {
      const auto px=image.pixelColor(x,y);
      left|=px==QColor(30,140,180);right|=px==QColor(230,140,180);
    }
    return left&&right;
  };
  if(!check(wait(coloredPhotoPixels),"same basename in two folders renders both independently verified photo colors"))return false;
  const auto thumbFrame=viewport->grabFramebuffer();
  const auto evidencePath=qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR");
  if(!evidencePath.isEmpty()&&(!QDir().mkpath(evidencePath)||
      !thumbFrame.save(QDir(evidencePath).filePath(QStringLiteral("camera-photos-on.png")))))return false;
  QPointF secondCenter;
  int secondPixels=0;
  for(int y=thumbFrame.height()/4;y<thumbFrame.height()*3/4;++y)for(int x=thumbFrame.width()/6;x<thumbFrame.width()*5/6;++x)
    if(thumbFrame.pixelColor(x,y)==QColor(230,140,180)){secondCenter+=QPointF(x,y);++secondPixels;}
  if(!check(secondPixels>=12,"second photo has a visible pickable interior"))return false;
  secondCenter /= secondPixels;
  secondCenter=QPointF(secondCenter.x()*viewport->width()/thumbFrame.width(),
                       secondCenter.y()*viewport->height()/thumbFrame.height());
  const auto sendPhotoMouse=[&](QEvent::Type type,Qt::MouseButtons buttons){
    QMouseEvent event(type,secondCenter,viewport->mapToGlobal(secondCenter.toPoint()),
                      Qt::LeftButton,buttons,Qt::NoModifier);
    QApplication::sendEvent(viewport,&event);
  };
  const auto beforeCardClick=viewport->viewTarget();
  sendPhotoMouse(QEvent::MouseButtonPress,Qt::LeftButton);
  sendPhotoMouse(QEvent::MouseButtonRelease,Qt::NoButton);
  const auto chosen=table->currentIndex().siblingAtColumn(0).data(Qt::UserRole).toJsonObject();
  if(!check(chosen.value(QStringLiteral("name"))==QJsonValue(testPhotoName(1))&&
            viewport->viewTarget()==beforeCardClick,"clicking the painted second card selects its exact photo without orbiting"))return false;
  bool cardOriginalOpened=false;
  QTimer cardPhotoCloser;cardPhotoCloser.setInterval(10);
  QObject::connect(&cardPhotoCloser,&QTimer::timeout,&dialog,[&]{
    auto *viewer=dialog.findChild<QDialog*>(QStringLiteral("reviewOriginalImageDialog"));if(!viewer)return;
    auto *label=viewer->findChild<QLabel*>(QStringLiteral("reviewOriginalImagePixels"));
    cardOriginalOpened=label&&!label->pixmap().isNull()&&label->pixmap().toImage().pixelColor(0,0)==QColor(230,140,180)&&
        viewer->property("reviewOriginalPath").toString()==QStringLiteral("input/")+testPhotoName(1);
    viewer->reject();cardPhotoCloser.stop();
  });
  cardPhotoCloser.start();
  sendPhotoMouse(QEvent::MouseButtonDblClick,Qt::LeftButton);
  sendPhotoMouse(QEvent::MouseButtonRelease,Qt::NoButton);
  if(!check(wait([&]{return cardOriginalOpened;}),"double clicking the card opens the same real photo, not its same-name neighbor"))return false;
  cardPhotoCloser.stop();
  showPhotos->click();
  table->setCurrentIndex(table->model()->index(0,0));table->selectRow(0);
  const auto target=viewport->viewTarget();const auto distance=viewport->viewDistance();const auto angles=viewport->viewOrbitAngles();const auto orthographic=viewport->orthographicProjection();
  locate->click();if(!check(wait([&]{return !dialog.busy()&&!viewport->orthographicProjection();}),"locate independently revalidates report identity before native navigation"))return false;
  const auto frame=orbitFrame(viewport->viewOrbitAngles());const auto center=viewport->viewTarget()+frame.cameraOffsetDirection*viewport->viewDistance();
  const auto wanted=viewport->sceneCoordinates().localFromGlobal({0,0,0});
  // COLMAP stores world-to-camera q, not camera-to-world q. Here R_w2c is
  // [[0,-1,0],[1,0,0],[0,0,1]], so R_c2w=transpose(R_w2c), and image-up
  // is -column_Y(R_c2w)=(-1,0,0). This expectation is independent of the loader.
  const QVector3D wantedUp(-1,0,0);const float centerError=(center-wanted).length(),forwardError=(-frame.cameraOffsetDirection-QVector3D(0,0,1)).length(),upError=(frame.upDirection-wantedUp).length();
  std::fprintf(stderr,"Reconstruction review pose evidence: centerError=%.9g forwardError=%.9g upError=%.9g actualUp=(%.9g,%.9g,%.9g) expectedUp=(-1,0,0) tolerance=0.0001\n",double(centerError),double(forwardError),double(upError),double(frame.upDirection.x()),double(frame.upDirection.y()),double(frame.upDirection.z()));
  if(!check(centerError<.0001F&&forwardError<.0001F&&upError<.0001F,"native camera center, forward and roll match independently computed COLMAP source pose"))return false;
  restore->click();if(!check((viewport->viewTarget()-target).length()<.0001F&&std::abs(viewport->viewDistance()-distance)<.0001F&&viewport->viewOrbitAngles()==angles&&viewport->orthographicProjection()==orthographic,"restore returns projection, target, orbit and distance"))return false;
  const QString originalLanguage=AppLanguage::current();const auto originalTheme=AppTheme::currentTheme();
  const int originalScale=QCoreApplication::instance()->property("gswUiScalePercent").toInt();
  const auto stableId=table->model()->index(0,0).data(Qt::UserRole).toJsonObject().value(QStringLiteral("id"));
  const QString output=qEnvironmentVariable("GSW_SMOKE_OUTPUT_DIR");if(!output.isEmpty())QDir().mkpath(output);
  for(const auto theme:{UiTheme::Light,UiTheme::Dark}){AppTheme::applyTheme(*qobject_cast<QApplication*>(QCoreApplication::instance()),theme,false);
    for(const auto &language:AppLanguage::supported()){AppLanguage::apply(language,false);QApplication::processEvents();
      if(!check(table->model()->rowCount()==2&&filter->currentData()==QStringLiteral("registered")&&table->model()->index(0,0).data(Qt::UserRole).toJsonObject().value(QStringLiteral("id"))==stableId&&viewport->viewTarget()==target&&dialog.report()==report,"live language/theme changes preserve selection, filter, data and view"))return false;
      for(int width:{1080,560}){AppTheme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()),width==560?150:originalScale,false);dialog.resize(width,740);QApplication::processEvents();if(!check(dialog.width()==width&&locate->isVisible()&&restore->isVisible()&&groups->isVisible(),"normal and narrow 150-percent native windows keep controls available"))return false;
        if(!output.isEmpty()&&!dialog.grab().save(QDir(output).filePath(QStringLiteral("reconstruction-review-%1-%2-%3.png").arg(language,theme==UiTheme::Light?QStringLiteral("light"):QStringLiteral("dark")).arg(width))))return false;
      }
    }
  }
  AppLanguage::apply(originalLanguage,false);AppTheme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()),originalScale,false);AppTheme::applyTheme(*qobject_cast<QApplication*>(QCoreApplication::instance()),originalTheme,false);
  groups->setCurrentIndex(groups->findData(QStringLiteral("distorted/sparse/1")));
  if(!check(wait([&]{return !dialog.busy()&&dialog.report().value(QStringLiteral("registeredCount")).toInt()==5;})&&dialog.report().value(QStringLiteral("trainingCount")).toInt()==2&&dialog.report().value(QStringLiteral("actualTrainingGroup"))==QStringLiteral("sparse/0"),"inspecting another component does not change the training component or subset"))return false;
  dialog.close();if(!check(snapshot(temp.path())==before,"all original images, models and dataset files remain byte-identical"))return false;
  // An old report must not drive a different context; repeated inspection and
  // hide cancel old QProcess/image callbacks, even when delivered late.
  ReconstructionReviewDialog cancelled(temp.path(),python,backend,QProcessEnvironment::systemEnvironment());cancelled.show();cancelled.inspect(QStringLiteral("sparse/0"));cancelled.close();QApplication::processEvents();
  if(!check(!cancelled.busy(),"closing cancels the read-only helper and invalidates late callbacks"))return false;
  QTemporaryDir otherRoot(QDir(fixtureRoot).filePath(QStringLiteral("other-XXXXXX")));if(!otherRoot.isValid()||!fixture(otherRoot.path()))return false;
  for(int i=3;i<9;++i)if(!QFile::remove(QDir(otherRoot.path()).filePath(QString::fromUtf8("input/写真/frame%1.png").arg(i))))return false;
  ReconstructionReviewDialog other(otherRoot.path(),python,backend,QProcessEnvironment::systemEnvironment());other.show();
  if(!check(wait([&]{return !other.busy()&&other.report().value(QStringLiteral("ready")).toBool();})&&other.report().value(QStringLiteral("originalCount")).toInt()==3&&QDir::cleanPath(other.report().value(QStringLiteral("dataset")).toString())==QDir::cleanPath(otherRoot.path()),"new project context cannot adopt the closed inspection's nine-image response"))return false;
  auto *otherTable=other.findChild<QTableView*>(QStringLiteral("reviewImages"));auto *otherLocate=other.findChild<QPushButton*>(QStringLiteral("reviewLocateCamera"));auto *otherViewport=other.findChild<NativeViewport*>(QStringLiteral("reviewSparseViewport"));
  if(!otherTable||!otherLocate||!otherViewport)return false;otherTable->setCurrentIndex(otherTable->model()->index(0,0));otherTable->selectRow(0);
  if(!check(otherLocate->isEnabled(),"fresh registered image permits navigation before source mutation"))return false;
  const auto otherTarget=otherViewport->viewTarget();QFile changed(QDir(otherRoot.path()).filePath(QStringLiteral("sparse/0/cameras.txt")));if(!changed.open(QIODevice::Append)||changed.write("\n")!=1)return false;changed.close();
  otherLocate->click();if(!check(!otherLocate->isEnabled()&&!otherViewport->isEnabled()&&otherViewport->viewTarget()==otherTarget,"changed source expires the real UI report and prevents old camera navigation"))return false;
  const auto otherRefs=cameraPhotoReferences(other.report());
  const auto otherPath=QStringLiteral("input/")+testPhotoName(0);
  CameraPhotoReference expectedPhoto;
  for(const auto &ref:otherRefs)if(ref.relativePath==otherPath)expectedPhoto=ref;
  if(!check(readCameraPhoto(expectedPhoto,QSize(64,48)).status==CameraPhotoStatus::Available,"original photo still matches its recorded bytes"))return false;
  QImage replacement(64,48,QImage::Format_RGB32);replacement.fill(Qt::red);
  const auto path=QDir(otherRoot.path()).filePath(otherPath);
  if(!replacement.save(path))return false;
  const auto refusedPhoto=readCameraPhoto(expectedPhoto,QSize(64,48));
  if(!check(refusedPhoto.status==CameraPhotoStatus::Changed&&refusedPhoto.pixels.isNull(),"replaced original falls back without displaying replacement pixels"))return false;
  if(!QFile::remove(path))return false;
  const auto missingPhoto=readCameraPhoto(expectedPhoto,QSize(64,48));
  if(!check(missingPhoto.status==CameraPhotoStatus::Missing&&missingPhoto.pixels.isNull(),"missing original produces no substituted photo"))return false;
  other.close();
  for(const auto &id:{QStringLiteral("3dgs"),QStringLiteral("2dgs")}){TrainingDialog training(temp.path(),QStringLiteral("owned"),temp.path(),true,true);auto *backendCombo=training.findChild<QComboBox*>(QStringLiteral("trainingBackendCombo"));auto *button=training.findChild<QPushButton*>(QStringLiteral("trainingReconstructionReview"));if(!backendCombo||!button)return false;backendCombo->setCurrentIndex(backendCombo->findData(id));const auto config=training.configurationDocument();int requests=0;QObject::connect(&training,&TrainingDialog::reconstructionReviewRequested,&training,[&](const QString &path,const QString &,const QString &){if(path==temp.path())++requests;});button->click();if(!check(requests==1&&training.configurationDocument()==config,"3DGS/2DGS shared review entry never changes training settings or starts a worker"))return false;}
  MeshGenerationDialog mesh(temp.path());auto *button=mesh.findChild<QPushButton*>(QStringLiteral("meshReconstructionReview"));auto *method=mesh.findChild<QComboBox*>(QStringLiteral("meshMethodCombo"));if(!button||!method)return false;int requests=0;QObject::connect(&mesh,&MeshGenerationDialog::reconstructionReviewRequested,&mesh,[&]{++requests;});for(int i=0;i<method->count();++i){method->setCurrentIndex(i);const auto config=mesh.configuration();button->click();if(!check(mesh.configuration()==config,"all mesh/depth/texture methods retain their existing configuration and compatibility gate"))return false;}
  return check(requests==method->count()&&snapshot(temp.path())==before,"shared entry emits observation requests only; no task or source write")&&
      mainCameraPhotos(fixtureRoot,python,output);
}
}
