#include "pin_context_menu.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QPair>
#include <QStringList>

using Hypr::FunctionKind;
using Hypr::PinAssignmentModel;
using Hypr::kindOf;

namespace PinMenu {
namespace {

// Section order and captions, in FunctionKind enum order (Unassigned excluded).
const QList<QPair<FunctionKind, QString>> &sectionOrder()
{
    static const QList<QPair<FunctionKind, QString>> sections = {
        {FunctionKind::Gpio, QStringLiteral("GPIO")},
        {FunctionKind::Uart, QStringLiteral("UART")},
        {FunctionKind::Spi, QStringLiteral("SPI")},
        {FunctionKind::I2c, QStringLiteral("I2C")},
        {FunctionKind::Pwm, QStringLiteral("PWM")},
        {FunctionKind::Adc, QStringLiteral("ADC")},
    };
    return sections;
}

// The pin (if any) other than `self` that currently holds this exact bus
// signal function. Bus signals ("<type>.<instance>.<role>") are exclusive to
// one pin at a time per PinAssignmentModel::assign(); scalar functions
// (gpio/pwm/adc) are per-pin by construction and never trigger this.
QString otherHolderOf(const PinAssignmentModel &model, const QString &self, const QString &function)
{
    if (function.count('.') != 2)
        return QString();
    const auto assignments = model.assignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        if (it.key() != self && it.value() == function)
            return it.key();
    }
    return QString();
}

} // namespace

QString describe(const QString &function)
{
    if (function.isEmpty())
        return QStringLiteral("Unassigned");
    if (function == QStringLiteral("gpio"))
        return QStringLiteral("GPIO");
    if (function == QStringLiteral("pwm"))
        return QStringLiteral("PWM output");
    if (function == QStringLiteral("adc"))
        return QStringLiteral("ADC input");
    const auto parts = function.split(QLatin1Char('.'));
    if (parts.size() == 3)
        return parts.at(1).toUpper() + QLatin1Char(' ') + parts.at(2).toUpper();
    return function;
}

QMenu *build(PinAssignmentModel *model, const QString &pin, QWidget *parent)
{
    if (!model || !model->board())
        return nullptr;
    const auto functions = model->validFunctions(pin);
    if (functions.isEmpty())
        return nullptr;

    auto *menu = new QMenu(parent);
    menu->addSection(pin);

    const auto current = model->functionFor(pin);
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);

    for (const auto &section : sectionOrder()) {
        QStringList inSection;
        for (const auto &function : functions) {
            if (kindOf(function) == section.first)
                inSection.append(function);
        }
        if (inSection.isEmpty())
            continue;
        menu->addSection(section.second);
        for (const auto &function : inSection) {
            QString text = describe(function);
            const auto holder = otherHolderOf(*model, pin, function);
            if (!holder.isEmpty())
                text += QStringLiteral(" (moves from ") + holder + QLatin1Char(')');

            auto *action = new QAction(text, menu);
            action->setObjectName(QStringLiteral("fn:") + function);
            action->setProperty("function", function);
            action->setCheckable(true);
            action->setChecked(function == current);
            group->addAction(action);
            menu->addAction(action);

            QObject::connect(action, &QAction::triggered, model, [model, pin, function]() {
                model->assign(pin, function);
            });
        }
    }

    menu->addSeparator();
    auto *reset = new QAction(QStringLiteral("Reset to unassigned"), menu);
    reset->setObjectName(QStringLiteral("fn:"));
    reset->setProperty("function", QString());
    reset->setEnabled(!current.isEmpty());
    menu->addAction(reset);
    QObject::connect(reset, &QAction::triggered, model, [model, pin]() {
        model->assign(pin, QString());
    });

    return menu;
}

QString inspectorText(const PinAssignmentModel &model, const QString &pin)
{
    const auto functions = model.validFunctions(pin);
    if (functions.isEmpty())
        return QString();

    const auto current = model.functionFor(pin);
    QStringList lines;
    lines << pin
          << QString()
          << QStringLiteral("ASSIGNMENT")
          << describe(current)
          << QString()
          << QStringLiteral("AVAILABLE FUNCTIONS");
    for (const auto &function : functions) {
        const bool isCurrent = function == current;
        lines << (isCurrent ? QStringLiteral("● ") : QStringLiteral("○ ")) + describe(function);
    }
    lines << QString()
          << QStringLiteral("Right-click the pin to change its function.")
          << QStringLiteral("Capabilities come from boards.yaml, not verified physical wiring.");
    return lines.join(QLatin1Char('\n'));
}

} // namespace PinMenu
