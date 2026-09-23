#include "hardware_view.h"
#include "theme.h"
#include "pinout_scene.h"
#include "pin_context_menu.h"
#include <QComboBox>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPainter>
#include <algorithm>

HardwareView::HardwareView(const QString &path, QWidget *parent) : QWidget(parent) {
 auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
 auto *toolbar = new QHBoxLayout; toolbar->setContentsMargins(12,8,12,8);
 toolbar->addWidget(new QLabel("TARGET"));
 selector = new QComboBox; selector->setObjectName("boardSelector"); selector->addItem("Select a board…", "");
 try { boards = Hypr::BoardCatalog::load(path); for(const auto &board:boards) selector->addItem(board.name,board.id); }
 catch(const std::exception &error) { selector->setToolTip(QString::fromUtf8(error.what())); selector->setEnabled(false); }
 toolbar->addWidget(selector); toolbar->addStretch();
 toolbar->addWidget(new QLabel("Descriptor-backed · Right-click a pin to assign")); layout->addLayout(toolbar);
 auto *split = new QSplitter; layout->addWidget(split,1);
 resources = new QTreeWidget; resources->setObjectName("resourceTree"); resources->setHeaderLabel("PERIPHERALS"); resources->setMinimumWidth(180); split->addWidget(resources);
 auto *center = new QWidget; auto *middle = new QVBoxLayout(center); middle->setContentsMargins(0,0,0,0); middle->setSpacing(0);
 auto *tools = new QHBoxLayout; tools->setContentsMargins(12,8,12,8);
 title = new QLabel("Pinout view"); tools->addWidget(title); tools->addStretch();
 model = new Hypr::PinAssignmentModel(this);
 scene = new PinoutScene(model, this);
 canvas = new QGraphicsView(scene); canvas->setObjectName("pinCanvas"); canvas->setRenderHint(QPainter::Antialiasing);
 canvas->setDragMode(QGraphicsView::ScrollHandDrag);
 canvas->setContextMenuPolicy(Qt::CustomContextMenu);
 for(const QString &caption: {QString("−"),QString("+"),QString("Fit")}) {
  auto *button = new QPushButton(caption); button->setFixedWidth(42); tools->addWidget(button);
  connect(button,&QPushButton::clicked,this,[this,caption] { if(caption=="Fit") canvas->fitInView(scene->sceneRect(),Qt::KeepAspectRatio); else canvas->scale(caption=="+"?1.2:1/1.2,caption=="+"?1.2:1/1.2); });
 }
 middle->addLayout(tools); middle->addWidget(canvas,1);
 middle->addWidget(PinoutStyle::createLegend(center));
 notice = new QLabel; notice->setObjectName("geometryNotice"); notice->setWordWrap(true); notice->setContentsMargins(12,8,12,8); middle->addWidget(notice); split->addWidget(center);
 auto *inspector = new QWidget; inspector->setMinimumWidth(230); auto *right = new QVBoxLayout(inspector);
 right->addWidget(new QLabel("PIN INSPECTOR")); details = new QLabel; details->setObjectName("pinDetails"); details->setWordWrap(true); details->setTextInteractionFlags(Qt::TextSelectableByMouse); right->addWidget(details); right->addStretch(); split->addWidget(inspector);
 split->setSizes({240,840,320});
 connect(selector,&QComboBox::currentIndexChanged,this,[this]{refresh();});
 connect(scene,&QGraphicsScene::selectionChanged,this,[this]{updateInspector();});
 connect(model,&Hypr::PinAssignmentModel::assignmentChanged,this,[this](const QString&,const QString&){updateInspector();updateNotice();});
 connect(model,&Hypr::PinAssignmentModel::reset,this,[this]{updateInspector();updateNotice();});
 connect(canvas,&QGraphicsView::customContextMenuRequested,this,[this](const QPoint &pos){
  QGraphicsItem *item = canvas->itemAt(pos);
  while(item && item->data(1).toString() != "pin") item = item->parentItem();
  if(!item) return;
  const QString pin = item->data(0).toString();
  scene->clearSelection();
  item->setSelected(true);
  QMenu *menu = PinMenu::build(model, pin, canvas);
  if(!menu) return;
  menu->exec(canvas->mapToGlobal(pos));
  delete menu;
 });
 connect(resources,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int){
  const QString pin=item->data(0,Qt::UserRole).toString();
  if(pin.isEmpty()) return;
  if(auto *lead = scene->pinItem(pin)) { scene->clearSelection(); lead->setSelected(true); canvas->ensureVisible(lead); }
 });
 refresh();
}
HardwareView::~HardwareView() {
 // model and scene are children of `this` too, and their signals drive
 // updateInspector()/updateNotice(), which touch other children (details,
 // notice) nested under `split`. Qt tears children down in the order they
 // were added, so `split` (and details/notice with it) can be destroyed
 // before scene/model; letting scene's own teardown still fire
 // selectionChanged into an already-destroyed `details` would crash.
 // Disconnect before any child destruction starts.
 disconnect(scene, nullptr, this, nullptr);
 disconnect(model, nullptr, this, nullptr);
}
QString HardwareView::boardId() const {return selector->currentData().toString();}
bool HardwareView::selectBoard(const QString &id) {int index=selector->findData(id); if(index<0)return false; selector->setCurrentIndex(index); return true;}
Hypr::PinAssignmentModel *HardwareView::pinModel() const { return model; }
void HardwareView::refresh() {
 resources->clear();
 const QString id=boardId();
 const Hypr::Board *board = nullptr;
 if(!id.isEmpty()) {
  const auto found=std::find_if(boards.begin(),boards.end(),[&](const auto &b){return b.id==id;});
  if(found!=boards.end()) board=&(*found);
 }
 model->setBoard(board);
 if(!board) {
  title->setText("Pinout view");
  notice->setText(selector->isEnabled()?"No board selected.":"Could not load board catalog: "+selector->toolTip());
  canvas->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);
  return;
 }
 title->setText(board->name+" · "+QString::number(board->clockMHz)+" MHz");
 updateNotice();
 QMap<QString,QTreeWidgetItem*> groups;
 for(const auto &resource:board->resources) {
  if(!groups.contains(resource.type)) groups[resource.type]=new QTreeWidgetItem(resources,{resource.type.toUpper()});
  auto *item=new QTreeWidgetItem(groups[resource.type],{resource.id});
  for(auto it=resource.signals.begin();it!=resource.signals.end();++it) {
   auto *signal=new QTreeWidgetItem(item,{it.key()+"  →  "+it.value()}); signal->setData(0,Qt::UserRole,it.value());
  }
 }
 resources->expandToDepth(0);
 canvas->fitInView(scene->sceneRect(),Qt::KeepAspectRatio);
}
void HardwareView::updateNotice() {
 const Hypr::Board *board = model->board();
 if(!board) return;
 const int assigned = model->assignments().size();
 notice->setText(QString("Logical pin layout — not a physical package pinout. %1 descriptor pins; physical positions and electrical restrictions require board-specific verification. %2 %3 assigned.")
  .arg(board->pins.size()).arg(assigned).arg(assigned == 1 ? "pin" : "pins"));
}
void HardwareView::updateInspector() {
 const auto selected = scene->selectedItems();
 if(selected.isEmpty()) { details->setText("Select a pin to inspect descriptor capabilities."); return; }
 if(selected.size() > 1) { details->setText("Multiple pins selected. Select one pin to inspect its capabilities."); return; }
 const QString pin = selected.first()->data(0).toString();
 const QString text = PinMenu::inspectorText(*model, pin);
 details->setText(text.isEmpty() ? "Select a pin to inspect descriptor capabilities." : text);
}
