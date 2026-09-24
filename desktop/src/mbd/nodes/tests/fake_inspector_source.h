#pragma once

#include "node_inspector.h"

#include <QHash>

// Minimal in-memory NodeInspector::Source for tests, standing in for
// Agent A's GraphModel (contract §7 "Inspector editing").
class FakeInspectorSource : public Hypr::NodeInspector::Source {
public:
    QHash<QString, Hypr::GraphNode> nodes;

    const Hypr::GraphNode *node(const QString &nodeId) const override
    {
        const auto it = nodes.constFind(nodeId);
        return it == nodes.constEnd() ? nullptr : &it.value();
    }

    void setParam(const QString &nodeId, const QString &key, const Hypr::Json::Value &value) override
    {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) return;
        Hypr::Json::set(it->params, key, value);
        setParamCalls.push_back({nodeId, key, value});
    }

    void removeParam(const QString &nodeId, const QString &key) override
    {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) return;
        Hypr::Json::remove(it->params, key);
        removeParamCalls.push_back({nodeId, key});
    }

    struct SetCall { QString nodeId; QString key; Hypr::Json::Value value; };
    struct RemoveCall { QString nodeId; QString key; };
    QList<SetCall> setParamCalls;
    QList<RemoveCall> removeParamCalls;
};
