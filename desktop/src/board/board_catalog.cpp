#include "board_catalog.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <limits>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace Hypr {
namespace {

[[noreturn]] void invalid(const QString &where, const QString &reason)
{
    throw std::runtime_error((where + ": " + reason).toStdString());
}

QString text(const YAML::Node &node, const QString &where)
{
    if (!node.IsScalar())
        invalid(where, "expected a non-empty string");
    const auto value = QString::fromStdString(node.Scalar());
    // yaml-cpp retains scalar tags rather than resolving their types. Quoted
    // numeric pin names remain strings; implicit numbers/bools are not strings.
    if (node.Tag() != "!" && node.Tag() != "tag:yaml.org,2002:str") {
        bool numeric = false;
        value.toDouble(&numeric);
        static const QRegularExpression nonString(
            "^(?:true|false|yes|no|on|off|null|~|[-+]?\\.(?:inf|nan)|[-+]?0[xob][0-9a-f_]+)$",
            QRegularExpression::CaseInsensitiveOption);
        if (numeric || nonString.match(value).hasMatch()
            || (node.Tag() != "?" && !node.Tag().empty()))
            invalid(where, "expected a string, not a typed scalar");
    }
    if (value.trimmed().isEmpty())
        invalid(where, "expected a non-empty string");
    return value; // Preserve descriptor spelling, including valid quoted strings.
}

void mapping(const YAML::Node &node, const QString &where)
{
    if (!node.IsMap())
        invalid(where, "expected a map");
    QSet<QString> keys;
    for (const auto &entry : node) {
        const auto key = text(entry.first, where + " key");
        if (keys.contains(key))
            invalid(where, "duplicate key '" + key + "'");
        keys.insert(key);
    }
}

void sequence(const YAML::Node &node, const QString &where)
{
    if (!node.IsSequence())
        invalid(where, "expected a sequence");
}

Board parseBoard(const QString &id, const YAML::Node &data)
{
    const auto where = "boards." + id;
    mapping(data, where);
    Board board;
    board.id = id;
    board.name = text(data["name"], where + ".name");
    board.architecture = text(data["architecture"], where + ".architecture");
    const auto clock = data["clock_freq_mhz"];
    const auto clockText = clock.IsScalar() ? QString::fromStdString(clock.Scalar()) : QString();
    static const QRegularExpression integer("^[0-9]+$");
    bool clockOk = false;
    const auto clockValue = clockText.toLongLong(&clockOk);
    if (!integer.match(clockText).hasMatch() || !clockOk || clockValue <= 0
        || clockValue > std::numeric_limits<int>::max())
        invalid(where + ".clock_freq_mhz", "expected a positive integer within int range");
    board.clockMHz = static_cast<int>(clockValue);

    QMap<QString, qsizetype> pinIndices;
    QSet<QString> resourceIds;
    const auto addPin = [&](const QString &name, const QString &function) {
        if (!pinIndices.contains(name)) {
            pinIndices.insert(name, board.pins.size());
            board.pins.append(Pin{name, {}});
        }
        auto &functions = board.pins[pinIndices.value(name)].functions;
        if (!functions.contains(function))
            functions.append(function);
    };
    const auto addResource = [&](const Resource &resource) {
        if (!resourceIds.contains(resource.id)) {
            resourceIds.insert(resource.id);
            board.resources.append(resource);
        }
    };

    const auto pins = data["pins"];
    mapping(pins, where + ".pins");
    for (const auto &type : {QStringLiteral("gpio"), QStringLiteral("pwm"), QStringLiteral("adc")}) {
        const auto entries = pins[type.toStdString()];
        if (!entries)
            continue;
        const auto location = where + ".pins." + type;
        sequence(entries, location);
        const auto role = type == "gpio" ? QStringLiteral("gpio")
            : type == "pwm" ? QStringLiteral("output") : QStringLiteral("input");
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto name = text(entries[i], location + "[" + QString::number(i) + "]");
            addPin(name, type);
            addResource(Resource{type + "." + name, type, {{role, name}}});
        }
    }
    for (const auto &type : {QStringLiteral("spi"), QStringLiteral("i2c"), QStringLiteral("uart")}) {
        const auto instances = pins[type.toStdString()];
        if (!instances)
            continue;
        const auto location = where + ".pins." + type;
        mapping(instances, location);
        for (const auto &instance : instances) {
            const auto instanceName = text(instance.first, location + " instance");
            const auto id = type + "." + instanceName;
            const auto instanceLocation = location + "." + instanceName;
            mapping(instance.second, instanceLocation);
            Resource resource{id, type, {}};
            for (const auto &signal : instance.second) {
                const auto role = text(signal.first, instanceLocation + " role");
                if (role == "devices")
                    continue; // Nested device metadata is not a physical signal.
                const auto name = text(signal.second, instanceLocation + "." + role);
                resource.signals.insert(role, name);
                addPin(name, id + "." + role);
            }
            if (resource.signals.isEmpty())
                invalid(instanceLocation, "expected at least one pin signal");
            addResource(resource);
        }
    }
    if (board.pins.isEmpty())
        invalid(where + ".pins", "expected at least one descriptor pin");
    const auto accelerators = data["accelerators"];
    if (accelerators) {
        sequence(accelerators, where + ".accelerators");
        for (std::size_t i = 0; i < accelerators.size(); ++i) {
            const auto name = text(accelerators[i], where + ".accelerators[" + QString::number(i) + "]").toLower();
            addResource(Resource{"accelerator." + name, "accelerator", {}});
        }
    }
    return board;
}

} // namespace

QList<Board> BoardCatalog::load(const QString &path)
{
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            invalid("file", "cannot open: " + file.errorString());
        const auto contents = file.readAll();
        if (file.error() != QFileDevice::NoError)
            invalid("file", "cannot read: " + file.errorString());
        const auto documents = YAML::LoadAll(contents.toStdString());
        if (documents.empty())
            invalid("root", "expected a map");
        if (documents.size() != 1)
            invalid("document", "expected exactly one YAML document");
        const auto root = documents.front();
        mapping(root, "root");
        const auto entries = root["boards"];
        mapping(entries, "boards");
        if (entries.size() == 0)
            invalid("boards", "expected at least one board");
        QList<Board> boards;
        for (const auto &entry : entries)
            boards.append(parseBoard(text(entry.first, "boards key"), entry.second));
        return boards;
    } catch (const YAML::Exception &error) {
        throw std::runtime_error("BoardCatalog '" + path.toStdString() + "': invalid YAML: " + error.what());
    } catch (const std::runtime_error &error) {
        throw std::runtime_error("BoardCatalog '" + path.toStdString() + "': " + error.what());
    }
}

} // namespace Hypr
