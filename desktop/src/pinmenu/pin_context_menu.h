#pragma once

#include "pin_assignment_model.h"

#include <QString>

class QMenu;
class QWidget;

// CONTRACT (owned by the integrator; implement in pin_context_menu.cpp).
namespace PinMenu {
// Human label for a function: "gpio" -> "GPIO", "pwm" -> "PWM output",
// "adc" -> "ADC input", "uart.uart0.tx" -> "UART0 TX", "spi.hspi.sck" -> "HSPI SCK",
// "i2c.i2c0.sda" -> "I2C0 SDA", "" -> "Unassigned".
QString describe(const QString &function);

// Context menu for one pin. Title section = pin name. Then one checkable action
// per model->validFunctions(pin), grouped by FunctionKind with section headers
// (in FunctionKind enum order), current function checked (exclusive group),
// then a separator and "Reset to unassigned" (disabled if already unassigned).
// Each action has objectName "fn:<function>" (reset: "fn:") and property
// "function". Triggering calls model->assign(pin, function) — nothing else.
// If a bus signal is currently held by another pin, its text gets a suffix
// " (moves from <otherPin>)". Returns nullptr for pins the model doesn't know.
// Caller owns the menu (parent may be nullptr).
QMenu *build(Hypr::PinAssignmentModel *model, const QString &pin, QWidget *parent = nullptr);

// Plain-text Pin Inspector body for the pin:
//   <pin>\n\nASSIGNMENT\n<describe(current)>\n\nAVAILABLE FUNCTIONS\n<one describe() per line, current marked "● ", others "○ ">
//   \n\nRight-click the pin to change its function.\nCapabilities come from boards.yaml, not verified physical wiring.
// Unknown pin -> "".
QString inspectorText(const Hypr::PinAssignmentModel &model, const QString &pin);
}
