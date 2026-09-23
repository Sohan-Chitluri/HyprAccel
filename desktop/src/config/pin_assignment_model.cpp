#include "pin_assignment_model.h"

#include <QSet>

namespace Hypr {

FunctionKind kindOf(const QString &function)
{
    const auto type = function.section('.', 0, 0);
    if (type == "gpio") return FunctionKind::Gpio;
    if (type == "uart") return FunctionKind::Uart;
    if (type == "spi") return FunctionKind::Spi;
    if (type == "i2c") return FunctionKind::I2c;
    if (type == "pwm") return FunctionKind::Pwm;
    if (type == "adc") return FunctionKind::Adc;
    return FunctionKind::Unassigned;
}

QString kindName(FunctionKind kind)
{
    switch (kind) {
    case FunctionKind::Gpio: return QStringLiteral("gpio");
    case FunctionKind::Uart: return QStringLiteral("uart");
    case FunctionKind::Spi: return QStringLiteral("spi");
    case FunctionKind::I2c: return QStringLiteral("i2c");
    case FunctionKind::Pwm: return QStringLiteral("pwm");
    case FunctionKind::Adc: return QStringLiteral("adc");
    case FunctionKind::Unassigned: break;
    }
    return QStringLiteral("unassigned");
}

ResourceRole resourceRoleFor(const QString &pin, const QString &function)
{
    if (function == "gpio") return {"gpio." + pin, "gpio"};
    if (function == "pwm") return {"pwm." + pin, "output"};
    if (function == "adc") return {"adc." + pin, "input"};
    const auto parts = function.split('.');
    if (parts.size() == 3) return {parts[0] + "." + parts[1], parts[2]};
    return {};
}

QString functionForResourceRole(const QString &resource, const QString &role)
{
    const auto type = resource.section('.', 0, 0);
    if (type == "gpio" && role == "gpio") return type;
    if (type == "pwm" && role == "output") return type;
    if (type == "adc" && role == "input") return type;
    if ((type == "uart" || type == "spi" || type == "i2c") && resource.count('.') == 1 && !role.isEmpty())
        return resource + "." + role;
    return {};
}

PinAssignmentModel::PinAssignmentModel(QObject *parent) : QObject(parent) {}

void PinAssignmentModel::setBoard(const Board *board)
{
    current = board;
    values.clear();
    Q_EMIT reset();
}

const Board *PinAssignmentModel::board() const { return current; }

const Pin *PinAssignmentModel::findPin(const QString &pin) const
{
    if (!current) return nullptr;
    for (const auto &candidate : current->pins)
        if (candidate.name == pin) return &candidate;
    return nullptr;
}

QStringList PinAssignmentModel::validFunctions(const QString &pin) const
{
    const auto *found = findPin(pin);
    return found ? found->functions : QStringList();
}

QString PinAssignmentModel::functionFor(const QString &pin) const { return values.value(pin); }

FunctionKind PinAssignmentModel::kindFor(const QString &pin) const { return kindOf(functionFor(pin)); }

bool PinAssignmentModel::assign(const QString &pin, const QString &function)
{
    const auto *found = findPin(pin);
    if (!found || (!function.isEmpty() && !found->functions.contains(function)))
        return false;
    if (values.value(pin) == function)
        return true;
    // Bus signals are exclusive; scalar functions are per pin by construction.
    if (function.count('.') == 2) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key() != pin && it.value() == function) {
                const auto previous = it.key();
                values.erase(it);
                Q_EMIT assignmentChanged(previous, QString());
                break;
            }
        }
    }
    if (function.isEmpty()) values.remove(pin);
    else values.insert(pin, function);
    Q_EMIT assignmentChanged(pin, function);
    return true;
}

void PinAssignmentModel::clear(const QString &pin) { assign(pin, QString()); }

QMap<QString, QString> PinAssignmentModel::assignments() const { return values; }

QMap<QString, QString> PinAssignmentModel::setAssignments(const QMap<QString, QString> &assignments)
{
    QMap<QString, QString> dropped;
    QMap<QString, QString> next;
    QSet<QString> busSignals;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const auto *found = findPin(it.key());
        const bool bus = it.value().count('.') == 2;
        if (it.value().isEmpty()) continue;
        if (!found || !found->functions.contains(it.value()) || (bus && busSignals.contains(it.value()))) {
            dropped.insert(it.key(), it.value());
            continue;
        }
        if (bus) busSignals.insert(it.value());
        next.insert(it.key(), it.value());
    }
    values = next;
    Q_EMIT reset();
    return dropped;
}

} // namespace Hypr
