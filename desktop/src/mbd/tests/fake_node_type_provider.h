#pragma once

// A small, self-contained NodeTypeProvider used only by studio_mbd's own
// tests. Agent A cannot depend on Agent B's NodeTypeRegistry, so this fake
// stands in for it; it mirrors just enough of contract §2/§3/D5 to exercise
// GraphModel/GraphCanvas against the real interface.

#include "contract/node_type_provider.h"

#include <QSet>

namespace HyprTest {

class FakeNodeTypeProvider : public Hypr::NodeTypeProvider {
public:
    QStringList types() const override
    {
        return {"Constant", "Add", "BoolSource", "AnySink", "CordicOp", "CustomCode", "ActuatorLike"};
    }

    bool hasType(const QString &type) const override { return types().contains(type); }

    QString displayName(const QString &type) const override
    {
        if (type == QLatin1String("CordicOp")) return QStringLiteral("CORDIC");
        return type;
    }

    Hypr::Json::Object defaultParams(const QString &type) const override
    {
        Hypr::Json::Object params;
        if (type == QLatin1String("Constant")) {
            Hypr::Json::set(params, "value", Hypr::Json::Value::fromNumber(1));
        } else if (type == QLatin1String("CordicOp")) {
            Hypr::Json::set(params, "operation", Hypr::Json::Value::fromString("sin"));
        } else if (type == QLatin1String("CustomCode")) {
            Hypr::Json::Array inputs;
            inputs.push_back(Hypr::Json::Value::fromString("value"));
            Hypr::Json::set(params, "inputs", Hypr::Json::Value::fromArray(inputs));
        }
        return params;
    }

    QList<Hypr::PortSpec> inputs(const QString &type, const Hypr::Json::Object &params) const override
    {
        if (type == QLatin1String("Add")) return {{"a", "number"}, {"b", "number"}};
        if (type == QLatin1String("AnySink")) return {{"in", "any"}};
        if (type == QLatin1String("ActuatorLike")) return {{"command", "number"}};
        if (type == QLatin1String("CordicOp")) {
            const QString op = operationOf(params);
            if (op == QLatin1String("sincos") || op == QLatin1String("sin") || op == QLatin1String("cos"))
                return {{"angle_rad", "number"}};
            if (op == QLatin1String("atan2")) return {{"y", "number"}, {"x", "number"}};
            return {{"angle_rad", "number"}};
        }
        if (type == QLatin1String("CustomCode")) {
            QList<Hypr::PortSpec> result;
            if (const Hypr::Json::Value *inputsValue = Hypr::Json::find(params, "inputs")) {
                if (inputsValue->isArray())
                    for (const auto &entry : inputsValue->array)
                        if (entry.isString()) result.push_back({entry.string, "number"});
            }
            return result;
        }
        return {};
    }

    QList<Hypr::PortSpec> outputs(const QString &type, const Hypr::Json::Object &params) const override
    {
        if (type == QLatin1String("Constant")) return {{"value", "number"}};
        if (type == QLatin1String("Add")) return {{"value", "number"}};
        if (type == QLatin1String("BoolSource")) return {{"flag", "boolean"}};
        if (type == QLatin1String("ActuatorLike")) return {{"applied", "number"}};
        if (type == QLatin1String("CordicOp")) {
            const QString op = operationOf(params);
            if (op == QLatin1String("sincos")) return {{"sin", "number"}, {"cos", "number"}};
            if (op == QLatin1String("atan2")) return {{"angle_rad", "number"}};
            return {{"value", "number"}};
        }
        return {};
    }

    // Mirrors contract D5: a source port codegen can't use as a generated
    // output. ActuatorLike.applied stands in for the real ActuatorOutput.applied.
    bool isBlockedSource(const QString &type, const QString &port) const
    {
        return type == QLatin1String("ActuatorLike") && port == QLatin1String("applied");
    }

    Hypr::ConnectionCheck checkConnection(const Hypr::GraphDocument &doc,
                                          const QString &fromNode, const QString &fromPort,
                                          const QString &toNode, const QString &toPort) const override
    {
        using Hypr::ConnectionCheck;
        using Hypr::ConnectionVerdict;

        if (fromNode == toNode)
            return {ConnectionVerdict::Block, QStringLiteral("cannot connect a node to itself")};

        const Hypr::GraphNode *from = findNode(doc, fromNode);
        const Hypr::GraphNode *to = findNode(doc, toNode);
        if (!from || !to) return {ConnectionVerdict::Block, QStringLiteral("unknown node")};

        const auto fromOutputs = outputs(from->type, from->params);
        const auto toInputs = inputs(to->type, to->params);

        const Hypr::PortSpec *fromSpec = findPort(fromOutputs, fromPort);
        const Hypr::PortSpec *toSpec = findPort(toInputs, toPort);
        if (!fromSpec) return {ConnectionVerdict::Block, QStringLiteral("not a valid output port")};
        if (!toSpec) return {ConnectionVerdict::Block, QStringLiteral("not a valid input port")};
        if (isBlockedSource(from->type, fromPort))
            return {ConnectionVerdict::Block, QStringLiteral("port is not a generated output")};

        for (const auto &e : doc.edges)
            if (e.toNode == toNode && e.toPort == toPort)
                return {ConnectionVerdict::Block, QStringLiteral("input already connected")};

        if (wouldCreateCycle(doc, fromNode, toNode))
            return {ConnectionVerdict::Block, QStringLiteral("would create a cycle")};

        QString fromLabel = fromSpec->typeLabel;
        QString toLabel = toSpec->typeLabel;
        if (toLabel == QLatin1String("number (optional)")) toLabel = QStringLiteral("number");
        if (fromLabel == QLatin1String("number (optional)")) fromLabel = QStringLiteral("number");
        if (toLabel != QLatin1String("any") && fromLabel != toLabel)
            return {ConnectionVerdict::Warn, QStringLiteral("port types differ: %1 -> %2").arg(fromLabel, toLabel)};

        return {ConnectionVerdict::Ok, QString()};
    }

private:
    static QString operationOf(const Hypr::Json::Object &params)
    {
        if (const Hypr::Json::Value *v = Hypr::Json::find(params, "operation"))
            if (v->isString()) return v->string;
        return QStringLiteral("sin");
    }

    static const Hypr::GraphNode *findNode(const Hypr::GraphDocument &doc, const QString &id)
    {
        for (const auto &n : doc.nodes)
            if (n.id == id) return &n;
        return nullptr;
    }

    static const Hypr::PortSpec *findPort(const QList<Hypr::PortSpec> &ports, const QString &name)
    {
        for (const auto &p : ports)
            if (p.name == name) return &p;
        return nullptr;
    }

    static bool wouldCreateCycle(const Hypr::GraphDocument &doc, const QString &fromNode, const QString &toNode)
    {
        // Does toNode already reach fromNode? If so, adding fromNode->toNode closes a cycle.
        QSet<QString> visited;
        QList<QString> stack{toNode};
        while (!stack.isEmpty()) {
            const QString current = stack.takeLast();
            if (current == fromNode) return true;
            if (visited.contains(current)) continue;
            visited.insert(current);
            for (const auto &e : doc.edges)
                if (e.fromNode == current) stack.push_back(e.toNode);
        }
        return false;
    }
};

} // namespace HyprTest
