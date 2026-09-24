#pragma once

// CONTRACT (integrator-owned; see desktop/docs/mbd_graph_contract.md §1.3).
// Plain data for one hypraccel.mbd.graph v1 document, as the editor state the
// web keeps between load and toGraph(). Derived fields (inputs, metadata) are
// NOT stored here: persistence recomputes them on save exactly like toGraph().

#include "ordered_json.h"

#include <QList>
#include <QPointF>
#include <QString>

namespace Hypr {

struct GraphNode {
    QString id;            // ^[A-Za-z][A-Za-z0-9_-]*$
    QString type;          // node type key, e.g. "CordicOp"
    QString label;
    Json::Object params;   // insertion-ordered, unknown keys preserved
    QPointF position;
};

struct GraphEdge {
    QString id;
    QString fromNode;
    QString fromPort;      // an output port of fromNode
    QString toNode;
    QString toPort;        // an input port of toNode
};

struct GraphDocument {
    QString id;            // graph id == file stem
    QString name;          // display name
    QList<GraphNode> nodes;
    QList<GraphEdge> edges;
};

// Contract §1.4 rules, shared so model/persistence/tests agree.
inline QString edgeIdFor(const QString &fromNode, const QString &fromPort,
                         const QString &toNode, const QString &toPort)
{
    return fromNode + QLatin1Char('-') + fromPort + QLatin1Char('-') + toNode + QLatin1Char('-') + toPort;
}

} // namespace Hypr
