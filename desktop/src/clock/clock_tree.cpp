#include "clock_tree.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
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
    // numeric ids/labels remain strings; implicit numbers/bools are not strings.
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
    return value;
}

double number(const YAML::Node &node, const QString &where)
{
    if (!node.IsScalar())
        invalid(where, "expected a number");
    const auto raw = QString::fromStdString(node.Scalar());
    static const QRegularExpression numeric("^[-+]?[0-9]+(\\.[0-9]+)?$");
    if (!numeric.match(raw).hasMatch())
        invalid(where, "expected a number");
    bool ok = false;
    const auto value = raw.toDouble(&ok);
    if (!ok)
        invalid(where, "expected a number");
    return value;
}

bool boolean(const YAML::Node &node, const QString &where, bool defaultValue)
{
    if (!node)
        return defaultValue;
    if (!node.IsScalar())
        invalid(where, "expected true or false");
    const auto raw = QString::fromStdString(node.Scalar()).toLower();
    if (raw == "true")
        return true;
    if (raw == "false")
        return false;
    invalid(where, "expected true or false");
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

ClockNode::Kind parseKind(const QString &raw, const QString &where)
{
    if (raw == "source")
        return ClockNode::Kind::Source;
    if (raw == "mux")
        return ClockNode::Kind::Mux;
    if (raw == "pll")
        return ClockNode::Kind::Pll;
    if (raw == "divider")
        return ClockNode::Kind::Divider;
    if (raw == "gate")
        return ClockNode::Kind::Gate;
    if (raw == "output")
        return ClockNode::Kind::Output;
    invalid(where, "unknown kind '" + raw + "' (expected source/mux/pll/divider/gate/output)");
}

// Parses boards.<id>.clocks and validates it against the ClockNode/ClockTree
// contract. Nodes must be declared in topological order (every input id must
// already have been declared); this both defines the ClockTree::nodes order
// and makes an input cycle or a forward reference surface as a plain "unknown
// input" error rather than needing a separate cycle detector.
ClockTree parseClockTree(const QString &boardId, const YAML::Node &clocksNode)
{
    const auto where = "boards." + boardId + ".clocks";
    mapping(clocksNode, where);
    ClockTree tree;
    tree.boardId = boardId;

    const auto nodesNode = clocksNode["nodes"];
    if (!nodesNode)
        invalid(where + ".nodes", "expected a sequence");
    sequence(nodesNode, where + ".nodes");
    if (nodesNode.size() == 0)
        invalid(where + ".nodes", "expected at least one node");

    QSet<QString> declared;
    for (std::size_t i = 0; i < nodesNode.size(); ++i) {
        const auto nodeWhere = where + ".nodes[" + QString::number(i) + "]";
        const auto entry = nodesNode[i];
        mapping(entry, nodeWhere);

        ClockNode node;
        node.id = text(entry["id"], nodeWhere + ".id");
        static const QRegularExpression idPattern("^[a-z0-9_]+$");
        if (!idPattern.match(node.id).hasMatch())
            invalid(nodeWhere + ".id", "expected [a-z0-9_]+, got '" + node.id + "'");
        if (declared.contains(node.id))
            invalid(nodeWhere + ".id", "duplicate node id '" + node.id + "'");

        node.label = text(entry["label"], nodeWhere + ".label");
        node.kind = parseKind(text(entry["kind"], nodeWhere + ".kind"), nodeWhere + ".kind");

        const auto inputsNode = entry["inputs"];
        if (inputsNode) {
            sequence(inputsNode, nodeWhere + ".inputs");
            for (std::size_t j = 0; j < inputsNode.size(); ++j) {
                const auto inputWhere = nodeWhere + ".inputs[" + QString::number(j) + "]";
                const auto inputId = text(inputsNode[j], inputWhere);
                if (!declared.contains(inputId)) {
                    invalid(inputWhere,
                        "unknown input '" + inputId
                            + "' (inputs must reference a node already declared earlier in the list;"
                              " this also rejects self-references and cycles)");
                }
                node.inputs.append(inputId);
            }
        }

        switch (node.kind) {
        case ClockNode::Kind::Source:
            if (!node.inputs.isEmpty())
                invalid(nodeWhere + ".inputs", "source nodes must not declare inputs");
            node.freqMHz = number(entry["freq_mhz"], nodeWhere + ".freq_mhz");
            if (node.freqMHz <= 0)
                invalid(nodeWhere + ".freq_mhz", "expected a positive number");
            break;
        case ClockNode::Kind::Mux:
            if (node.inputs.isEmpty())
                invalid(nodeWhere + ".inputs", "mux nodes require at least one input");
            break;
        case ClockNode::Kind::Pll:
        case ClockNode::Kind::Divider:
        case ClockNode::Kind::Gate:
        case ClockNode::Kind::Output:
            if (node.inputs.size() != 1)
                invalid(nodeWhere + ".inputs", "expected exactly one input");
            break;
        }

        const auto optionsNode = entry["options"];
        if (optionsNode) {
            if (node.kind != ClockNode::Kind::Pll && node.kind != ClockNode::Kind::Divider)
                invalid(nodeWhere + ".options", "options are only valid on pll/divider nodes");
            sequence(optionsNode, nodeWhere + ".options");
            for (std::size_t j = 0; j < optionsNode.size(); ++j) {
                const auto optionWhere = nodeWhere + ".options[" + QString::number(j) + "]";
                const auto value = number(optionsNode[j], optionWhere);
                if (value <= 0)
                    invalid(optionWhere, "expected a positive number");
                node.options.append(value);
            }
        }

        const auto defaultNode = entry["default"];
        if (node.kind == ClockNode::Kind::Mux) {
            node.defaultValue = text(defaultNode, nodeWhere + ".default");
            if (!node.inputs.contains(node.defaultValue))
                invalid(nodeWhere + ".default", "default '" + node.defaultValue + "' is not one of this node's inputs");
        } else if (node.kind == ClockNode::Kind::Pll || node.kind == ClockNode::Kind::Divider) {
            const auto value = number(defaultNode, nodeWhere + ".default");
            if (value <= 0)
                invalid(nodeWhere + ".default", "expected a positive number");
            if (!node.options.isEmpty()) {
                bool found = false;
                for (const auto option : node.options)
                    found = found || qFuzzyCompare(option, value);
                if (!found)
                    invalid(nodeWhere + ".default", "default is not one of this node's options");
            }
            node.defaultValue = QString::number(value, 'g', 15);
        } else if (defaultNode) {
            invalid(nodeWhere + ".default", "default is only valid on mux/pll/divider nodes");
        }

        node.editable = boolean(entry["editable"], nodeWhere + ".editable", false);
        if (node.editable && (node.kind == ClockNode::Kind::Source || node.kind == ClockNode::Kind::Gate
                || node.kind == ClockNode::Kind::Output))
            invalid(nodeWhere + ".editable", "source/gate/output nodes cannot be editable (nothing to select)");

        const auto followsNode = entry["follows"];
        if (followsNode) {
            const auto followsWhere = nodeWhere + ".follows";
            if (node.kind != ClockNode::Kind::Mux && node.kind != ClockNode::Kind::Divider)
                invalid(followsWhere, "follows is only valid on mux/divider nodes");
            if (node.editable)
                invalid(followsWhere, "a node that follows another cannot be editable");
            mapping(followsNode, followsWhere);
            node.follows = text(followsNode["node"], followsWhere + ".node");
            const ClockNode *followed = nullptr;
            for (const auto &candidate : tree.nodes)
                if (candidate.id == node.follows) followed = &candidate;
            if (!followed || followed->kind == ClockNode::Kind::Source || followed->kind == ClockNode::Kind::Gate
                    || followed->kind == ClockNode::Kind::Output)
                invalid(followsWhere + ".node", "expected an earlier mux/pll/divider node, got '" + node.follows + "'");
            const auto mapNode = followsNode["map"];
            mapping(mapNode, followsWhere + ".map");
            for (const auto &pair : mapNode) {
                const auto key = text(pair.first, followsWhere + ".map key");
                const auto valueWhere = followsWhere + ".map." + key;
                if (followed->kind == ClockNode::Kind::Mux ? !followed->inputs.contains(key) : key.toDouble() <= 0)
                    invalid(valueWhere, "key '" + key + "' is not a selection of '" + node.follows + "'");
                QString value;
                if (node.kind == ClockNode::Kind::Mux) {
                    value = text(pair.second, valueWhere);
                    if (!node.inputs.contains(value))
                        invalid(valueWhere, "'" + value + "' is not one of this node's inputs");
                } else {
                    const auto factor = number(pair.second, valueWhere);
                    if (factor <= 0)
                        invalid(valueWhere, "expected a positive number");
                    value = QString::number(factor, 'g', 15);
                }
                node.followMap.insert(key, value);
            }
            if (node.followMap.isEmpty())
                invalid(followsWhere + ".map", "expected at least one entry");
        }

        const auto maxNode = entry["max_mhz"];
        if (maxNode) {
            node.maxMHz = number(maxNode, nodeWhere + ".max_mhz");
            if (node.maxMHz < 0)
                invalid(nodeWhere + ".max_mhz", "expected a non-negative number");
        }

        const auto refNode = entry["ref"];
        if (refNode)
            node.reference = text(refNode, nodeWhere + ".ref");
        const auto noteNode = entry["note"];
        if (noteNode)
            node.note = text(noteNode, nodeWhere + ".note");

        declared.insert(node.id);
        tree.nodes.append(node);
    }

    const auto peripheralsNode = clocksNode["peripheral_outputs"];
    if (peripheralsNode) {
        sequence(peripheralsNode, where + ".peripheral_outputs");
        for (std::size_t i = 0; i < peripheralsNode.size(); ++i) {
            const auto entryWhere = where + ".peripheral_outputs[" + QString::number(i) + "]";
            const auto id = text(peripheralsNode[i], entryWhere);
            if (!declared.contains(id))
                invalid(entryWhere, "unknown node id '" + id + "'");
            tree.peripheralOutputs.append(id);
        }
    }

    return tree;
}

} // namespace

ClockResult computeClocks(const ClockTree &tree, const ClockConfig &config)
{
    ClockResult result;
    QMap<QString, QString> effective; // node id -> selection actually used
    for (const auto &node : tree.nodes) {
        double freq = 0;
        // A follower's selection is dictated by hardware coupling, not config.
        QString followed;
        if (!node.follows.isEmpty()) {
            const auto source = effective.value(node.follows);
            for (auto it = node.followMap.begin(); it != node.followMap.end(); ++it) {
                bool numeric = false;
                const double key = it.key().toDouble(&numeric);
                if (it.key() == source || (numeric && qFuzzyCompare(key, source.toDouble())))
                    followed = it.value();
            }
            if (followed.isEmpty()) {
                result.errors.append(node.label + ": no documented value when " + node.follows + " is '" + source
                    + "', using default '" + node.defaultValue + "'");
                followed = node.defaultValue;
            }
        }
        switch (node.kind) {
        case ClockNode::Kind::Source:
            freq = node.freqMHz;
            break;
        case ClockNode::Kind::Mux: {
            auto selected = !followed.isEmpty() ? followed : config.selections.value(node.id, node.defaultValue);
            if (!node.inputs.contains(selected)) {
                result.errors.append(node.label + ": invalid selection '" + selected
                    + "', using default '" + node.defaultValue + "'");
                selected = node.defaultValue;
            }
            freq = result.freqMHz.value(selected, 0);
            effective.insert(node.id, selected);
            break;
        }
        case ClockNode::Kind::Pll:
        case ClockNode::Kind::Divider: {
            const auto base = result.freqMHz.value(node.inputs.first(), 0);
            auto selectedText = !followed.isEmpty() ? followed : config.selections.value(node.id, node.defaultValue);
            bool ok = false;
            double factor = selectedText.toDouble(&ok);
            bool valid = ok && factor > 0
                && (node.options.isEmpty() || [&] {
                       for (const auto option : node.options)
                           if (qFuzzyCompare(option, factor))
                               return true;
                       return false;
                   }());
            if (!valid) {
                result.errors.append(node.label + ": invalid selection '" + selectedText
                    + "', using default '" + node.defaultValue + "'");
                factor = node.defaultValue.toDouble();
            }
            freq = node.kind == ClockNode::Kind::Pll ? base * factor : base / factor;
            effective.insert(node.id, QString::number(factor, 'g', 15));
            break;
        }
        case ClockNode::Kind::Gate:
        case ClockNode::Kind::Output:
            freq = result.freqMHz.value(node.inputs.first(), 0);
            break;
        }
        if (node.maxMHz > 0 && freq > node.maxMHz + 1e-9) {
            result.errors.append(QString("%1 %2 MHz exceeds max %3 MHz")
                                      .arg(node.label)
                                      .arg(freq, 0, 'g', 10)
                                      .arg(node.maxMHz, 0, 'g', 10));
        }
        result.freqMHz.insert(node.id, freq);
    }
    return result;
}

ClockConfig defaultClockConfig(const ClockTree &tree)
{
    ClockConfig config;
    for (const auto &node : tree.nodes) {
        if (node.editable)
            config.selections.insert(node.id, node.defaultValue);
    }
    return config;
}

QMap<QString, ClockTree> ClockCatalog::load(const QString &yamlPath)
{
    try {
        QFile file(yamlPath);
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

        QMap<QString, ClockTree> trees;
        for (const auto &entry : entries) {
            const auto boardId = text(entry.first, "boards key");
            mapping(entry.second, "boards." + boardId);
            const auto clocksNode = entry.second["clocks"];
            if (!clocksNode)
                continue;
            trees.insert(boardId, parseClockTree(boardId, clocksNode));
        }
        return trees;
    } catch (const YAML::Exception &error) {
        throw std::runtime_error("ClockCatalog '" + yamlPath.toStdString() + "': invalid YAML: " + error.what());
    } catch (const std::runtime_error &error) {
        throw std::runtime_error("ClockCatalog '" + yamlPath.toStdString() + "': " + error.what());
    }
}

} // namespace Hypr
