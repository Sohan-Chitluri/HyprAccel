#pragma once
#include <QWidget>
#include "board_catalog.h"
#include "pin_assignment_model.h"
#include "pin_check_runner.h"
class QComboBox;
class QGraphicsView;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class PinoutScene;
class HardwareView : public QWidget {
public:
 explicit HardwareView(const QString &catalogPath, QWidget *parent = nullptr);
 ~HardwareView() override;
 QString boardId() const;
 bool selectBoard(const QString &id);
 Hypr::PinAssignmentModel *pinModel() const;
 // Exposed for tests: the last pin-conflict check result rendered into the
 // Pin Inspector's Issues section (available() == false when unavailable).
 Hypr::PinCheckResult lastPinCheckResult() const { return lastCheck; }
private:
 QList<Hypr::Board> boards;
 QComboBox *selector;
 QGraphicsView *canvas;
 PinoutScene *scene;
 QTreeWidget *resources;
 QLabel *details;
 QLabel *issues;
 QLabel *notice;
 QLabel *title;
 Hypr::PinAssignmentModel *model;
 Hypr::PinCheckRunner *pinCheck = nullptr;
 Hypr::PinCheckResult lastCheck;
 bool hasCheckResult = false; // false until the first check for the current board returns
 void refresh();
 void updateInspector();
 void updateNotice();
 void requestPinCheck();
 void renderIssues();
};
