#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QLabel>
#include <QTimer>
#include "hardware_view.h"
class HardwareViewTest : public QObject {
 Q_OBJECT
private Q_SLOTS:
 void selectionUsesRealDescriptor() {
  HardwareView view(QStringLiteral(BOARD_CATALOG_PATH)); view.show();
  auto *selector = view.findChild<QComboBox*>("boardSelector");
  QVERIFY2(selector, "A board selector must exist");
  QCOMPARE(selector->count(), 3);
  selector->setCurrentIndex(selector->findData("esp32"));
  auto *canvas = view.findChild<QGraphicsView*>("pinCanvas"); QVERIFY(canvas);
  QStringList pins;
  for (auto *item : canvas->scene()->items()) if (!item->data(0).toString().isEmpty()) pins << item->data(0).toString();
  QVERIFY(pins.contains("GPIO25")); QVERIFY(pins.contains("GPIO9")); QVERIFY(!pins.contains("PWM7"));
  QCOMPARE(pins.size(), 28);
  QVERIFY(view.findChild<QLabel*>("geometryNotice")->text().contains("not a physical"));
  selector->setCurrentIndex(selector->findData("thejas32"));
  pins.clear(); for(auto *item:canvas->scene()->items()) if(!item->data(0).toString().isEmpty()) pins << item->data(0).toString();
  QVERIFY(pins.contains("PWM7")); QVERIFY(pins.contains("SPI3CLK"));
  for(auto *item:canvas->scene()->items()) if(item->data(0).toString()=="PWM7") item->setSelected(true);
  QVERIFY(view.findChild<QLabel*>("pinDetails")->text().contains("PWM7"));
  selector->setCurrentIndex(0);
  QCOMPARE(view.boardId(), QString());
  QVERIFY(!view.findChild<QLabel*>("pinDetails")->text().contains("PWM7"));
 }
 void clearingSelectionResetsInspector() {
  HardwareView view(QStringLiteral(BOARD_CATALOG_PATH));
  view.selectBoard("thejas32");
  auto *canvas = view.findChild<QGraphicsView*>("pinCanvas"); QVERIFY(canvas);
  for(auto *item:canvas->scene()->items()) if(item->data(0).toString()=="PWM7") item->setSelected(true);
  QVERIFY(view.findChild<QLabel*>("pinDetails")->text().contains("PWM7"));
  canvas->scene()->clearSelection();
  QVERIFY(!view.findChild<QLabel*>("pinDetails")->text().contains("PWM7"));
 }
 void missingCatalogShowsError() {
  HardwareView view("/not/a/real/board-catalog.yaml");
  QVERIFY(!view.findChild<QComboBox*>("boardSelector")->isEnabled());
  QVERIFY(view.findChild<QLabel*>("geometryNotice")->text().contains("Could not load"));
 }
 void assigningViaModelRecoloursWithoutChangingItemCount() {
  HardwareView view(QStringLiteral(BOARD_CATALOG_PATH));
  view.selectBoard("esp32");
  auto *canvas = view.findChild<QGraphicsView*>("pinCanvas"); QVERIFY(canvas);
  QStringList pinsBefore;
  for (auto *item : canvas->scene()->items()) if (!item->data(0).toString().isEmpty()) pinsBefore << item->data(0).toString();
  QCOMPARE(pinsBefore.size(), 28);

  QVERIFY(view.pinModel());
  QVERIFY(view.pinModel()->assign("GPIO1", "uart.uart0.tx"));

  QStringList pinsAfter;
  for (auto *item : canvas->scene()->items()) if (!item->data(0).toString().isEmpty()) pinsAfter << item->data(0).toString();
  QCOMPARE(pinsAfter.size(), pinsBefore.size());

  for (auto *item : canvas->scene()->items()) if (item->data(0).toString() == "GPIO1") item->setSelected(true);
  QVERIFY(view.findChild<QLabel*>("pinDetails")->text().contains("GPIO1"));
  QVERIFY(view.findChild<QLabel*>("pinDetails")->text().contains("UART0 TX"));
  QVERIFY(view.findChild<QLabel*>("geometryNotice")->text().contains("1 pin assigned"));
 }
 void rightClickingPinLabelResolvesThePin() {
  // Each pin's text label is a CHILD QGraphicsTextItem of its lead (data(1) ==
  // "pin" lives on the lead, not the label); the context-menu hit-test must
  // climb parentItem() from whatever itemAt() returns to find it.
  HardwareView view(QStringLiteral(BOARD_CATALOG_PATH));
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  view.selectBoard("esp32");
  auto *canvas = view.findChild<QGraphicsView*>("pinCanvas"); QVERIFY(canvas);

  QGraphicsItem *lead = nullptr;
  for (auto *item : canvas->scene()->items()) if (item->data(0).toString() == "GPIO1") { lead = item; break; }
  QVERIFY(lead);
  QGraphicsTextItem *label = nullptr;
  for (auto *child : lead->childItems()) {
   if (auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(child)) { label = text; break; }
  }
  QVERIFY2(label, "The pin lead must have a text label child");
  QVERIFY(label->data(0).toString().isEmpty()); // confirms the hit-test can't rely on data(0) alone

  const QPoint viewPos = canvas->mapFromScene(label->sceneBoundingRect().center());
  QVERIFY(qgraphicsitem_cast<QGraphicsItem *>(canvas->itemAt(viewPos)) != nullptr);

  QTimer::singleShot(200, qApp, [] { if (auto *popup = QApplication::activePopupWidget()) popup->close(); });
  Q_EMIT canvas->customContextMenuRequested(viewPos);
  QVERIFY2(lead->isSelected(), "Right-clicking the pin's label must select the pin's lead");
 }
};
QTEST_MAIN(HardwareViewTest)
#include "hardware_view_test.moc"
