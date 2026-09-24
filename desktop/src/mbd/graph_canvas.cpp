#include "graph_canvas.h"

#include "theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Hypr {

namespace {
constexpr char kNodeMimeType[] = "application/x-hypraccel-node";
constexpr double kMinZoom = 0.2;
constexpr double kMaxZoom = 4.0;
constexpr qreal kPortRadius = 5.0;

QColor themeColor(const char *name, const QColor &fallback)
{
    const QColor c = StudioTheme::token(QString::fromLatin1(name));
    return c.isValid() ? c : fallback;
}
} // namespace

// --- PortHandle ----------------------------------------------------------

PortHandle::PortHandle(QString name, bool isOutput, QGraphicsItem *parent)
    : QGraphicsEllipseItem(-kPortRadius, -kPortRadius, kPortRadius * 2, kPortRadius * 2, parent)
    , portName(std::move(name))
    , output(isOutput)
{
    setBrush(QBrush(themeColor("accent", QColor(90, 150, 220))));
    setPen(QPen(themeColor("border", QColor(40, 40, 40)), 1));
    setAcceptHoverEvents(true);
    setZValue(2);
}

// --- NodeItem --------------------------------------------------------------

NodeItem::NodeItem(QString id, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , nodeId(std::move(id))
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setZValue(1);
}

void NodeItem::rebuild(const QString &title, const QString &label,
                        const QList<PortSpec> &inputs, const QList<PortSpec> &outputs)
{
    prepareGeometryChange();
    title_ = title;
    label_ = label;
    inputs_ = inputs;
    outputs_ = outputs;

    qDeleteAll(inputHandles_);
    qDeleteAll(outputHandles_);
    inputHandles_.clear();
    outputHandles_.clear();

    const int rows = std::max(inputs_.size(), outputs_.size());
    height_ = kHeaderHeight + kPadding * 2 + rows * kRowHeight;
    if (rows == 0) height_ = kHeaderHeight + kPadding;

    for (int i = 0; i < inputs_.size(); ++i) {
        auto *handle = new PortHandle(inputs_[i].name, false, this);
        const qreal y = kHeaderHeight + kPadding + i * kRowHeight + kRowHeight / 2.0;
        handle->setPos(0, y);
        inputHandles_.push_back(handle);
    }
    for (int i = 0; i < outputs_.size(); ++i) {
        auto *handle = new PortHandle(outputs_[i].name, true, this);
        const qreal y = kHeaderHeight + kPadding + i * kRowHeight + kRowHeight / 2.0;
        handle->setPos(kWidth, y);
        outputHandles_.push_back(handle);
    }
    update();
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-kPortRadius, -kPortRadius, kWidth + kPortRadius * 2, height_ + kPortRadius * 2);
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF body(0, 0, kWidth, height_);
    QColor fill = themeColor("card", QColor(250, 250, 252));
    QColor border = selected ? themeColor("primary", QColor(60, 120, 220))
                              : themeColor("border", QColor(190, 190, 195));

    painter->setBrush(fill);
    painter->setPen(QPen(border, selected ? 2 : 1));
    painter->drawRoundedRect(body, 6, 6);

    // Header (clipped to the body's rounded shape so the top corners stay round).
    painter->save();
    QPainterPath clip;
    clip.addRoundedRect(body, 6, 6);
    painter->setClipPath(clip);
    painter->setPen(Qt::NoPen);
    painter->setBrush(themeColor("surface2", QColor(230, 230, 235)));
    painter->drawRect(QRectF(0, 0, kWidth, kHeaderHeight));
    painter->restore();

    painter->setPen(themeColor("foreground", QColor(20, 20, 25)));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF());
    painter->setFont(titleFont);
    painter->drawText(QRectF(kPadding, 2, kWidth - kPadding * 2, kHeaderHeight / 2.0 + 2),
                       Qt::AlignLeft | Qt::AlignVCenter, title_);

    QFont labelFont = painter->font();
    labelFont.setBold(false);
    labelFont.setPointSizeF(labelFont.pointSizeF() * 0.85);
    painter->setFont(labelFont);
    painter->setPen(themeColor("mutedFg", QColor(110, 110, 115)));
    painter->drawText(QRectF(kPadding, kHeaderHeight / 2.0, kWidth - kPadding * 2, kHeaderHeight / 2.0),
                       Qt::AlignLeft | Qt::AlignVCenter, label_);

    // Port labels.
    QFont portFont = painter->font();
    portFont.setPointSizeF(portFont.pointSizeF() * 0.9);
    painter->setFont(portFont);
    painter->setPen(themeColor("foreground", QColor(20, 20, 25)));

    for (int i = 0; i < inputs_.size(); ++i) {
        const qreal y = kHeaderHeight + kPadding + i * kRowHeight;
        const QString text = inputs_[i].name + QStringLiteral(": ") + inputs_[i].typeLabel;
        painter->drawText(QRectF(kPadding + kPortRadius, y, kWidth / 2.0 - kPadding, kRowHeight),
                           Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    for (int i = 0; i < outputs_.size(); ++i) {
        const qreal y = kHeaderHeight + kPadding + i * kRowHeight;
        const QString text = outputs_[i].name + QStringLiteral(": ") + outputs_[i].typeLabel;
        painter->drawText(QRectF(kWidth / 2.0, y, kWidth / 2.0 - kPadding - kPortRadius, kRowHeight),
                           Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

QPointF NodeItem::portScenePos(const QString &portName, bool output) const
{
    const auto &handles = output ? outputHandles_ : inputHandles_;
    for (auto *h : handles)
        if (h->portName == portName) return mapToScene(h->pos());
    // Dangling: fall back to the node's edge midpoint.
    return mapToScene(QPointF(output ? kWidth : 0, height_ / 2.0));
}

bool NodeItem::hasPort(const QString &portName, bool output) const
{
    const auto &handles = output ? outputHandles_ : inputHandles_;
    for (auto *h : handles)
        if (h->portName == portName) return true;
    return false;
}

// --- EdgeItem --------------------------------------------------------------

void EdgeItem::applyPath(QPointF from, QPointF to)
{
    QPainterPath path(from);
    const qreal dx = std::max<qreal>(40.0, std::abs(to.x() - from.x()) * 0.5);
    path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
    setPath(path);
}

void EdgeItem::setSelectedStyle(bool selected)
{
    selected_ = selected;
    restyle();
}

void EdgeItem::setWarn(bool warn, const QString &reason)
{
    warn_ = warn;
    setToolTip(warn ? reason : QString());
    restyle();
}

void EdgeItem::setDangling(bool dangling)
{
    dangling_ = dangling;
    restyle();
}

void EdgeItem::restyle()
{
    QColor color = themeColor("mutedFg", QColor(120, 120, 125));
    Qt::PenStyle style = Qt::SolidLine;
    qreal width = selected_ ? 3 : 2;

    if (dangling_) {
        color = themeColor("err", QColor(210, 60, 60));
        style = Qt::DashLine;
    } else if (warn_) {
        color = themeColor("warn", QColor(220, 160, 30));
        style = Qt::DashLine;
    } else if (selected_) {
        color = themeColor("primary", QColor(60, 120, 220));
    }
    setPen(QPen(color, width, style));
}

// --- GraphCanvas -----------------------------------------------------------

GraphCanvas::GraphCanvas(GraphModel *model, const NodeTypeProvider *provider, QWidget *parent)
    : QGraphicsView(parent)
    , model_(model)
    , provider_(provider)
    , scene_(new QGraphicsScene(this))
{
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::NoDrag);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scene_->setBackgroundBrush(themeColor("background", QColor(245, 245, 248)));

    connect(model_, &GraphModel::documentReset, this, &GraphCanvas::onDocumentReset);
    connect(model_, &GraphModel::selectionChanged, this, &GraphCanvas::onSelectionChanged);
    connect(model_, &GraphModel::nodeAdded, this, &GraphCanvas::onNodeAdded);
    connect(model_, &GraphModel::nodeRemoved, this, &GraphCanvas::onNodeRemoved);
    connect(model_, &GraphModel::edgeAdded, this, &GraphCanvas::onEdgeAdded);
    connect(model_, &GraphModel::edgeRemoved, this, &GraphCanvas::onEdgeRemoved);
    connect(model_, &GraphModel::nodeMoved, this, &GraphCanvas::onNodeMoved);
    connect(model_, &GraphModel::nodeChanged, this, &GraphCanvas::onNodeChanged);

    rebuildAll();
}

void GraphCanvas::rebuildAll()
{
    scene_->clear();
    nodeItems_.clear();
    edgeItems_.clear();

    for (const auto &n : model_->document().nodes) addNodeItem(n.id);
    for (const auto &e : model_->document().edges) addEdgeItem(e.id);
    onSelectionChanged();
}

void GraphCanvas::addNodeItem(const QString &id)
{
    const GraphNode *n = model_->node(id);
    if (!n) return;
    auto *item = new NodeItem(id);
    scene_->addItem(item);
    item->setPos(n->position);
    nodeItems_.insert(id, item);
    refreshNodeItem(id);
}

void GraphCanvas::removeNodeItem(const QString &id)
{
    if (NodeItem *item = nodeItems_.take(id)) {
        scene_->removeItem(item);
        delete item;
    }
}

void GraphCanvas::addEdgeItem(const QString &id)
{
    const GraphEdge *e = model_->edge(id);
    if (!e) return;
    auto *item = new EdgeItem();
    item->edgeId = id;
    item->fromNode = e->fromNode;
    item->fromPort = e->fromPort;
    item->toNode = e->toNode;
    item->toPort = e->toPort;
    item->setZValue(0);
    scene_->addItem(item);
    edgeItems_.insert(id, item);
    refreshEdgeItem(item);
}

void GraphCanvas::removeEdgeItem(const QString &id)
{
    if (EdgeItem *item = edgeItems_.take(id)) {
        scene_->removeItem(item);
        delete item;
    }
    edgeWarnReason_.remove(id);
}

void GraphCanvas::refreshNodeItem(const QString &id)
{
    NodeItem *item = nodeItems_.value(id);
    const GraphNode *n = model_->node(id);
    if (!item || !n) return;

    const QList<PortSpec> ins = provider_->inputs(n->type, n->params);
    const QList<PortSpec> outs = provider_->outputs(n->type, n->params);
    item->rebuild(provider_->displayName(n->type), n->label, ins, outs);
    item->selected = (model_->selectedNodeId() == id);
    item->update();

    refreshEdgesTouching(id);
}

void GraphCanvas::refreshEdgeItem(EdgeItem *item)
{
    if (!item) return;
    NodeItem *fromItem = nodeItems_.value(item->fromNode);
    NodeItem *toItem = nodeItems_.value(item->toNode);
    if (!fromItem || !toItem) return;

    const bool dangling = !fromItem->hasPort(item->fromPort, true) || !toItem->hasPort(item->toPort, false);
    item->setDangling(dangling);
    item->setWarn(edgeWarnReason_.contains(item->edgeId), edgeWarnReason_.value(item->edgeId));
    item->setSelectedStyle(model_->selectedEdgeId() == item->edgeId);
    item->applyPath(fromItem->portScenePos(item->fromPort, true), toItem->portScenePos(item->toPort, false));
}

void GraphCanvas::refreshEdgesTouching(const QString &nodeId)
{
    for (auto *item : std::as_const(edgeItems_))
        if (item->fromNode == nodeId || item->toNode == nodeId) refreshEdgeItem(item);
}

void GraphCanvas::onDocumentReset() { rebuildAll(); }

void GraphCanvas::onSelectionChanged()
{
    const QString selNode = model_->selectedNodeId();
    const QString selEdge = model_->selectedEdgeId();
    for (auto it = nodeItems_.begin(); it != nodeItems_.end(); ++it) {
        it.value()->selected = (it.key() == selNode);
        it.value()->update();
    }
    for (auto it = edgeItems_.begin(); it != edgeItems_.end(); ++it)
        it.value()->setSelectedStyle(it.key() == selEdge);
}

void GraphCanvas::onNodeAdded(const QString &id) { addNodeItem(id); }
void GraphCanvas::onNodeRemoved(const QString &id) { removeNodeItem(id); }
void GraphCanvas::onEdgeAdded(const QString &id) { addEdgeItem(id); }
void GraphCanvas::onEdgeRemoved(const QString &id) { removeEdgeItem(id); }

void GraphCanvas::onNodeMoved(const QString &id)
{
    if (NodeItem *item = nodeItems_.value(id)) {
        const GraphNode *n = model_->node(id);
        if (n) item->setPos(n->position);
        refreshEdgesTouching(id);
    }
}

void GraphCanvas::onNodeChanged(const QString &id) { refreshNodeItem(id); }

// --- Drag & drop from the palette -------------------------------------------

void GraphCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(kNodeMimeType))) event->acceptProposedAction();
    else event->ignore();
}

void GraphCanvas::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(kNodeMimeType))) event->acceptProposedAction();
    else event->ignore();
}

void GraphCanvas::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasFormat(QString::fromLatin1(kNodeMimeType))) {
        event->ignore();
        return;
    }
    const QString type = QString::fromUtf8(mime->data(QString::fromLatin1(kNodeMimeType)));
    if (!provider_->hasType(type)) {
        event->ignore();
        return;
    }
    const QPointF scenePos = mapToScene(event->position().toPoint());
    model_->addNode(type, provider_->defaultParams(type), scenePos, provider_->displayName(type));
    event->acceptProposedAction();
}

// --- Mouse interaction -------------------------------------------------------

NodeItem *GraphCanvas::nodeItemAt(const QPoint &viewPos) const
{
    const auto items = this->items(viewPos);
    for (auto *it : items) {
        if (auto *node = dynamic_cast<NodeItem *>(it)) return node;
        if (auto *port = dynamic_cast<PortHandle *>(it))
            if (auto *node = dynamic_cast<NodeItem *>(port->parentItem())) return node;
    }
    return nullptr;
}

PortHandle *GraphCanvas::portHandleAt(const QPoint &viewPos) const
{
    const auto items = this->items(viewPos);
    for (auto *it : items)
        if (auto *port = dynamic_cast<PortHandle *>(it)) return port;
    return nullptr;
}

EdgeItem *GraphCanvas::edgeItemAt(const QPoint &viewPos) const
{
    const auto items = this->items(viewPos);
    for (auto *it : items)
        if (auto *edge = dynamic_cast<EdgeItem *>(it)) return edge;
    return nullptr;
}

void GraphCanvas::selectNode(const QString &id) { model_->setSelectedNode(id); }
void GraphCanvas::selectEdge(const QString &id) { model_->setSelectedEdge(id); }

void GraphCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || (spaceHeld_ && event->button() == Qt::LeftButton)) {
        panning_ = true;
        panLastPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (PortHandle *port = portHandleAt(event->pos())) {
            if (port->output) {
                NodeItem *node = dynamic_cast<NodeItem *>(port->parentItem());
                if (node) {
                    drawingEdge_ = true;
                    dragFromNode_ = node->nodeId;
                    dragFromPort_ = port->portName;
                    dragPathItem_ = new QGraphicsPathItem();
                    dragPathItem_->setPen(QPen(themeColor("primary", QColor(60, 120, 220)), 2, Qt::DashLine));
                    dragPathItem_->setZValue(3);
                    scene_->addItem(dragPathItem_);
                    QPainterPath p(node->portScenePos(port->portName, true));
                    p.lineTo(mapToScene(event->pos()));
                    dragPathItem_->setPath(p);
                }
                event->accept();
                return;
            }
        }

        if (NodeItem *node = nodeItemAt(event->pos())) {
            selectNode(node->nodeId);
            model_->beginDrag(node->nodeId);
            draggingNode_ = node;
            dragOffset_ = mapToScene(event->pos()) - node->pos();
            event->accept();
            return;
        }

        if (EdgeItem *edge = edgeItemAt(event->pos())) {
            selectEdge(edge->edgeId);
            event->accept();
            return;
        }

        model_->clearSelection();
    }

    QGraphicsView::mousePressEvent(event);
}

void GraphCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_) {
        const QPoint delta = event->pos() - panLastPos_;
        panLastPos_ = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (drawingEdge_ && dragPathItem_) {
        NodeItem *fromNode = nodeItems_.value(dragFromNode_);
        if (fromNode) {
            QPainterPath p(fromNode->portScenePos(dragFromPort_, true));
            p.lineTo(mapToScene(event->pos()));
            dragPathItem_->setPath(p);
        }
        event->accept();
        return;
    }

    if (draggingNode_) {
        const QPointF newPos = mapToScene(event->pos()) - dragOffset_;
        model_->moveNode(draggingNode_->nodeId, newPos);
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void GraphCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        setCursor(spaceHeld_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (drawingEdge_ && event->button() == Qt::LeftButton) {
        drawingEdge_ = false;
        if (dragPathItem_) {
            scene_->removeItem(dragPathItem_);
            delete dragPathItem_;
            dragPathItem_ = nullptr;
        }
        if (PortHandle *port = portHandleAt(event->pos())) {
            if (!port->output) {
                NodeItem *toNode = dynamic_cast<NodeItem *>(port->parentItem());
                if (toNode) {
                    const ConnectionCheck check = provider_->checkConnection(
                        model_->document(), dragFromNode_, dragFromPort_, toNode->nodeId, port->portName);
                    if (check.verdict == ConnectionVerdict::Block) {
                        Q_EMIT connectionRejected(check.reason);
                        QToolTip::showText(event->globalPosition().toPoint(), check.reason, this);
                        setStatusTip(check.reason);
                    } else {
                        const QString id = model_->addEdge(dragFromNode_, dragFromPort_, toNode->nodeId, port->portName);
                        if (check.verdict == ConnectionVerdict::Warn) edgeWarnReason_.insert(id, check.reason);
                        if (EdgeItem *item = edgeItems_.value(id)) refreshEdgeItem(item);
                    }
                }
            }
        }
        event->accept();
        return;
    }

    if (draggingNode_) {
        model_->endDrag();
        draggingNode_ = nullptr;
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void GraphCanvas::applyZoom()
{
    QTransform t;
    t.scale(zoomFactor_, zoomFactor_);
    setTransform(t);
}

void GraphCanvas::wheelEvent(QWheelEvent *event)
{
    const double step = 1.15;
    const double factor = event->angleDelta().y() > 0 ? step : 1.0 / step;
    zoomFactor_ = std::clamp(zoomFactor_ * factor, kMinZoom, kMaxZoom);
    applyZoom();
    event->accept();
}

void GraphCanvas::zoomIn()
{
    zoomFactor_ = std::clamp(zoomFactor_ * 1.2, kMinZoom, kMaxZoom);
    applyZoom();
}

void GraphCanvas::zoomOut()
{
    zoomFactor_ = std::clamp(zoomFactor_ / 1.2, kMinZoom, kMaxZoom);
    applyZoom();
}

void GraphCanvas::resetZoom()
{
    zoomFactor_ = 1.0;
    applyZoom();
}

QPointF GraphCanvas::portScenePosFor(const QString &nodeId, const QString &portName, bool output) const
{
    if (NodeItem *item = nodeItems_.value(nodeId)) return item->portScenePos(portName, output);
    return QPointF();
}

bool GraphCanvas::nodeHasRenderedPort(const QString &nodeId, const QString &portName, bool output) const
{
    if (NodeItem *item = nodeItems_.value(nodeId)) return item->hasPort(portName, output);
    return false;
}

void GraphCanvas::fitToView()
{
    const QRectF itemsRect = scene_->itemsBoundingRect();
    if (itemsRect.isEmpty()) return;
    fitInView(itemsRect.marginsAdded(QMarginsF(40, 40, 40, 40)), Qt::KeepAspectRatio);
    zoomFactor_ = transform().m11();
}

void GraphCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceHeld_ = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!model_->selectedEdgeId().isEmpty()) {
            model_->removeEdge(model_->selectedEdgeId());
            event->accept();
            return;
        }
        if (!model_->selectedNodeId().isEmpty()) {
            model_->removeNode(model_->selectedNodeId());
            event->accept();
            return;
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void GraphCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceHeld_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphCanvas::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint viewPos = event->pos();
    if (NodeItem *node = nodeItemAt(viewPos)) {
        selectNode(node->nodeId);
        QMenu menu(this);
        QAction *deleteAction = menu.addAction(tr("Delete Node"));
        if (menu.exec(event->globalPos()) == deleteAction) {
            const auto reply = QMessageBox::question(this, tr("Delete Node"),
                tr("Delete node \"%1\"? This also removes its connections.").arg(node->nodeId));
            if (reply == QMessageBox::Yes) model_->removeNode(node->nodeId);
        }
        event->accept();
        return;
    }
    if (EdgeItem *edge = edgeItemAt(viewPos)) {
        selectEdge(edge->edgeId);
        QMenu menu(this);
        QAction *removeAction = menu.addAction(tr("Remove Connection"));
        if (menu.exec(event->globalPos()) == removeAction) model_->removeEdge(edge->edgeId);
        event->accept();
        return;
    }
    QGraphicsView::contextMenuEvent(event);
}

} // namespace Hypr
