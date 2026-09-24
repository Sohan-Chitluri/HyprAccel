#include "graph_model.h"

#include <QRegularExpression>

namespace Hypr {

namespace {

Json::Object deepCopy(const Json::Object &object)
{
    // Json::Object / Json::Value hold everything by value already (no
    // pointers/shared state), so a plain copy is a deep copy.
    return object;
}

} // namespace

GraphModel::GraphModel(QObject *parent) : QObject(parent) {}

const GraphDocument &GraphModel::document() const { return doc_; }

GraphNode *GraphModel::findNode(const QString &id)
{
    for (auto &n : doc_.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

GraphEdge *GraphModel::findEdge(const QString &id)
{
    for (auto &e : doc_.edges)
        if (e.id == id) return &e;
    return nullptr;
}

const GraphNode *GraphModel::node(const QString &id) const
{
    for (const auto &n : doc_.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const GraphEdge *GraphModel::edge(const QString &id) const
{
    for (const auto &e : doc_.edges)
        if (e.id == id) return &e;
    return nullptr;
}

QString GraphModel::selectedNodeId() const { return selectedNodeId_; }
QString GraphModel::selectedEdgeId() const { return selectedEdgeId_; }

void GraphModel::setSelectedNode(const QString &nodeId)
{
    if (selectedNodeId_ == nodeId && selectedEdgeId_.isEmpty()) return;
    selectedNodeId_ = nodeId;
    selectedEdgeId_.clear();
    Q_EMIT selectionChanged();
}

void GraphModel::setSelectedEdge(const QString &edgeId)
{
    if (selectedEdgeId_ == edgeId && selectedNodeId_.isEmpty()) return;
    selectedEdgeId_ = edgeId;
    selectedNodeId_.clear();
    Q_EMIT selectionChanged();
}

void GraphModel::clearSelection()
{
    if (selectedNodeId_.isEmpty() && selectedEdgeId_.isEmpty()) return;
    selectedNodeId_.clear();
    selectedEdgeId_.clear();
    Q_EMIT selectionChanged();
}

bool GraphModel::isDirty() const { return dirty_; }

void GraphModel::setDirty(bool dirty)
{
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    Q_EMIT dirtyChanged(dirty_);
}

int GraphModel::nextIdAfter(const GraphDocument &doc)
{
    static const QRegularExpression suffix(QStringLiteral("_(\\d+)$"));
    int maxSeen = 0;
    for (const auto &n : doc.nodes) {
        const auto match = suffix.match(n.id);
        if (match.hasMatch()) {
            bool ok = false;
            int value = match.captured(1).toInt(&ok);
            if (ok && value > maxSeen) maxSeen = value;
        }
    }
    return maxSeen + 1;
}

void GraphModel::reset(const GraphDocument &doc)
{
    doc_ = doc;
    nextId_ = nextIdAfter(doc_);
    selectedNodeId_.clear();
    selectedEdgeId_.clear();
    undoStack_.clear();
    redoStack_.clear();
    dragActive_ = false;
    dirty_ = false;
    Q_EMIT documentReset();
    Q_EMIT selectionChanged();
}

GraphModel::Snapshot GraphModel::snapshot() const
{
    Snapshot s;
    s.doc = doc_;
    s.selectedNodeId = selectedNodeId_;
    s.selectedEdgeId = selectedEdgeId_;
    s.nextId = nextId_;
    return s;
}

void GraphModel::restore(const Snapshot &snap)
{
    doc_ = snap.doc;
    selectedNodeId_ = snap.selectedNodeId;
    selectedEdgeId_ = snap.selectedEdgeId;
    nextId_ = snap.nextId;
    setDirty(true);
    Q_EMIT documentReset();
    Q_EMIT selectionChanged();
}

void GraphModel::remember()
{
    undoStack_.push_back(snapshot());
    redoStack_.clear();
}

QString GraphModel::addNode(const QString &type, const Json::Object &defaultParams,
                             QPointF pos, const QString &displayName)
{
    remember();

    QString stripped;
    for (const QChar &c : type)
        if (c.isLetter()) stripped += c.toLower();

    const QString id = stripped + QLatin1Char('_') + QString::number(nextId_++);

    GraphNode n;
    n.id = id;
    n.type = type;
    n.label = displayName + QLatin1Char(' ') + id;
    n.params = deepCopy(defaultParams);
    n.position = pos;
    doc_.nodes.push_back(n);

    setDirty(true);
    Q_EMIT nodeAdded(id);
    return id;
}

bool GraphModel::removeNode(const QString &id)
{
    GraphNode *n = findNode(id);
    if (!n) return false;

    remember();

    QVector<QString> removedEdges;
    for (auto it = doc_.edges.begin(); it != doc_.edges.end();) {
        if (it->fromNode == id || it->toNode == id) {
            removedEdges.push_back(it->id);
            it = doc_.edges.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = doc_.nodes.begin(); it != doc_.nodes.end(); ++it) {
        if (it->id == id) {
            doc_.nodes.erase(it);
            break;
        }
    }

    if (selectedNodeId_ == id) selectedNodeId_.clear();
    for (const auto &eid : removedEdges)
        if (selectedEdgeId_ == eid) selectedEdgeId_.clear();

    setDirty(true);
    for (const auto &eid : removedEdges)
        Q_EMIT edgeRemoved(eid);
    Q_EMIT nodeRemoved(id);
    Q_EMIT selectionChanged();
    return true;
}

bool GraphModel::moveNode(const QString &id, QPointF pos)
{
    GraphNode *n = findNode(id);
    if (!n) return false;
    if (n->position == pos) return true;

    if (!dragActive_) {
        remember();
    } else {
        dragChanged_ = true;
    }
    n->position = pos;
    setDirty(true);
    Q_EMIT nodeMoved(id);
    return true;
}

void GraphModel::beginDrag(const QString &nodeId)
{
    Q_UNUSED(nodeId);
    if (dragActive_) return;
    dragActive_ = true;
    dragChanged_ = false;
    dragStartSnapshot_ = snapshot();
}

void GraphModel::endDrag()
{
    if (!dragActive_) return;
    dragActive_ = false;
    // Only push the drag as an undo step if it actually moved something.
    if (dragChanged_) {
        undoStack_.push_back(dragStartSnapshot_);
        redoStack_.clear();
    }
}

void GraphModel::setLabel(const QString &nodeId, const QString &label)
{
    GraphNode *n = findNode(nodeId);
    if (!n) return;
    if (n->label == label) return;
    remember();
    n->label = label;
    setDirty(true);
    Q_EMIT nodeChanged(nodeId);
}

void GraphModel::setParam(const QString &nodeId, const QString &key, const Json::Value &value)
{
    GraphNode *n = findNode(nodeId);
    if (!n) return;

    if (key == QLatin1String("hardwareResource") && value.isString() && value.string.isEmpty()) {
        removeParam(nodeId, key);
        return;
    }

    const Json::Value *existing = Json::find(n->params, key);
    if (existing && *existing == value) return; // no-op, no signal

    remember();
    Json::set(n->params, key, value);
    setDirty(true);
    Q_EMIT nodeChanged(nodeId);
}

void GraphModel::removeParam(const QString &nodeId, const QString &key)
{
    GraphNode *n = findNode(nodeId);
    if (!n) return;
    if (!Json::find(n->params, key)) return; // no-op, no signal

    remember();
    Json::remove(n->params, key);
    setDirty(true);
    Q_EMIT nodeChanged(nodeId);
}

QString GraphModel::addEdge(const QString &fromNode, const QString &fromPort,
                             const QString &toNode, const QString &toPort)
{
    remember();

    GraphEdge e;
    e.id = edgeIdFor(fromNode, fromPort, toNode, toPort);
    e.fromNode = fromNode;
    e.fromPort = fromPort;
    e.toNode = toNode;
    e.toPort = toPort;
    doc_.edges.push_back(e);

    setDirty(true);
    Q_EMIT edgeAdded(e.id);
    return e.id;
}

bool GraphModel::removeEdge(const QString &id)
{
    for (auto it = doc_.edges.begin(); it != doc_.edges.end(); ++it) {
        if (it->id == id) {
            remember();
            doc_.edges.erase(it);
            if (selectedEdgeId_ == id) {
                selectedEdgeId_.clear();
                Q_EMIT selectionChanged();
            }
            setDirty(true);
            Q_EMIT edgeRemoved(id);
            return true;
        }
    }
    return false;
}

bool GraphModel::canUndo() const { return !undoStack_.isEmpty(); }
bool GraphModel::canRedo() const { return !redoStack_.isEmpty(); }

void GraphModel::undo()
{
    if (undoStack_.isEmpty()) return;
    redoStack_.push_back(snapshot());
    const Snapshot snap = undoStack_.back();
    undoStack_.pop_back();
    restore(snap);
}

void GraphModel::redo()
{
    if (redoStack_.isEmpty()) return;
    undoStack_.push_back(snapshot());
    const Snapshot snap = redoStack_.back();
    redoStack_.pop_back();
    restore(snap);
}

} // namespace Hypr

void Hypr::GraphModel::markSaved(const QString &graphId, const QString &name)
{
    doc_.id = graphId;
    doc_.name = name;
    setDirty(false);
}
