#pragma once

#include "clock_tree.h"

#include <QWidget>

// CONTRACT (owned by the integrator; implement in clock_view.cpp).
// Clock Configuration tab. objectName "clockView".
//  - Tree visualisation of the selected board's ClockTree (sources -> mux/PLL/
//    dividers -> outputs), each node showing its computed MHz.
//  - Editable nodes get a QComboBox (objectName "clock:<nodeId>") listing the
//    mux inputs or PLL/divider options; non-editable nodes show value + note.
//  - A "Peripheral clocks" table (objectName "clockOutputs") with one row per
//    ClockTree::peripheralOutputs entry: label, MHz, limit, status.
//  - Errors from computeClocks shown in a label objectName "clockErrors".
//  - Unknown board / no clocks data -> explanatory placeholder, no crash.
class ClockView : public QWidget {
    Q_OBJECT
public:
    explicit ClockView(const QString &catalogPath, QWidget *parent = nullptr);
    ~ClockView() override;
    void setBoard(const QString &boardId);   // resets config to defaults
    QString boardId() const;
    Hypr::ClockConfig config() const;
    // Applies selections (invalid ones dropped); does not emit configChanged.
    void setConfig(const Hypr::ClockConfig &config);
    Hypr::ClockResult result() const;
Q_SIGNALS:
    void configChanged();                     // user edited a clock control
private:
    struct Private;
    Private *d;
};
