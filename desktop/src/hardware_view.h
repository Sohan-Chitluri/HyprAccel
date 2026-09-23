#pragma once
#include <QWidget>
#include "board_catalog.h"
#include "pin_assignment_model.h"
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
private:
 QList<Hypr::Board> boards;
 QComboBox *selector;
 QGraphicsView *canvas;
 PinoutScene *scene;
 QTreeWidget *resources;
 QLabel *details;
 QLabel *notice;
 QLabel *title;
 Hypr::PinAssignmentModel *model;
 void refresh();
 void updateInspector();
 void updateNotice();
};
