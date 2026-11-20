#include "ReconstructionReviewDialog.h"
#include "AppLanguage.h"
#include "NativeViewport.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace gsw {
namespace {
QString text(const char *s) { return QCoreApplication::translate("Workbench",s); }
QString stateText(const QString &s) {
  if(s==QStringLiteral("registered"))return text(AppLanguage::source("已注册"));
  if(s==QStringLiteral("unregistered"))return text(AppLanguage::source("未注册（原因未知）"));
  if(s==QStringLiteral("posed"))return text(AppLanguage::source("已知位姿（SfM 注册率不适用）"));
  if(s==QStringLiteral("missing"))return text(AppLanguage::source("原图缺失或原始清单不可核对"));
  if(s==QStringLiteral("conflict"))return text(AppLanguage::source("图像身份冲突"));
  return text(AppLanguage::source("不可核实"));
}
QString count(const QJsonValue &v) { return v.isDouble()?QString::number(v.toInt()):text(AppLanguage::source("不可用")); }
QString membership(const QJsonValue &v) { return v.isBool()?(v.toBool()?text(AppLanguage::source("是")):text(AppLanguage::source("否"))):text(AppLanguage::source("未知")); }
QString reasonText(const QString &code) {
  if(code==QStringLiteral("too_few_views"))return text(AppLanguage::source("注册照片过少"));
  if(code==QStringLiteral("too_few_points"))return text(AppLanguage::source("有效稀疏点不足"));
  if(code==QStringLiteral("invalid_points"))return text(AppLanguage::source("稀疏点或轨迹无效"));
  if(code==QStringLiteral("low_coverage"))return text(AppLanguage::source("注册覆盖不足"));
  if(code==QStringLiteral("input_mismatch"))return text(AppLanguage::source("输入照片数量不匹配"));
  return text(AppLanguage::source("模型文件无效"));
}
bool containedFile(const QString &root,const QString &relative) {
  if(relative.isEmpty()||relative.contains(QLatin1Char('\\'))||relative.contains(QLatin1Char(':'))||QDir::isAbsolutePath(relative))return false;
  QString ancestor=QDir(root).absolutePath();
  for(;;) {
    const QFileInfo info(ancestor);if(info.isSymLink())return false;
#ifdef Q_OS_WIN
    const DWORD attributes=GetFileAttributesW(reinterpret_cast<LPCWSTR>(ancestor.utf16()));
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
    const auto parent=info.absoluteDir().absolutePath();if(parent==ancestor)break;ancestor=parent;
  }
  if(QDir(root).canonicalPath().compare(QDir::cleanPath(root),Qt::CaseInsensitive)!=0)return false;
  QString path=root;
  for(const auto &part:relative.split(QLatin1Char('/'))) {
    if(part.isEmpty()||part==QStringLiteral(".")||part==QStringLiteral(".."))return false;
    path=QDir(path).filePath(part);const QFileInfo info(path);if(info.isSymLink())return false;
#ifdef Q_OS_WIN
    const DWORD a=GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
  }
  return QFileInfo(path).isFile()&&QFileInfo(path).canonicalFilePath().startsWith(QDir(root).canonicalPath()+QLatin1Char('/'),Qt::CaseInsensitive);
}
class Filter final : public QSortFilterProxyModel {
public:
  QString status, search;
  void apply(){invalidateFilter();}
  bool filterAcceptsRow(int row,const QModelIndex &parent)const override {
    const auto item=sourceModel()->index(row,0,parent).data(Qt::UserRole).toJsonObject();
    const QString s=item.value(QStringLiteral("status")).toString();
    return (status.isEmpty()||s==status||(status==QStringLiteral("abnormal")&&s!=QStringLiteral("registered")&&s!=QStringLiteral("unregistered")&&s!=QStringLiteral("posed")))&&
        item.value(QStringLiteral("name")).toString().contains(search,Qt::CaseInsensitive);
  }
};
}
class ReconstructionReviewDialog::State {
public:
  QString dataset,python,backend,camera,identity,modelDirectory;QProcessEnvironment environment;
  QJsonObject report;QProcess *process=nullptr;quint64 generation=0,imageGeneration=0;
  QThreadPool *imagePool=nullptr;std::shared_ptr<std::atomic<quint64>> imageEpoch=std::make_shared<std::atomic<quint64>>(0);
  QByteArray output; bool pending=false,expired=false;
  QLabel *summary=nullptr,*groupInfo=nullptr,*details=nullptr,*image=nullptr;
  QComboBox *groups=nullptr,*filter=nullptr;QLineEdit *search=nullptr;
  QTableView *table=nullptr;QStandardItemModel *model=nullptr;Filter *proxy=nullptr;
  QPushButton *locate=nullptr,*photo=nullptr,*restore=nullptr;NativeViewport *viewport=nullptr;QSplitter *split=nullptr;
};
ReconstructionReviewDialog::ReconstructionReviewDialog(const QString &dataset,const QString &python,const QString &backend,
    const QProcessEnvironment &environment,const QString &camera,const QString &identity,QWidget *parent)
    :QDialog(parent),m(std::make_unique<State>()) {
  m->dataset=dataset;m->python=python;m->backend=backend;m->environment=environment;m->camera=camera;m->identity=identity;
  m->imagePool=new QThreadPool(this);m->imagePool->setMaxThreadCount(1);
  setObjectName(QStringLiteral("reconstructionReviewDialog"));
  AppLanguage::bind(this,"windowTitle",AppLanguage::source("重建质检（只读）"));resize(1080,740);setMinimumSize(360,300);
  auto *layout=new QVBoxLayout(this);
  auto *scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
  auto *body=new QWidget(scroll);auto *bodyLayout=new QVBoxLayout(body);scroll->setWidget(body);layout->addWidget(scroll,1);
  m->summary=new QLabel(body);m->summary->setObjectName(QStringLiteral("reviewSummary"));m->summary->setTextFormat(Qt::PlainText);m->summary->setWordWrap(true);bodyLayout->addWidget(m->summary);
  m->groups=new QComboBox(body);m->groups->setObjectName(QStringLiteral("reviewGroups"));m->groups->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m->groups->setMinimumContentsLength(10);bodyLayout->addWidget(m->groups);
  m->groupInfo=new QLabel(body);m->groupInfo->setWordWrap(true);m->groupInfo->setTextFormat(Qt::PlainText);bodyLayout->addWidget(m->groupInfo);
  m->filter=new QComboBox(body);m->filter->setObjectName(QStringLiteral("reviewFilter"));
  for(const auto &id:{QString(),QStringLiteral("registered"),QStringLiteral("unregistered"),QStringLiteral("abnormal")})m->filter->addItem({},id);
  bodyLayout->addWidget(m->filter);
  m->search=new QLineEdit(body);m->search->setObjectName(QStringLiteral("reviewSearch"));AppLanguage::bind(m->search,"placeholderText",AppLanguage::source("按完整相对文件名搜索"));bodyLayout->addWidget(m->search);
  m->split=new QSplitter(Qt::Horizontal,body);bodyLayout->addWidget(m->split,1);
  m->table=new QTableView(m->split);m->table->setObjectName(QStringLiteral("reviewImages"));m->table->setSelectionBehavior(QAbstractItemView::SelectRows);m->table->setSelectionMode(QAbstractItemView::SingleSelection);
  m->table->setEditTriggers(QAbstractItemView::NoEditTriggers);m->model=new QStandardItemModel(this);m->model->setColumnCount(6);
  m->proxy=new Filter;m->proxy->setParent(this);m->proxy->setSourceModel(m->model);m->proxy->setDynamicSortFilter(false);m->table->setModel(m->proxy);
  m->table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);m->table->horizontalHeader()->setStretchLastSection(true);m->table->setColumnWidth(0,230);
  auto *right=new QWidget(m->split);auto *rightLayout=new QVBoxLayout(right);
  m->image=new QLabel(right);m->image->setObjectName(QStringLiteral("reviewThumbnail"));m->image->setAlignment(Qt::AlignCenter);m->image->setMinimumHeight(120);m->image->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);rightLayout->addWidget(m->image);
  m->details=new QLabel(right);m->details->setObjectName(QStringLiteral("reviewDetails"));m->details->setTextFormat(Qt::PlainText);m->details->setWordWrap(true);rightLayout->addWidget(m->details);
  m->viewport=new NativeViewport(right);m->viewport->setObjectName(QStringLiteral("reviewSparseViewport"));m->viewport->setEditToolsLocked(true);m->viewport->setMinimumSize(220,180);rightLayout->addWidget(m->viewport,1);
  auto *notice=AppLanguage::text(new QLabel(body),AppLanguage::source("独立稀疏预览；不会替换主模型。注册比例不是完整度或几何精度。未注册原因未记录时为未知；逐图误差不可用。"));notice->setWordWrap(true);bodyLayout->addWidget(notice);
  auto *buttons=new QGridLayout;layout->addLayout(buttons);
  m->photo=AppLanguage::text(new QPushButton(this),AppLanguage::source("查看原图"));m->photo->setObjectName(QStringLiteral("reviewOpenPhoto"));buttons->addWidget(m->photo,0,0);
  m->locate=AppLanguage::text(new QPushButton(this),AppLanguage::source("定位相机"));m->locate->setObjectName(QStringLiteral("reviewLocateCamera"));buttons->addWidget(m->locate,0,1);
  m->restore=AppLanguage::text(new QPushButton(this),AppLanguage::source("返回原视角"));m->restore->setObjectName(QStringLiteral("reviewRestoreView"));buttons->addWidget(m->restore,1,0);
  auto *reload=AppLanguage::text(new QPushButton(this),AppLanguage::source("重新检查"));buttons->addWidget(reload,1,1);
  auto *close=AppLanguage::text(new QPushButton(this),AppLanguage::source("关闭"));buttons->addWidget(close,2,0,1,2);
  connect(close,&QPushButton::clicked,this,&QDialog::reject);connect(reload,&QPushButton::clicked,this,[this]{inspect(m->groups->currentData().toString());});
  connect(m->groups,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{inspect(m->groups->currentData().toString());});
  connect(m->filter,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{m->proxy->status=m->filter->currentData().toString();m->proxy->apply();selectCurrent();});
  connect(m->search,&QLineEdit::textChanged,this,[this](const QString &s){m->proxy->search=s;m->proxy->apply();selectCurrent();});
  connect(m->table->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this]{selectCurrent();});
  connect(m->table,&QTableView::doubleClicked,this,[this]{locate();});connect(m->locate,&QPushButton::clicked,this,&ReconstructionReviewDialog::locate);
  connect(m->restore,&QPushButton::clicked,m->viewport,&NativeViewport::restoreSourceCameraView);connect(m->photo,&QPushButton::clicked,this,[this]{loadImage(true);});
  AppLanguage::onChanged(this,[this]{refreshLanguage();});
  auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this]{if(!m->pending&&!m->report.isEmpty()&&stale())invalidate();});timer->start(2000);
  refreshLanguage();QTimer::singleShot(0,this,[this]{inspect();});
}
ReconstructionReviewDialog::~ReconstructionReviewDialog(){cancel();}
QJsonObject ReconstructionReviewDialog::report()const{return m->report;}
bool ReconstructionReviewDialog::busy()const{return m->pending;}
void ReconstructionReviewDialog::cancel(){++m->generation;++m->imageGeneration;m->imageEpoch->store(m->imageGeneration);m->imagePool->clear();m->pending=false;if(m->process){m->process->disconnect(this);m->process->kill();m->process->deleteLater();m->process=nullptr;}}
void ReconstructionReviewDialog::hideEvent(QHideEvent *e){cancel();m->viewport->restoreSourceCameraView();QDialog::hideEvent(e);}
void ReconstructionReviewDialog::resizeEvent(QResizeEvent *e){m->split->setOrientation(width()<760?Qt::Vertical:Qt::Horizontal);QDialog::resizeEvent(e);}
bool ReconstructionReviewDialog::stale()const {
  const QString root=m->report.value(QStringLiteral("dataset")).toString();
  for(const auto &value:m->report.value(QStringLiteral("snapshots")).toArray()) {
    const auto s=value.toObject();const QFileInfo file(QDir(root).filePath(s.value(QStringLiteral("path")).toString()));
    if(s.value(QStringLiteral("missing")).toBool()){if(file.exists())return true;continue;}
    if(s.value(QStringLiteral("directory")).toBool())continue;
    if(!file.isFile()||file.isSymLink()||file.size()!=qint64(s.value(QStringLiteral("size")).toDouble())||file.lastModified().toMSecsSinceEpoch()!=s.value(QStringLiteral("modifiedNs")).toString().toLongLong()/1000000)return true;
  }
  const auto directories=m->report.value(QStringLiteral("directories")).toObject();
  for(auto i=directories.begin();i!=directories.end();++i)if(QFileInfo(QDir(root).filePath(i.key())).lastModified().toMSecsSinceEpoch()!=i.value().toString().toLongLong()/1000000)return true;
  return false;
}
void ReconstructionReviewDialog::invalidate(){m->expired=true;++m->imageGeneration;m->imageEpoch->store(m->imageGeneration);m->imagePool->clear();m->image->clear();m->locate->setEnabled(false);m->photo->setEnabled(false);m->viewport->restoreSourceCameraView();m->viewport->setEnabled(false);refreshLanguage();}
void ReconstructionReviewDialog::inspect(const QString &group){m->expired=false;request(group);}
void ReconstructionReviewDialog::setModelDirectory(const QString &directory){cancel();m->modelDirectory=directory;m->report={};}
void ReconstructionReviewDialog::request(const QString &group,const QString &action,const QString &rowId) {
  cancel();m->pending=true;m->output.clear();m->locate->setEnabled(false);m->photo->setEnabled(false);m->viewport->setEnabled(false);m->table->setEnabled(false);m->groupInfo->clear();m->details->clear();refreshLanguage();
  const auto generation=m->generation;const QString expected=m->report.value(QStringLiteral("identity")).toString();
  auto *process=new QProcess(this);m->process=process;process->setProcessEnvironment(m->environment);process->setWorkingDirectory(m->backend);
  QStringList args{QStringLiteral("-B"),QDir(m->backend).filePath(QStringLiteral("native/worker/reconstruction_review.py")),QStringLiteral("--backend-root"),m->backend,QStringLiteral("--dataset"),m->dataset};
  if(!group.isEmpty())args<<QStringLiteral("--group")<<group;
  if(!m->camera.isEmpty())args<<QStringLiteral("--camera-dataset")<<m->camera;
  if(!m->identity.isEmpty())args<<QStringLiteral("--expected-identity")<<m->identity;
  if(!m->modelDirectory.isEmpty())args<<QStringLiteral("--model-directory")<<m->modelDirectory;
  process->setProgram(m->python);process->setArguments(args);
  connect(process,&QProcess::readyReadStandardOutput,this,[this,process,generation]{if(generation!=m->generation)return;m->output+=process->readAllStandardOutput();if(m->output.size()>32*1024*1024){cancel();invalidate();emit inspectionFinished();}});
  connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
  connect(process,&QProcess::errorOccurred,this,[this,process,generation](QProcess::ProcessError e){if(e==QProcess::FailedToStart&&generation==m->generation&&process==m->process){cancel();m->report={};invalidate();emit inspectionFinished();}});
  connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process,generation,expected,action,rowId](int code,QProcess::ExitStatus status){
    if(generation!=m->generation||process!=m->process)return;
    m->output+=process->readAllStandardOutput();m->pending=false;m->process=nullptr;process->deleteLater();
    const auto report=m->output.size()<=32*1024*1024?QJsonDocument::fromJson(m->output).object():QJsonObject();
    if(code!=0||status!=QProcess::NormalExit||report.value(QStringLiteral("schema"))!=QStringLiteral("gsw.reconstruction_review/1")||!report.value(QStringLiteral("ready")).toBool()){
      m->report=report;invalidate();emit inspectionFinished();return;
    }
    if(!action.isEmpty()) {
      if(report.value(QStringLiteral("identity")).toString()!=expected){invalidate();emit inspectionFinished();return;}
      m->viewport->setEnabled(true);m->table->setEnabled(true);
      if(action==QStringLiteral("locate")) (void)m->viewport->setReviewCameraView(rowId);
      selectCurrent();emit inspectionFinished();return;
    }
    m->report=report;m->expired=false;m->image->clear();m->model->removeRows(0,m->model->rowCount());
    {QSignalBlocker block(m->groups);m->groups->clear();for(const auto &g:report.value(QStringLiteral("groups")).toArray()){const auto v=g.toObject();m->groups->addItem(v.value(QStringLiteral("id")).toString(),v.value(QStringLiteral("id")));}m->groups->setCurrentIndex(m->groups->findData(report.value(QStringLiteral("selectedGroup"))));}
    for(const auto &v:report.value(QStringLiteral("rows")).toArray()) {QList<QStandardItem*> items;for(int i=0;i<6;++i)items.append(new QStandardItem);items[0]->setData(v.toObject(),Qt::UserRole);m->model->appendRow(items);}
    m->proxy->apply();m->table->setEnabled(true);m->viewport->setEnabled(m->viewport->setReconstructionReviewData(report));refreshLanguage();selectCurrent();emit inspectionFinished();
  });
  QTimer::singleShot(30000,process,[this,process,generation]{if(generation==m->generation&&m->process==process){cancel();invalidate();emit inspectionFinished();}});
  process->start();
}
void ReconstructionReviewDialog::refreshLanguage() {
  const char *filters[]={AppLanguage::source("全部照片"),AppLanguage::source("已注册"),AppLanguage::source("未注册（原因未知）"),AppLanguage::source("缺文件或异常")};
  for(int i=0;i<4;++i)m->filter->setItemText(i,text(filters[i]));
  m->model->setHorizontalHeaderLabels({text(AppLanguage::source("照片（相对路径）")),text(AppLanguage::source("注册状态")),text(AppLanguage::source("原始输入")),text(AppLanguage::source("实际训练使用")),text(AppLanguage::source("数据划分")),text(AppLanguage::source("逐图误差（px）"))});
  for(int i=0;i<m->model->rowCount();++i){const auto row=m->model->item(i,0)->data(Qt::UserRole).toJsonObject();
    m->model->item(i,0)->setText(row.value(QStringLiteral("name")).toString());m->model->item(i,1)->setText(stateText(row.value(QStringLiteral("status")).toString()));
    m->model->item(i,2)->setText(membership(row.value(QStringLiteral("original"))));m->model->item(i,3)->setText(membership(row.value(QStringLiteral("training"))));
    m->model->item(i,4)->setText(row.value(QStringLiteral("split")).toString(text(AppLanguage::source("未知"))));m->model->item(i,5)->setText(text(AppLanguage::source("不可用")));}
  if(m->pending){m->summary->setText(text(AppLanguage::source("正在只读检查照片与分组…")));return;}
  if(m->expired){m->summary->setText(text(AppLanguage::source("报告已过期或无法核实来源；请重新检查。定位和图片读取已禁用。"))+QLatin1Char('\n')+
      text(AppLanguage::source("技术诊断（原始标识保留）：%1")).arg(m->report.value(QStringLiteral("diagnostic")).toString()));return;}
  m->summary->setText(text(AppLanguage::source("数据集"))+QStringLiteral(": ")+QDir::toNativeSeparators(m->report.value(QStringLiteral("dataset")).toString())+QLatin1Char('\n')+text(AppLanguage::source("原始输入：%1 · 当前分组注册：%2 · 未注册：%3 · 实际训练使用：%4\n原始清单来源：%5 · 训练清单状态：%6"))
      .arg(count(m->report.value(QStringLiteral("originalCount"))),m->report.value(QStringLiteral("registrationApplicable")).toBool()?count(m->report.value(QStringLiteral("registeredCount"))):text(AppLanguage::source("不适用")),count(m->report.value(QStringLiteral("unregisteredCount"))),count(m->report.value(QStringLiteral("trainingCount"))),m->report.value(QStringLiteral("originalAvailable")).toBool()?m->report.value(QStringLiteral("originalSource")).toString():text(AppLanguage::source("原始清单不可核对")),m->report.value(QStringLiteral("trainingStatus")).toString()==QStringLiteral("not_prepared")?text(AppLanguage::source("尚未准备或不可核实")):text(AppLanguage::source("已验证的训练清单"))));
  for(const auto &g:m->report.value(QStringLiteral("groups")).toArray()){const auto group=g.toObject();if(group.value(QStringLiteral("id"))!=m->report.value(QStringLiteral("selectedGroup")))continue;
    QStringList reasons;for(const auto &r:group.value(QStringLiteral("reasons")).toArray())reasons.append(reasonText(r.toString())+QStringLiteral(" [")+r.toString()+QLatin1Char(']'));
    m->groupInfo->setText(text(AppLanguage::source("查看分组：%1 · 标定：%2 · 稀疏点：%3 · 有效稀疏点平均误差：%4 px\n实际训练分组：%5；查看其他分组不更改训练选择。准入原因（原始标识）：%6"))
        .arg(group.value(QStringLiteral("id")).toString(),count(group.value(QStringLiteral("cameraCount"))),count(group.value(QStringLiteral("sparsePoints"))),group.value(QStringLiteral("meanReprojectionError")).isDouble()?QString::number(group.value(QStringLiteral("meanReprojectionError")).toDouble(),'g',5):text(AppLanguage::source("不可用")),m->report.value(QStringLiteral("actualTrainingGroup")).toString(text(AppLanguage::source("尚未准备或不可核实"))),reasons.isEmpty()?text(AppLanguage::source("最低有效性检查通过（不是重建精度保证）。")):reasons.join(QStringLiteral(" / "))));}
  if(m->report.value(QStringLiteral("groups")).toArray().isEmpty())m->groupInfo->setText(text(AppLanguage::source("尚未重建或来源受限；此窗口不会启动重建、训练或网格生成。")));
  selectCurrent();
}
void ReconstructionReviewDialog::selectCurrent() {
  const auto index=m->proxy->mapToSource(m->table->currentIndex());const auto row=index.isValid()?m->model->item(index.row(),0)->data(Qt::UserRole).toJsonObject():QJsonObject();
  const bool valid=!m->pending&&!m->expired&&!row.isEmpty();
  m->locate->setEnabled(valid&&m->viewport->isEnabled()&&row.value(QStringLiteral("pose")).isObject());m->photo->setEnabled(valid&&row.value(QStringLiteral("imagePath")).isString());
  m->details->setText(row.isEmpty()?QString():text(AppLanguage::source("照片：%1\n%2\n定位：%3\n位姿/路径诊断（原始标识）：%4"))
      .arg(row.value(QStringLiteral("name")).toString(),stateText(row.value(QStringLiteral("status")).toString()),m->locate->isEnabled()?text(AppLanguage::source("在本分组独立预览中定位")):text(AppLanguage::source("无已验证位姿或坐标关系，不能定位")),row.value(QStringLiteral("poseReason")).toString()));
  if(valid&&row.value(QStringLiteral("imagePath")).isString())loadImage(false);
}
void ReconstructionReviewDialog::locate(){if(!m->locate->isEnabled())return;if(stale()){invalidate();return;}const auto row=m->proxy->index(m->table->currentIndex().row(),0).data(Qt::UserRole).toJsonObject();m->viewport->beginReviewNavigation();request(m->report.value(QStringLiteral("selectedGroup")).toString(),QStringLiteral("locate"),row.value(QStringLiteral("id")).toString());}
void ReconstructionReviewDialog::loadImage(bool fullSize) {
  if(m->expired||m->pending)return;
  const auto row=m->proxy->index(m->table->currentIndex().row(),0).data(Qt::UserRole).toJsonObject();
  const QString relative=row.value(QStringLiteral("imagePath")).toString(),root=m->report.value(QStringLiteral("dataset")).toString();if(relative.isEmpty())return;
  QString expected;for(const auto &v:m->report.value(QStringLiteral("snapshots")).toArray()){const auto s=v.toObject();if(s.value(QStringLiteral("path"))==relative)expected=s.value(QStringLiteral("sha256")).toString();}
  const auto generation=++m->imageGeneration;const auto epoch=m->imageEpoch;epoch->store(generation);m->imagePool->clear();auto *watcher=new QFutureWatcher<QImage>(this);
  connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,generation,fullSize,relative]{const auto image=watcher->result();watcher->deleteLater();if(generation!=m->imageGeneration||m->expired||!isVisible())return;
    if(image.isNull()){m->image->setText(text(AppLanguage::source("原图不可读、已变化或超过预览预算。")));return;}
    if(!fullSize){m->image->setPixmap(QPixmap::fromImage(image));return;}
    QDialog viewer(this);viewer.setObjectName(QStringLiteral("reviewOriginalImageDialog"));viewer.setProperty("reviewOriginalPath",relative);viewer.setWindowTitle(relative);auto *layout=new QVBoxLayout(&viewer);auto *label=new QLabel(&viewer);label->setPixmap(QPixmap::fromImage(image));label->setAlignment(Qt::AlignCenter);layout->addWidget(label);viewer.resize(image.size().boundedTo(QSize(1280,900)));viewer.exec();
  });
  watcher->setFuture(QtConcurrent::run(m->imagePool,[root,relative,expected,fullSize,epoch,generation]{
    const auto cancelled=[&]{return epoch->load()!=generation;};if(cancelled())return QImage();
    if(expected.size()!=64||!containedFile(root,relative))return QImage();QFile file(QDir(root).filePath(relative));
    if(!file.open(QIODevice::ReadOnly)||file.size()>256*1024*1024)return QImage();QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()){if(cancelled())return QImage();const auto block=file.read(1024*1024);if(block.isEmpty()&&file.error()!=QFile::NoError)return QImage();hash.addData(block);}
    if(cancelled()||QString::fromLatin1(hash.result().toHex())!=expected||!containedFile(root,relative)||!file.seek(0))return QImage();
    QImageReader reader(&file);reader.setAutoTransform(true);const auto size=reader.size();if(!size.isValid()||qint64(size.width())*size.height()>64000000)return QImage();
    reader.setScaledSize(size.scaled(fullSize?QSize(1280,900):QSize(240,160),Qt::KeepAspectRatio));return reader.read();
  }));
}
}
