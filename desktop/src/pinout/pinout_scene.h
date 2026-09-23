#pragma once

#include "pin_assignment_model.h"

#include <QColor>
#include <QGraphicsScene>

class QWidget;

// CONTRACT (owned by the integrator; implement in pinout_scene.cpp, do not change
// signatures without flagging it in your report).
namespace PinoutStyle {
// One fixed colour per FunctionKind, readable on the dark StudioTheme canvas.
// Unassigned uses the existing neutral lead look (StudioTheme "surface3").
QColor colorFor(Hypr::FunctionKind kind);
// Legend caption, e.g. "GPIO", "UART", "SPI", "I²C", "PWM", "ADC", "Unassigned".
QString labelFor(Hypr::FunctionKind kind);
// Legend panel: one swatch + caption per FunctionKind in enum order.
// objectName "pinLegend"; each swatch row carries property "kind" = kindName().
QWidget *createLegend(QWidget *parent = nullptr);
}

// Draws the logical pinout for model->board() (same geometry as the former
// HardwareView::refresh: 760x760 scene, chip body, pins spread over 4 sides).
// Rules the rest of the app depends on:
//  - Exactly one selectable item per pin (the lead) with data(0) = pin name and
//    data(1) = "pin". No other item may set data(0) (legend, labels, grid).
//  - Lead fill = PinoutStyle::colorFor(model->kindFor(pin)); tooltip lists the
//    descriptor functions and the current assignment.
//  - Hover highlights the lead; selection draws an accent outline. Both must
//    survive recolouring (recolour in place, never recreate items on change).
//  - Listens to model assignmentChanged (recolour that lead only) and reset()
//    (full rebuild). No board -> muted "Select a target board above" text.
//  - The scene never changes assignments itself.
class PinoutScene : public QGraphicsScene {
public:
    explicit PinoutScene(Hypr::PinAssignmentModel *model, QObject *parent = nullptr);
    void rebuild();
    // Lead item for a pin, or nullptr.
    QGraphicsItem *pinItem(const QString &pin) const;
private:
    Hypr::PinAssignmentModel *model;
    QMap<QString, QGraphicsItem *> leads;
    void recolor(const QString &pin);
};
