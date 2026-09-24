#include "node_type_registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <functional>
#include <stdexcept>

// Must live at true global scope (no enclosing namespace, not even an
// anonymous one): Q_INIT_RESOURCE's local `extern` declaration is only
// mangled to match the qrc-generated global qInitResources_node_types()
// symbol from there.
static void ensureNodeTypesResourceInitialized()
{
    Q_INIT_RESOURCE(node_types);
}

namespace Hypr {

namespace {

Json::Value fromQJson(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return Json::Value::null();
    case QJsonValue::Bool:
        return Json::Value::fromBool(v.toBool());
    case QJsonValue::Double:
        return Json::Value::fromNumber(v.toDouble());
    case QJsonValue::String:
        return Json::Value::fromString(v.toString());
    case QJsonValue::Array: {
        Json::Array arr;
        for (const auto &item : v.toArray()) arr.push_back(fromQJson(item));
        return Json::Value::fromArray(std::move(arr));
    }
    case QJsonValue::Object: {
        Json::Object obj;
        const auto o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            obj.emplace_back(it.key(), fromQJson(it.value()));
        return Json::Value::fromObject(std::move(obj));
    }
    }
    return Json::Value::null();
}

[[noreturn]] void throwShape(const QString &what)
{
    throw std::runtime_error(("NodeTypeRegistry: " + what).toStdString());
}

QString normalizeTypeLabel(const QString &label)
{
    // Contract §2.1 / D5: "number (optional)" is treated as "number" for
    // Warn purposes.
    if (label == QLatin1String("number (optional)")) return QStringLiteral("number");
    return label;
}

} // namespace

NodeTypeRegistry NodeTypeRegistry::loadFromResource(const QString &resourcePath)
{
    // node_types.json is compiled into this static library via
    // qt_add_resources(); its initializer must be forced in explicitly, or a
    // linker may drop the otherwise-unreferenced object file.
    ensureNodeTypesResourceInitialized();
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        throwShape("could not open resource " + resourcePath);
    return loadFromJson(file.readAll());
}

NodeTypeRegistry NodeTypeRegistry::loadFromJson(const QByteArray &jsonBytes)
{
    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        throwShape("could not parse node_types.json: " + parseError.errorString());
    const auto root = doc.object();

    NodeTypeRegistry registry;

    if (!root.value("types").isArray()) throwShape("missing 'types' array");
    for (const auto &typeVal : root.value("types").toArray()) {
        if (!typeVal.isObject()) throwShape("'types' entry is not an object");
        const auto t = typeVal.toObject();

        TypeInfo info;
        info.key = t.value("key").toString();
        if (info.key.isEmpty()) throwShape("type entry missing 'key'");
        info.displayName = t.value("displayName").toString();
        info.description = t.value("description").toString();
        info.category = t.value("category").toString();

        for (const auto &d : t.value("defaults").toArray()) {
            const auto dObj = d.toObject();
            info.defaults.emplace_back(dObj.value("key").toString(), fromQJson(dObj.value("value")));
        }

        const auto portsObj = t.value("ports").toObject();
        info.dynamicPorts = portsObj.value("dynamic").toBool();
        for (const auto &p : portsObj.value("inputs").toArray()) {
            const auto pObj = p.toObject();
            info.staticInputs.push_back({pObj.value("name").toString(), pObj.value("type").toString()});
        }
        for (const auto &p : portsObj.value("outputs").toArray()) {
            const auto pObj = p.toObject();
            info.staticOutputs.push_back({pObj.value("name").toString(), pObj.value("type").toString()});
        }

        for (const auto &g : t.value("inspectorGroups").toArray()) {
            const auto gObj = g.toObject();
            InspectorGroup group;
            group.name = gObj.value("name").toString();
            for (const auto &f : gObj.value("fields").toArray()) {
                const auto fObj = f.toObject();
                InspectorField field;
                field.key = fObj.value("key").toString();
                field.kind = fObj.value("kind").toString();
                if (field.kind == QLatin1String("select")) {
                    for (const auto &o : fObj.value("options").toArray()) {
                        const auto oObj = o.toObject();
                        field.options.push_back({fromQJson(oObj.value("value")), oObj.value("label").toString()});
                    }
                }
                group.fields.push_back(field);
            }
            info.inspectorGroups.push_back(group);
        }

        for (const auto &h : t.value("hardwareResourceTypes").toArray())
            info.hardwareResourceTypes.push_back(h.toString());

        const auto specialObj = t.value("special").toObject();
        info.isCustomCode = specialObj.value("customCode").toBool();
        info.isAccelerator = specialObj.value("accelerator").toBool();

        registry.typeIndex_.insert(info.key, registry.typeList_.size());
        registry.typeList_.push_back(std::move(info));
    }
    if (registry.typeList_.isEmpty()) throwShape("'types' array is empty");

    if (!root.value("categories").isArray()) throwShape("missing 'categories' array");
    for (const auto &c : root.value("categories").toArray()) {
        const auto cObj = c.toObject();
        PaletteCategory cat;
        cat.name = cObj.value("name").toString();
        for (const auto &item : cObj.value("items").toArray()) cat.items.push_back(item.toString());
        cat.placeholder = cObj.value("placeholder").toString();
        registry.categories_.push_back(cat);
    }

    if (!root.value("codegenSupportedTypes").isArray()) throwShape("missing 'codegenSupportedTypes' array");
    for (const auto &v : root.value("codegenSupportedTypes").toArray())
        registry.codegenSupportedTypes_.push_back(v.toString());

    if (!root.value("codegenSources").isObject()) throwShape("missing 'codegenSources' object");
    const auto sourcesObj = root.value("codegenSources").toObject();
    for (auto it = sourcesObj.constBegin(); it != sourcesObj.constEnd(); ++it) {
        const auto specObj = it.value().toObject();
        CodegenSourceSpec spec;
        if (specObj.contains("byOperation")) {
            spec.isCordic = true;
            const auto byOp = specObj.value("byOperation").toObject();
            for (auto opIt = byOp.constBegin(); opIt != byOp.constEnd(); ++opIt) {
                QStringList ports;
                for (const auto &p : opIt.value().toArray()) ports.push_back(p.toString());
                spec.byOperation.insert(opIt.key(), ports);
            }
        } else {
            for (const auto &p : specObj.value("ports").toArray()) spec.flatPorts.push_back(p.toString());
        }
        registry.codegenSources_.insert(it.key(), spec);
    }

    return registry;
}

const NodeTypeRegistry::TypeInfo *NodeTypeRegistry::findType(const QString &type) const
{
    const auto it = typeIndex_.constFind(type);
    if (it == typeIndex_.constEnd()) return nullptr;
    return &typeList_[it.value()];
}

QStringList NodeTypeRegistry::types() const
{
    QStringList result;
    result.reserve(typeList_.size());
    for (const auto &info : typeList_) result.push_back(info.key);
    return result;
}

bool NodeTypeRegistry::hasType(const QString &type) const
{
    return typeIndex_.contains(type);
}

QString NodeTypeRegistry::displayName(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->displayName : type;
}

Json::Object NodeTypeRegistry::defaultParams(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->defaults : Json::Object{};
}

QString NodeTypeRegistry::description(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->description : QString();
}

QString NodeTypeRegistry::category(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->category : QString();
}

QList<InspectorGroup> NodeTypeRegistry::inspectorGroups(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->inspectorGroups : QList<InspectorGroup>{};
}

QStringList NodeTypeRegistry::hardwareResourceTypes(const QString &type) const
{
    const auto *info = findType(type);
    return info ? info->hardwareResourceTypes : QStringList{};
}

bool NodeTypeRegistry::isCustomCode(const QString &type) const
{
    const auto *info = findType(type);
    return info && info->isCustomCode;
}

bool NodeTypeRegistry::isAccelerator(const QString &type) const
{
    const auto *info = findType(type);
    return info && info->isAccelerator;
}

// Contract §2.1: CordicOp's operation param and CustomCode's inputs param
// change the port list. Mirrors graph_editor.html's `ports(type, params)`.
QList<PortSpec> NodeTypeRegistry::dynamicOutputs(const TypeInfo &info, const Json::Object &params) const
{
    if (info.isAccelerator) {
        const Json::Value *op = Json::find(params, QStringLiteral("operation"));
        const QString operation = op && op->isString() ? op->string : QString();
        if (operation == QLatin1String("atan2"))
            return {{QStringLiteral("angle_rad"), QStringLiteral("number")}};
        if (operation == QLatin1String("sincos"))
            return {{QStringLiteral("sin"), QStringLiteral("number")}, {QStringLiteral("cos"), QStringLiteral("number")}};
        return info.staticOutputs; // sin / cos / default -> static ("value":number)
    }
    if (info.isCustomCode) return {}; // CustomCode has no outputs, dynamic or otherwise
    return info.staticOutputs;
}

QList<PortSpec> NodeTypeRegistry::dynamicInputs(const TypeInfo &info, const Json::Object &params) const
{
    if (info.isAccelerator) {
        const Json::Value *op = Json::find(params, QStringLiteral("operation"));
        const QString operation = op && op->isString() ? op->string : QString();
        if (operation == QLatin1String("atan2"))
            return {{QStringLiteral("y"), QStringLiteral("number")}, {QStringLiteral("x"), QStringLiteral("number")}};
        return info.staticInputs; // sin / cos / sincos / default -> static ("angle_rad":number)
    }
    if (info.isCustomCode) {
        QList<PortSpec> result;
        const Json::Value *inputsVal = Json::find(params, QStringLiteral("inputs"));
        if (inputsVal && inputsVal->isArray()) {
            for (const auto &item : inputsVal->array) {
                if (item.isString()) result.push_back({item.string, QStringLiteral("number")});
            }
        }
        return result;
    }
    return info.staticInputs;
}

QList<PortSpec> NodeTypeRegistry::inputs(const QString &type, const Json::Object &params) const
{
    const auto *info = findType(type);
    if (!info) return {};
    return dynamicInputs(*info, params);
}

QList<PortSpec> NodeTypeRegistry::outputs(const QString &type, const Json::Object &params) const
{
    const auto *info = findType(type);
    if (!info) return {};
    return dynamicOutputs(*info, params);
}

QStringList NodeTypeRegistry::codegenSourcePorts(const QString &type, const Json::Object &params, bool *atan2Blocked) const
{
    if (atan2Blocked) *atan2Blocked = false;
    const auto it = codegenSources_.constFind(type);
    if (it == codegenSources_.constEnd()) return {};
    const CodegenSourceSpec &spec = it.value();
    if (!spec.isCordic) return spec.flatPorts;

    const Json::Value *op = Json::find(params, QStringLiteral("operation"));
    const QString operation = op && op->isString() ? op->string : QString();
    if (operation == QLatin1String("atan2")) {
        if (atan2Blocked) *atan2Blocked = true;
        return {};
    }
    return spec.byOperation.value(operation);
}

ConnectionCheck NodeTypeRegistry::checkConnection(const GraphDocument &doc,
                                                   const QString &fromNode, const QString &fromPort,
                                                   const QString &toNode, const QString &toPort) const
{
    ConnectionCheck result;

    const GraphNode *fromNodePtr = nullptr;
    const GraphNode *toNodePtr = nullptr;
    for (const auto &node : doc.nodes) {
        if (node.id == fromNode) fromNodePtr = &node;
        if (node.id == toNode) toNodePtr = &node;
    }
    if (!fromNodePtr) {
        result.verdict = ConnectionVerdict::Block;
        result.reason = QStringLiteral("Node '%1' does not exist.").arg(fromNode);
        return result;
    }
    if (!toNodePtr) {
        result.verdict = ConnectionVerdict::Block;
        result.reason = QStringLiteral("Node '%1' does not exist.").arg(toNode);
        return result;
    }

    // Self-loop.
    if (fromNode == toNode) {
        result.verdict = ConnectionVerdict::Block;
        result.reason = QStringLiteral("A node cannot connect to itself.");
        return result;
    }

    const auto fromOutputs = outputs(fromNodePtr->type, fromNodePtr->params);
    const auto fromInputs = inputs(fromNodePtr->type, fromNodePtr->params);
    const auto toOutputs = outputs(toNodePtr->type, toNodePtr->params);
    const auto toInputs = inputs(toNodePtr->type, toNodePtr->params);

    auto findPort = [](const QList<PortSpec> &ports, const QString &name) -> const PortSpec * {
        for (const auto &p : ports) if (p.name == name) return &p;
        return nullptr;
    };

    const PortSpec *fromAsOutput = findPort(fromOutputs, fromPort);
    const PortSpec *fromAsInput = findPort(fromInputs, fromPort);
    const PortSpec *toAsInput = findPort(toInputs, toPort);
    const PortSpec *toAsOutput = findPort(toOutputs, toPort);

    if (!fromAsOutput) {
        result.verdict = ConnectionVerdict::Block;
        if (fromAsInput)
            result.reason = QStringLiteral("'%1' is an input of '%2', not an output; connections must run output → input.").arg(fromPort, fromNode);
        else
            result.reason = QStringLiteral("'%1' is not a port of '%2'.").arg(fromPort, fromNode);
        return result;
    }
    if (!toAsInput) {
        result.verdict = ConnectionVerdict::Block;
        if (toAsOutput)
            result.reason = QStringLiteral("'%1' is an output of '%2', not an input; connections must run output → input.").arg(toPort, toNode);
        else
            result.reason = QStringLiteral("'%1' is not a port of '%2'.").arg(toPort, toNode);
        return result;
    }

    // Already-connected input: at most one edge per input port.
    for (const auto &edge : doc.edges) {
        if (edge.toNode == toNode && edge.toPort == toPort) {
            result.verdict = ConnectionVerdict::Block;
            result.reason = QStringLiteral("Input '%1' on '%2' already has a connection.").arg(toPort, toNode);
            return result;
        }
    }

    // Cycle: block if toNode can already reach fromNode.
    {
        QHash<QString, QStringList> adjacency;
        for (const auto &edge : doc.edges) adjacency[edge.fromNode].push_back(edge.toNode);
        QSet<QString> visited;
        QList<QString> queue{toNode};
        visited.insert(toNode);
        bool reachesFrom = false;
        while (!queue.isEmpty()) {
            const QString current = queue.takeFirst();
            if (current == fromNode) { reachesFrom = true; break; }
            for (const auto &next : adjacency.value(current)) {
                if (!visited.contains(next)) {
                    visited.insert(next);
                    queue.push_back(next);
                }
            }
        }
        if (reachesFrom) {
            result.verdict = ConnectionVerdict::Block;
            result.reason = QStringLiteral("Connecting '%1' to '%2' would create a cycle.").arg(fromNode, toNode);
            return result;
        }
    }

    // Codegen source acceptability.
    {
        bool atan2Blocked = false;
        const QStringList sourcePorts = codegenSourcePorts(fromNodePtr->type, fromNodePtr->params, &atan2Blocked);
        if (atan2Blocked) {
            result.verdict = ConnectionVerdict::Block;
            result.reason = QStringLiteral("CORDIC atan2 is not supported by codegen");
            return result;
        }
        if (!sourcePorts.contains(fromPort)) {
            result.verdict = ConnectionVerdict::Block;
            result.reason = QStringLiteral("'%1.%2' is not a port codegen can use as a generated output.").arg(fromNode, fromPort);
            return result;
        }
    }

    // Warn: type-label mismatch, unless the target accepts "any".
    const QString fromLabel = normalizeTypeLabel(fromAsOutput->typeLabel);
    const QString toLabel = normalizeTypeLabel(toAsInput->typeLabel);
    if (fromLabel != toLabel && toLabel != QLatin1String("any")) {
        result.verdict = ConnectionVerdict::Warn;
        result.reason = QStringLiteral("Port types differ: '%1' produces %2, '%3' expects %4.").arg(fromPort, fromLabel, toPort, toLabel);
        return result;
    }

    result.verdict = ConnectionVerdict::Ok;
    return result;
}

} // namespace Hypr
