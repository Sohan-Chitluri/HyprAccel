#pragma once

// GraphCanvas: the QGraphicsView/QGraphicsScene rendering of a GraphModel.
// Generic node boxes driven entirely by NodeTypeProvider (no node-type-
// specific logic lives here). See desktop/docs/mbd_graph_contract.md §7.

#include "contract/node_type_provider.h"
#include "graph_model.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QMap>
#include <QPointF>
#include <QString>

class QGraphicsScene;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace Hypr {

// A small circular hit target for one port. Child of a NodeItem.
class PortHandle : public QGraphicsEllipseItem {
public:
    PortHandle(QString portName, bool output, QGraphicsItem *parent);

    QString portName;
    bool output;
};

// Generic node box: header (display name + label), input ports on the left,
// output ports on the right, sized to the port count.
class NodeItem : public QGraphicsItem {
public:
    explicit NodeItem(QString nodeId, QGraphicsItem *parent = nullptr);

    QString nodeId;

    void rebuild(const QString &title, const QString &label,
                 const QList<PortSpec> &inputs, const QList<PortSpec> &outputs);
    bool selected = false;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // Scene position of a named port's connection point, or a point at the
    // node center if the port no longer exists (dangling).
    QPointF portScenePos(const QString &portName, bool output) const;
    bool hasPort(const QString &portName, bool output) const;

    static constexpr qreal kWidth = 170.0;
    static constexpr qreal kHeaderHeight = 30.0;
    static constexpr qreal kRowHeight = 18.0;
    static constexpr qreal kPadding = 6.0;

private:
    QString title_;
    QString label_;
    QList<PortSpec> inputs_;
    QList<PortSpec> outputs_;
    QVector<PortHandle *> inputHandles_;
    QVector<PortHandle *> outputHandles_;
    qreal height_ = kHeaderHeight;
};

// An edge between two ports, rendered as a cubic path. Styled Ok (solid),
// Warn (dashed amber, from the connection check made when it was drawn), or
// dangling (red, when an endpoint port no longer exists on its node).
class EdgeItem : public QGraphicsPathItem {
public:
    QString edgeId;
    QString fromNode, fromPort, toNode, toPort;

    void setSelectedStyle(bool selected);
    void setWarn(bool warn, const QString &reason);
    void setDangling(bool dangling);
    void applyPath(QPointF from, QPointF to);

private:
    void restyle();
    bool selected_ = false;
    bool warn_ = false;
    bool dangling_ = false;
};

class GraphCanvas : public QGraphicsView {
    Q_OBJECT
public:
    GraphCanvas(GraphModel *model, const NodeTypeProvider *provider, QWidget *parent = nullptr);

    void fitToView();
    void zoomIn();
    void zoomOut();
    void resetZoom();

    // Scene-space position of a rendered port's connection point for the
    // given node, or a null QPointF if the node isn't rendered.
    QPointF portScenePosFor(const QString &nodeId, const QString &portName, bool output) const;
    // Whether the node's currently-rendered box still has that port (false
    // for a dangling edge endpoint after e.g. a CordicOp operation change).
    bool nodeHasRenderedPort(const QString &nodeId, const QString &portName, bool output) const;

Q_SIGNALS:
    void connectionRejected(const QString &reason);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    GraphModel *model_;
    const NodeTypeProvider *provider_;
    QGraphicsScene *scene_;

    QMap<QString, NodeItem *> nodeItems_;
    QMap<QString, EdgeItem *> edgeItems_;
    QMap<QString, QString> edgeWarnReason_; // edgeId -> reason, present => Warn-styled

    // Interactive node drag.
    NodeItem *draggingNode_ = nullptr;
    QPointF dragOffset_;

    // Interactive edge draw.
    bool drawingEdge_ = false;
    QString dragFromNode_;
    QString dragFromPort_;
    QGraphicsPathItem *dragPathItem_ = nullptr;

    // Pan.
    bool panning_ = false;
    bool spaceHeld_ = false;
    QPoint panLastPos_;

    double zoomFactor_ = 1.0;

    void rebuildAll();
    void addNodeItem(const QString &id);
    void removeNodeItem(const QString &id);
    void addEdgeItem(const QString &id);
    void removeEdgeItem(const QString &id);
    void refreshNodeItem(const QString &id);
    void refreshEdgeItem(EdgeItem *item);
    void refreshEdgesTouching(const QString &nodeId);

    NodeItem *nodeItemAt(const QPoint &viewPos) const;
    PortHandle *portHandleAt(const QPoint &viewPos) const;
    EdgeItem *edgeItemAt(const QPoint &viewPos) const;

    void selectNode(const QString &id);
    void selectEdge(const QString &id);
    void applyZoom();

private Q_SLOTS:
    void onDocumentReset();
    void onSelectionChanged();
    void onNodeAdded(const QString &id);
    void onNodeRemoved(const QString &id);
    void onEdgeAdded(const QString &id);
    void onEdgeRemoved(const QString &id);
    void onNodeMoved(const QString &id);
    void onNodeChanged(const QString &id);
};

} // namespace Hypr
