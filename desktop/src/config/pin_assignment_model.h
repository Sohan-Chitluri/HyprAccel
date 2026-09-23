#pragma once

#include "board_catalog.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Hypr {

// Function spellings are exactly those in Pin::functions: scalar "gpio", "pwm",
// "adc", or bus signals "<type>.<instance>.<role>" (e.g. "uart.uart0.tx").
// The empty string means unassigned.
enum class FunctionKind { Unassigned, Gpio, Uart, Spi, I2c, Pwm, Adc };

FunctionKind kindOf(const QString &function);
// Stable lowercase name: "unassigned", "gpio", "uart", "spi", "i2c", "pwm", "adc".
QString kindName(FunctionKind kind);

// hardware.json (mbd/editor/server.js projectHardware) resource/role for an
// assignment: gpio -> ("gpio.<pin>", "gpio"), pwm -> ("pwm.<pin>", "output"),
// adc -> ("adc.<pin>", "input"), "uart.uart0.tx" -> ("uart.uart0", "tx").
struct ResourceRole { QString resource; QString role; };
ResourceRole resourceRoleFor(const QString &pin, const QString &function);
// Inverse of resourceRoleFor; returns an empty string for unknown shapes.
QString functionForResourceRole(const QString &resource, const QString &role);

// Single source of truth for pin assignments on the Pinout & Configuration
// screen. The renderer, context menu, inspector and project store all read and
// write through this model; nothing else holds assignment state.
class PinAssignmentModel : public QObject {
    Q_OBJECT
public:
    explicit PinAssignmentModel(QObject *parent = nullptr);

    // Clears all assignments and emits reset(). The board must outlive the
    // model's use of it; nullptr means no board selected.
    void setBoard(const Board *board);
    const Board *board() const;

    // Descriptor functions for the pin, in descriptor order; empty if unknown.
    QStringList validFunctions(const QString &pin) const;
    // Current function, or "" when unassigned.
    QString functionFor(const QString &pin) const;
    FunctionKind kindFor(const QString &pin) const;

    // Assigns a function valid for the pin (or "" to clear). A bus signal can be
    // held by only one pin; assigning it moves it and clears the previous holder
    // (which emits its own assignmentChanged). Returns false and changes
    // nothing when the pin or function is not valid for the current board.
    bool assign(const QString &pin, const QString &function);
    void clear(const QString &pin);

    // pin -> function for assigned pins only.
    QMap<QString, QString> assignments() const;
    // Replaces all assignments, silently dropping invalid entries; emits reset().
    // Returns the entries that were dropped.
    QMap<QString, QString> setAssignments(const QMap<QString, QString> &assignments);

Q_SIGNALS:
    void assignmentChanged(const QString &pin, const QString &function);
    void reset();

private:
    const Board *current = nullptr;
    QMap<QString, QString> values;
    const Pin *findPin(const QString &pin) const;
};

} // namespace Hypr
