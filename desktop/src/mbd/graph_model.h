#pragma once

// GraphModel: single source of truth for the MBD graph editor's document
// state. Owns a Hypr::GraphDocument, selection (node xor edge), nextId,
// dirty flag, and a snapshot-based undo/redo stack. See
// desktop/docs/mbd_graph_contract.md §1.4 and §7 for the id/label rules and
// the API this must expose to Agent B (NodeInspector/NodePalette) and Agent C
// (persistence).

#include "contract/graph_document.h"
#include "contract/ordered_json.h"

#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace Hypr {

class GraphModel : public QObject {
    Q_OBJECT
public:
    explicit GraphModel(QObject *parent = nullptr);

    // --- Document access -------------------------------------------------
    const GraphDocument &document() const;
    const GraphNode *node(const QString &id) const;
    const GraphEdge *edge(const QString &id) const;

    // --- Selection (mutually exclusive) -----------------------------------
    QString selectedNodeId() const;
    QString selectedEdgeId() const;
    void setSelectedNode(const QString &nodeId);   // "" clears
    void setSelectedEdge(const QString &edgeId);   // "" clears
    void clearSelection();

    bool isDirty() const;
    // After a successful save: clears dirty without touching undo history, and
    // adopts the id/name the store normalised (trimmed name, fallback to id).
    void markSaved(const QString &graphId, const QString &name);

    // --- Load --------------------------------------------------------------
    // Replaces the whole document, recomputes nextId per contract §1.4,
    // clears undo/redo and selection, clears dirty, emits documentReset().
    void reset(const GraphDocument &doc);

    // --- Mutators (contract §7 / §1.4) -------------------------------------
    // id = type with non-letters stripped, lowercased, + '_' + nextId++.
    // label = displayName + ' ' + id. params is deep-copied as the node's
    // params. One undo step.
    QString addNode(const QString &type, const Json::Object &defaultParams,
                     QPointF pos, const QString &displayName);
    // Removes the node and any edges touching it. One undo step. Returns
    // false if the node does not exist.
    bool removeNode(const QString &id);
    // Moves a node to a new position without pushing an undo step by itself;
    // callers doing an interactive drag should call beginDrag()/endDrag()
    // around a sequence of moveNode() calls so the whole drag is one undo
    // step (matching the web's "drag = one undo step").
    bool moveNode(const QString &id, QPointF pos);
    void beginDrag(const QString &nodeId);
    void endDrag();

    void setLabel(const QString &nodeId, const QString &label);

    // JS obj[key] = value semantics via Json::set: existing key keeps its
    // position, new key is appended. No-op (and no signal) if the value is
    // unchanged. Setting 'hardwareResource' to "" deletes the key instead
    // (contract §1.3 / web updateParam).
    void setParam(const QString &nodeId, const QString &key, const Json::Value &value);
    void removeParam(const QString &nodeId, const QString &key);

    // Edge id via edgeIdFor(). The model does no connection validation; the
    // canvas consults NodeTypeProvider::checkConnection first. One undo step.
    QString addEdge(const QString &fromNode, const QString &fromPort,
                     const QString &toNode, const QString &toPort);
    bool removeEdge(const QString &id);

    // --- Undo/redo -----------------------------------------------------
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

Q_SIGNALS:
    void selectionChanged();
    void nodeChanged(const QString &nodeId);
    void documentReset();

    void nodeAdded(const QString &nodeId);
    void nodeRemoved(const QString &nodeId);
    void edgeAdded(const QString &edgeId);
    void edgeRemoved(const QString &edgeId);
    void nodeMoved(const QString &nodeId);
    void dirtyChanged(bool dirty);

private:
    struct Snapshot {
        GraphDocument doc;
        QString selectedNodeId;
        QString selectedEdgeId;
        int nextId = 1;
    };

    GraphDocument doc_;
    QString selectedNodeId_;
    QString selectedEdgeId_;
    int nextId_ = 1;
    bool dirty_ = false;

    QVector<Snapshot> undoStack_;
    QVector<Snapshot> redoStack_;
    bool dragActive_ = false;
    bool dragChanged_ = false;
    Snapshot dragStartSnapshot_;

    GraphNode *findNode(const QString &id);
    GraphEdge *findEdge(const QString &id);

    Snapshot snapshot() const;
    void restore(const Snapshot &snap);
    void remember();       // pushes current state onto undoStack_, clears redo
    void setDirty(bool dirty);
    static int nextIdAfter(const GraphDocument &doc);
};

} // namespace Hypr
