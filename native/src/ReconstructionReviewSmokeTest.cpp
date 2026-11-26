#include "ReconstructionReviewSmokeTest.h"
#include "ReconstructionReviewDialog.h"
#include "TrainingDialog.h"
#include "MeshGenerationDialog.h"
#include "NativeViewport.h"
#include "AppLanguage.h"
#include "AppTheme.h"
#include "MainWindow.h"
#include "AppLanguage.h"
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
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <array>
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
  if(!QDir().mkpath(output)||!fixture(dataset))return false;
  // Independently surveyed quad. At camera 1, K projects these four world
  // corners onto (35,25), (41,25), (41,31), (35,31) in the 64x48 raster.
  const QByteArray quad =
      "ply\nformat ascii 1.0\nelement vertex 6\nproperty float x\nproperty float y\nproperty float z\n"
      "property uchar red\nproperty uchar green\nproperty uchar blue\n"
      "element face 2\nproperty list uchar int vertex_indices\nend_header\n"
      "0.07272727 0.21 4 255 100 20\n0.07272727 -0.27 4 255 100 20\n"
      "0.50909091 -0.27 4 255 100 20\n0.50909091 0.21 4 255 100 20\n"
      "-1 -1 -1 255 100 20\n1 1 4 255 100 20\n3 0 2 1\n3 0 3 2\n";
  const QColor firstRaster(46,179,12), secondRaster(14,27,233);
  const auto registeredPath=[&](int i) {
    return QDir(dataset).filePath(QStringLiteral("images/") + testPhotoName(i));
  };
  QImage registered(64,48,QImage::Format_RGB32);
  registered.fill(firstRaster);
  if(!write(model,quad)||!registered.save(registeredPath(0)))return false;
  registered.fill(secondRaster);
  if(!registered.save(registeredPath(1)))return false;
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
  auto *labels=window.findChild<QAction *>(QStringLiteral("showCameraLabelsAction"));
  if(!check(labels&&labels->isEnabled()&&labels->isCheckable(),
            "camera name leaders have an independent viewport control"))return false;
  auto *overlay=window.findChild<QAction *>(QStringLiteral("showCalibratedPhotoAction"));
  auto *lock=window.findChild<QAction *>(QStringLiteral("lockCalibratedPhotoAction"));
  auto *behind=window.findChild<QAction *>(QStringLiteral("calibratedPhotoBehindAction"));
  auto *front=window.findChild<QAction *>(QStringLiteral("calibratedPhotoFrontAction"));
  auto *opaque=window.findChild<QAction *>(QStringLiteral("calibratedPhotoOpacity100Action"));
  auto *partial=window.findChild<QAction *>(QStringLiteral("calibratedPhotoOpacity25Action"));
  if(!check(overlay&&lock&&behind&&front&&opaque&&partial&&overlay->isEnabled()&&
            wait([&]{return viewport->isValid()&&viewport->meshRenderingAvailable();}),
            "real main viewport admits a calibrated photo comparison over visible geometry"))return false;
  viewport->setRenderMode(NativeViewport::RenderMode::Mesh);
  viewport->setShowFrameRate(false);
  viewport->setAxisView(NavigationAxis::PositiveZ);
  if(!check(wait([&]{return viewport->orthographicProjection()&&
           std::abs(viewport->viewOrbitAngles().pitchDegrees-90.F)<.01F;}),
            "the camera label view reaches its settled orthographic pose"))return false;
  const auto evidenceFile=[&](const QString &name) { return QDir(evidence).filePath(name); };
  if(!evidence.isEmpty()&&!QDir().mkpath(evidence))return false;
  labels->setChecked(false);
  const auto unlabelled=viewport->grabFramebuffer();
  labels->trigger();
  const auto labelled=viewport->grabFramebuffer();
  qsizetype added=0;
  if(unlabelled.size()==labelled.size())
    for(int y=90;y<labelled.height()-35;++y)
      for(int x=8;x<labelled.width()-70;++x)
        added += labelled.pixel(x,y)!=unlabelled.pixel(x,y);
  if(!check(added>150&&thumbs->isChecked()&&!overlay->isChecked(),
            "camera labels add visible leaders without enabling the separate photo overlay"))return false;
  if(!evidence.isEmpty()&&(!window.grab().save(evidenceFile(QStringLiteral("camera-photos-main-window.png")))||
      !labelled.save(evidenceFile(QStringLiteral("camera-labels-framebuffer.png")))))return false;
  const auto prevLanguage=AppLanguage::current();
  const auto resetLanguage=qScopeGuard([&]{ (void)AppLanguage::apply(prevLanguage,false); });
  const std::array<QString,3> languages{QStringLiteral("zh_CN"),QStringLiteral("en_US"),QStringLiteral("ja_JP")};
  const std::array<QString,3> captions{QString::fromUtf8("显示相机标签"),
      QString::fromLatin1("Show Camera Labels"),QString::fromUtf8("カメラのラベルを表示")};
  for(size_t i=0;i<languages.size();++i) {
    if(!check(AppLanguage::apply(languages[i],false)&&labels->text()==captions[i]&&labels->isChecked(),
              "camera label caption follows the live language without resetting its state"))return false;
    if(!evidence.isEmpty()&&!viewport->grabFramebuffer().save(evidenceFile(
           QStringLiteral("camera-labels-%1.png").arg(languages[i]))))return false;
  }
  labels->trigger();
  const auto transform=viewport->modelTranslation();
  locate->click();
  const auto frame=orbitFrame(viewport->viewOrbitAngles());
  const auto center=viewport->viewTarget()+frame.cameraOffsetDirection*viewport->viewDistance();
  if(!check((center-viewport->sceneCoordinates().localFromGlobal({0,.45,0})).length()<.0001F&&
            viewport->modelTranslation()==transform,"main photo panel locates the exact pose without editing its model"))return false;
  viewport->restoreSourceCameraView();
  show->setChecked(false);
  const auto oldTarget=viewport->viewTarget();
  const auto oldAngles=viewport->viewOrbitAngles();
  const auto oldProjection=viewport->orthographicProjection();
  const auto eye=[&] {
    return viewport->viewTarget()+orbitFrame(viewport->viewOrbitAngles()).cameraOffsetDirection*viewport->viewDistance();
  };
  const auto sample=[](const QImage &image,double u,double v) {
    if(image.isNull())return QColor();
    const QSizeF size=QSizeF(64,48).scaled(QSizeF(image.size()),Qt::KeepAspectRatio);
    return image.pixelColor(int((image.width()-size.width())/2+size.width()*u/64.),
                            int((image.height()-size.height())/2+size.height()*v/48.));
  };
  const auto paints=[&](const QColor &expected) {
    const auto image=viewport->grabFramebuffer();
    int hits=0, total=0;
    // An opaque flat fixture should occupy most of its calibrated rectangle.
    // Do not infer which photo is displayed from one pixel: the real viewport
    // also draws editable gizmos, bounds, labels and controls over the image.
    for(int v=7;v<43;v+=2) for(int u=7;u<59;u+=2) {
      const auto actual=sample(image,u,v);
      hits+=actual.isValid()&&std::abs(actual.red()-expected.red())<=2&&
             std::abs(actual.green()-expected.green())<=2&&std::abs(actual.blue()-expected.blue())<=2;
      ++total;
    }
    return hits*4>=total*3;
  };
  opaque->trigger();
  overlay->trigger();
  if(!check(wait([&]{return paints(secondRaster);}),"front overlay uses the exact registered pixels rather than archived original"))return false;
  auto frontFrame=viewport->grabFramebuffer();
  if(!evidence.isEmpty()&&!frontFrame.save(evidenceFile(QStringLiteral("calibrated-front.png"))))return false;
  behind->trigger();
  const auto behindFrame=viewport->grabFramebuffer();
  const auto inQuad=sample(behindFrame,38,28);
  if(!check(sample(frontFrame,38,28)==secondRaster&&inQuad.isValid()&&
            inQuad.red()>inQuad.blue()&&sample(behindFrame,33,28)==secondRaster&&
            sample(behindFrame,43,28)==secondRaster&&sample(behindFrame,38,22)==secondRaster,
            "the back layer exposes the surveyed quad at its independently known photo pixels"))return false;
  if(!evidence.isEmpty()&&!behindFrame.save(evidenceFile(QStringLiteral("calibrated-behind.png"))))return false;
  front->trigger();
  partial->trigger();
  const auto blended=sample(viewport->grabFramebuffer(),38,28);
  if(!check(blended.red()>secondRaster.red()&&blended.red()<inQuad.red()&&blended.blue()>inQuad.blue(),
            "lower front opacity blends the same calibrated pixels with the model"))return false;
  opaque->trigger();
  lock->trigger();
  if(!check(lock->isChecked()&&viewport->setSourceCameraView(0)&&wait([&]{return paints(secondRaster);})&&
            (eye()-center).length()<.0001F,"the locked comparison holds its photo and exact pose while the view moves"))return false;
  lock->trigger();
  const bool positioned=viewport->setSourceCameraView(0);
  const auto beforeFollow=eye();
  const auto expectedEye=viewport->sceneCoordinates().localFromGlobal({0,0,0});
  if(!check(!lock->isChecked()&&positioned&&wait([&]{
              return paints(firstRaster)&&(eye()-expectedEye).length()<.0001F;}),
            "unlocked comparison follows the closest calibrated camera after navigation")) {
    const auto actualFrame=viewport->grabFramebuffer();
    const auto actualEye=eye();
    const auto point=sample(actualFrame,20,36);
    qsizetype firstPixels=0,secondPixels=0;
    for(int y=0;y<actualFrame.height();y+=4) for(int x=0;x<actualFrame.width();x+=4) {
      const auto color=actualFrame.pixelColor(x,y);
      firstPixels+=color==firstRaster; secondPixels+=color==secondRaster;
    }
    std::fprintf(stderr,"Comparison witness: lock=%d positioned=%d eye before=[%.9g,%.9g,%.9g]"
        " after=[%.9g,%.9g,%.9g] expected=[%.9g,%.9g,%.9g] sample=[%d,%d,%d]"
        " frame-quarter-samples first=%lld second=%lld\n",
        lock->isChecked(),positioned,beforeFollow.x(),beforeFollow.y(),beforeFollow.z(),
        actualEye.x(),actualEye.y(),actualEye.z(),expectedEye.x(),expectedEye.y(),expectedEye.z(),
        point.red(),point.green(),point.blue(),static_cast<long long>(firstPixels),static_cast<long long>(secondPixels));
    if(!evidence.isEmpty()) (void)actualFrame.save(evidenceFile(QStringLiteral("auto-follow-failed.png")));
    list->setCurrentRow(0);
    const bool manual=wait([&]{return paints(firstRaster);});
    std::fprintf(stderr,"Comparison witness: explicit selection independently finds first raster=%d\n",manual);
    if(!evidence.isEmpty()) (void)viewport->grabFramebuffer().save(evidenceFile(QStringLiteral("manual-first-witness.png")));
    return false;
  }
  if(!evidence.isEmpty()&&!viewport->grabFramebuffer().save(evidenceFile(QStringLiteral("calibrated-auto-follow.png"))))return false;
  list->setCurrentRow(0);
  list->setCurrentRow(1);
  if(!check(wait([&]{return paints(secondRaster);}),"choosing a photo in the linked list changes comparison camera"))return false;
  const auto rawRef=viewport->cameraPhotoReference(1);
  const auto wrongRaster=readCameraPhoto(rawRef,QSize(64,48),{},QSize(64,47));
  if(!check(wrongRaster.status==CameraPhotoStatus::InvalidCalibration&&wrongRaster.pixels.isNull(),
            "even successfully hashed photo bytes cannot overlay with a different calibrated raster size"))return false;
  registered.fill(Qt::yellow);
  if(!check(registered.save(registeredPath(1))&&wait([&]{return !paints(secondRaster)&&!paints(Qt::yellow);}),
            "replacement pixels invalidate the comparison without replacing the verified photo"))return false;
  overlay->trigger();
  return check(viewport->viewTarget()==oldTarget&&viewport->viewOrbitAngles()==oldAngles&&
               viewport->orthographicProjection()==oldProjection&&viewport->modelTranslation()==transform&&
               !lock->isChecked(),"closing the overlay restores navigation without changing the model");
}

bool completedCheckpointPhotos(const QString &fixtures, const QString &python,
                               const QString &evidence, const QString &backend) {
  QTemporaryDir project(QDir(fixtures).filePath(QStringLiteral("cp-XXXXXX")));
  if (!project.isValid()) return false;
  const auto file = project.filePath(QStringLiteral("checkpoint.gsw.json"));
  const QDir root(WorkspaceDocument::projectDataRootForFile(file));
  const auto scene = QStringLiteral("completed-protected-") + backend;
  const auto dataset = root.filePath(QStringLiteral("data/owned"));
  const auto unrelated = root.filePath(QStringLiteral("data/current"));
  const auto output = root.filePath(QStringLiteral("out/task"));
  const auto outputStep = QDir(output).filePath(QStringLiteral("point_cloud/iteration_10000"));
  const auto group = QStringLiteral("task-") + QString::fromLatin1(
      QCryptographicHash::hash(scene.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
  const QDir checkpoint(root.filePath(QStringLiteral(".gsw/checkpoints/training/%1/iteration_10000").arg(group)));
  const auto model = checkpoint.filePath(QStringLiteral("point_cloud.ply"));
  const auto internal = QDir(outputStep).filePath(QStringLiteral("point_cloud.ply"));
  const auto name = testPhotoName(1);
  const auto archive = QStringLiteral("source/originals/") + name;
  const auto registered = QStringLiteral("images/") + name;
  const QDir images(dataset);
  const QColor archiveInk(207, 43, 141), registeredInk(18, 37, 229);
  QImage photo(64, 48, QImage::Format_RGB32);
  const auto savePhoto = [&](const QString &absolute, const QColor &ink) {
    photo.fill(ink);
    return QDir().mkpath(QFileInfo(absolute).absolutePath()) && photo.save(absolute);
  };
  QByteArray splats = "ply\nformat ascii 1.0\nelement vertex 4\n"
      "property float x\nproperty float y\nproperty float z\n"
      "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
      "property float opacity\nproperty float scale_0\nproperty float scale_1\n";
  const bool volume = backend == QStringLiteral("3dgs");
  if (volume) splats += "property float scale_2\n";
  splats += "property float rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\nend_header\n";
  for (const auto *position : {"0 0 4", "0.4 0 4", "0 0.4 4", "0 0 4.4"}) {
    splats += position;
    splats += " 0 0 0 1 -3 -3";
    if (volume) splats += " -3";
    splats += " 1 0 0 0\n";
  }
  QJsonArray cameras;
  for (int i = 0; i < 2; ++i) {
    cameras.append(QJsonObject{{"id",i},{"img_name",testPhotoName(i)},{"width",64},{"height",48},
        {"fx",50},{"fy",55},{"position",QJsonArray{0,i*.45,0}},
        {"rotation",QJsonArray{QJsonArray{0,1,0},QJsonArray{-1,0,0},QJsonArray{0,0,1}}}});
  }
  const auto outputCameras = QDir(output).filePath(QStringLiteral("cameras.json"));
  if (!check(fixture(dataset) && QDir().mkpath(outputStep) && QDir().mkpath(checkpoint.absolutePath()) &&
             savePhoto(images.filePath(archive), archiveInk) &&
             savePhoto(images.filePath(registered), registeredInk) &&
             savePhoto(images.filePath(QStringLiteral("input/") + name), Qt::yellow) &&
             savePhoto(QDir(unrelated).filePath(archive), Qt::cyan) && write(internal, splats) &&
             QFile::copy(internal, model) && write(outputCameras, QJsonDocument(cameras).toJson()) &&
             QFile::copy(outputCameras, checkpoint.filePath(QStringLiteral("cameras.json"))),
             "build a disposable protected Gaussian result and three distinguishable photo roles")) return false;
  const QJsonObject media{{"kind","image"},{"relativePath",name},{"archivePath",archive},
      {"trainingPath",registered},{"byteSize",QFileInfo(images.filePath(archive)).size()}};
  if (!write(images.filePath(QStringLiteral("source/metadata_manifest.json")),
             QJsonDocument(QJsonObject{{"version",1},{"files",QJsonArray{media}}}).toJson())) return false;
  WorkspaceDocument document;
  QString error;
  if (!check(document.create(root.absolutePath(), &error) && document.setDatasetPath(unrelated, &error) &&
             document.publishGeneratedScene(model, scene, &error) && document.save(file, &error),
             "save the exact protected result while another project dataset is selected")) return false;
  GenerationExperiment owner;
  owner.id = QStringLiteral("checkpoint-owner");
  owner.pipeline = QStringLiteral("training");
  owner.backend = backend;
  owner.status = QStringLiteral("completed");
  owner.datasetPath = dataset;
  owner.outputRoot = output;
  owner.resultPath = model;
  owner.resultSceneId = scene;
  owner.displayName = owner.id;
  owner.createdAt = owner.updatedAt = QDateTime::currentDateTimeUtc();
  owner.parameters = QJsonObject{{"nativeCheckpoint",true},{"posePolicy","solve"},
    {"resultDeliveryOutcome",QJsonObject{{"training_status","done"},{"export_status","failed"},
      {"association_status","ready"},{"iteration",10000},{"internal_model_path",internal},
      {"reason","camera_scale_export_metadata_missing"}}}};
  GenerationHistoryStore history(root.absolutePath());
  if (!check(history.upsert(owner), "record native completion separately from the failed export")) return false;
  const auto before = snapshot(dataset);
  const auto savedModel = snapshot(checkpoint.absolutePath());
  const auto previousPrefix = qgetenv("GAUSSIAN_SPLATTING_CONDA_PREFIX");
  const auto restorePrefix = qScopeGuard([previousPrefix] {
    if (previousPrefix.isNull()) qunsetenv("GAUSSIAN_SPLATTING_CONDA_PREFIX");
    else qputenv("GAUSSIAN_SPLATTING_CONDA_PREFIX",previousPrefix);
  });
  qputenv("GAUSSIAN_SPLATTING_CONDA_PREFIX",QFileInfo(python).absolutePath().toLocal8Bit());

  const auto reopen = [&](bool ambiguous) {
    MainWindow window;
    window.resize(1050,730);
    window.show();
    auto *viewport = window.findChild<NativeViewport *>();
    auto *gallery = window.findChild<QAction *>(QStringLiteral("cameraPhotoListAction"));
    auto *list = window.findChild<QListWidget *>(QStringLiteral("cameraPhotosList"));
    auto *pixels = window.findChild<QLabel *>(QStringLiteral("cameraPhotoPixels"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("cameraPhotoStatus"));
    auto *original = window.findChild<QPushButton *>(QStringLiteral("cameraPhotoOriginal"));
    auto *overlay = window.findChild<QAction *>(QStringLiteral("showCalibratedPhotoAction"));
    auto *opaque = window.findChild<QAction *>(QStringLiteral("calibratedPhotoOpacity100Action"));
    if (!check(viewport && gallery && list && pixels && status && original && overlay && opaque &&
         window.openProjectFile(file) && wait([&]{
           return viewport->cameraCount()==2 && !viewport->displayingProcessingPreview() &&
                  viewport->gaussianRenderingAvailable() && overlay->isEnabled();
         }), "reopened protected completion displays its owned camera poses")) return false;
    if (!list->isVisible()) gallery->trigger();
    const auto select = [&](const QString &requested) {
      const auto matches = list->findItems(requested,Qt::MatchExactly);
      if (matches.size()!=1) return false;
      list->setCurrentItem(matches[0]);
      return true;
    };
    if (!check(wait([&]{return list->isVisible() && select(name);}),
               "select the completed checkpoint camera by its full relative name")) return false;
    const auto originalPixels = [&] {
      return original->isEnabled() && !pixels->pixmap().isNull() &&
          pixels->pixmap().toImage().pixelColor(0,0)==archiveInk;
    };
    const auto overlayPixels = [&] {
      const auto frame = viewport->grabFramebuffer();
      if (frame.isNull()) return false;
      const QSizeF fit = QSizeF(64,48).scaled(QSizeF(frame.size()),Qt::KeepAspectRatio);
      int count=0, total=0;
      for (int v=7;v<43;v+=2) for (int u=7;u<59;u+=2) {
        const auto c=frame.pixelColor(int((frame.width()-fit.width())/2+fit.width()*u/64.),
                                      int((frame.height()-fit.height())/2+fit.height()*v/48.));
        count += std::abs(c.red()-registeredInk.red())<=2 &&
                 std::abs(c.green()-registeredInk.green())<=2 &&
                 std::abs(c.blue()-registeredInk.blue())<=2;
        ++total;
      }
      return count*4>=total*3;
    };
    const auto unlinked = [&] {
      QElapsedTimer stable; stable.start();
      return wait([&] {
        if (original->isEnabled() || !pixels->pixmap().isNull() ||
            status->text()!=cameraPhotoStatusText(CameraPhotoStatus::Unlinked) || overlayPixels()) {
          stable.restart();
          return false;
        }
        return stable.elapsed()>=400;
      });
    };
    if (ambiguous) {
      opaque->trigger();
      overlay->setChecked(true);
      return check(unlinked(), "competing protected completion leaves photos unlinked");
    }
    if (!check(wait(originalPixels), "completed checkpoint opens its verified archived original")) return false;
    original->click();
    if (!check(wait(originalPixels), "View Original reads the archive, not input, registered or current-project pixels")) return false;
    if (!evidence.isEmpty() && !pixels->pixmap().save(QDir(evidence).filePath(
            QStringLiteral("completed-checkpoint-%1-original.png").arg(backend)))) return false;
    opaque->trigger();
    overlay->setChecked(true);
    if (!check(wait([&]{return overlayPixels() && originalPixels();}),
               "completed checkpoint overlays the distinct registered picture after reopen")) return false;
    if (!evidence.isEmpty() && !viewport->grabFramebuffer().save(QDir(evidence).filePath(
            QStringLiteral("completed-checkpoint-%1-overlay.png").arg(backend)))) return false;
    if (!check(select(testPhotoName(0)) && unlinked(),
               "same-basename camera without an owning import record cannot borrow old original or overlay")) return false;
    overlay->setChecked(false);
    std::fprintf(stderr,"Reconstruction review PASS: completed protected %s original and registered pixels after reopen\n",
                 qPrintable(backend));
    std::fflush(stderr);
    return true;
  };
  if (!reopen(false)) return false;
  if (volume) {
    auto duplicate = owner;
    duplicate.id = QStringLiteral("competing-checkpoint-owner");
    if (!check(history.upsert(duplicate),"record a competing terminal owner for the same result") ||
        !reopen(true)) return false;
  }
  return check(!before.isEmpty() && snapshot(dataset)==before && snapshot(checkpoint.absolutePath())==savedModel,
               "reopened checkpoint photo review leaves its source images and model bytes unchanged");
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
      mainCameraPhotos(fixtureRoot,python,output) &&
      completedCheckpointPhotos(fixtureRoot,python,output,QStringLiteral("3dgs")) &&
      completedCheckpointPhotos(fixtureRoot,python,output,QStringLiteral("2dgs"));
}
}
