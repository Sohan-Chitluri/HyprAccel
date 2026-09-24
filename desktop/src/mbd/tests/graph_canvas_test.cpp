#include "fake_node_type_provider.h"
#include "graph_canvas.h"
#include "graph_model.h"

#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

using namespace Hypr;
using HyprTest::FakeNodeTypeProvider;

namespace {

GraphNode makeNode(const QString &id, const QString &type, QPointF pos = QPointF())
{
    GraphNode n;
    n.id = id;
    n.type = type;
    n.label = id;
    n.position = pos;
    return n;
}

QPoint viewPosFor(GraphCanvas &canvas, QPointF scenePos)
{
    return canvas.mapFromScene(scenePos);
}

} // namespace

class GraphCanvasTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void drop_createsNodeAtScenePosition();
    void connect_ok_addsEdgeUnstyled();
    void connect_warn_addsEdgeAndMarksIt();
    void connect_block_rejectsAndEmitsSignal();
    void deleteKey_removesSelectedNode();
    void nodeDrag_updatesPositionAsOneUndoStep();
    void paramChange_refreshesPorts();
};

void GraphCanvasTest::drop_createsNodeAtScenePosition()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);

    QMimeData mime;
    mime.setData(QStringLiteral("application/x-hypraccel-node"), QByteArray("Constant"));
    const QPoint dropAt(120, 80);
    QDragEnterEvent enter(dropAt, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas.viewport(), &enter);
    QDropEvent event(QPointF(dropAt), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier, QEvent::Drop);
    QCoreApplication::sendEvent(canvas.viewport(), &event);

    QCOMPARE(model.document().nodes.size(), 1);
    const GraphNode &n = model.document().nodes.first();
    QCOMPARE(n.type, QStringLiteral("Constant"));
    const QPointF expected = canvas.mapToScene(dropAt);
    QVERIFY(std::abs(n.position.x() - expected.x()) < 1.0);
    QVERIFY(std::abs(n.position.y() - expected.y()) < 1.0);
}

void GraphCanvasTest::connect_ok_addsEdgeUnstyled()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    doc.nodes = {makeNode("constant_1", "Constant", QPointF(0, 0)), makeNode("add_2", "Add", QPointF(300, 0))};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);
    canvas.show();

    const QPoint fromPos = viewPosFor(canvas, canvas.portScenePosFor("constant_1", "value", true));
    const QPoint toPos = viewPosFor(canvas, canvas.portScenePosFor("add_2", "a", false));

    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
    QTest::mouseMove(canvas.viewport(), toPos);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

    QCOMPARE(model.document().edges.size(), 1);
    const GraphEdge &e = model.document().edges.first();
    QCOMPARE(e.fromNode, QStringLiteral("constant_1"));
    QCOMPARE(e.toNode, QStringLiteral("add_2"));
}

void GraphCanvasTest::connect_warn_addsEdgeAndMarksIt()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    doc.nodes = {makeNode("bool_1", "BoolSource", QPointF(0, 0)), makeNode("add_2", "Add", QPointF(300, 0))};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);
    canvas.show();

    const QPoint fromPos = viewPosFor(canvas, canvas.portScenePosFor("bool_1", "flag", true));
    const QPoint toPos = viewPosFor(canvas, canvas.portScenePosFor("add_2", "a", false));

    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
    QTest::mouseMove(canvas.viewport(), toPos);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

    QCOMPARE(model.document().edges.size(), 1);

    bool foundWarnItem = false;
    for (auto *item : canvas.scene()->items()) {
        if (auto *edge = dynamic_cast<EdgeItem *>(item)) {
            if (edge->pen().style() == Qt::DashLine && !edge->toolTip().isEmpty()) foundWarnItem = true;
        }
    }
    QVERIFY(foundWarnItem);
}

void GraphCanvasTest::connect_block_rejectsAndEmitsSignal()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    doc.nodes = {makeNode("act_1", "ActuatorLike", QPointF(0, 0)), makeNode("add_2", "Add", QPointF(300, 0))};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);
    canvas.show();
    QSignalSpy rejected(&canvas, &GraphCanvas::connectionRejected);

    // ActuatorLike.applied is a codegen-blocked source port (contract D5 /
    // FakeNodeTypeProvider::isBlockedSource).
    const QPoint fromPos = viewPosFor(canvas, canvas.portScenePosFor("act_1", "applied", true));
    const QPoint toPos = viewPosFor(canvas, canvas.portScenePosFor("add_2", "a", false));

    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, fromPos);
    QTest::mouseMove(canvas.viewport(), toPos);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, toPos);

    QCOMPARE(model.document().edges.size(), 0);
    QCOMPARE(rejected.count(), 1);
}

void GraphCanvasTest::deleteKey_removesSelectedNode()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    doc.nodes = {makeNode("constant_1", "Constant", QPointF(0, 0))};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);
    canvas.show();
    canvas.setFocus();

    model.setSelectedNode("constant_1");
    QTest::keyClick(&canvas, Qt::Key_Delete);

    QVERIFY(!model.node("constant_1"));
}

void GraphCanvasTest::nodeDrag_updatesPositionAsOneUndoStep()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    doc.nodes = {makeNode("constant_1", "Constant", QPointF(0, 0))};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);
    canvas.show();

    const QPoint headerPos = viewPosFor(canvas, QPointF(60, 10)); // inside the node header, away from ports

    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, headerPos);
    QTest::mouseMove(canvas.viewport(), headerPos + QPoint(40, 20));
    QTest::mouseMove(canvas.viewport(), headerPos + QPoint(80, 40));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, headerPos + QPoint(80, 40));

    QVERIFY(model.node("constant_1")->position != QPointF(0, 0));
    QVERIFY(model.canUndo());
    model.undo();
    QCOMPARE(model.node("constant_1")->position, QPointF(0, 0));
    QVERIFY(!model.canUndo());
}

void GraphCanvasTest::paramChange_refreshesPorts()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    GraphDocument doc;
    doc.id = "g";
    doc.name = "g";
    GraphNode cordic = makeNode("cordicop_1", "CordicOp", QPointF(0, 0));
    Json::set(cordic.params, "operation", Json::Value::fromString("sin"));
    doc.nodes = {cordic};
    model.reset(doc);

    GraphCanvas canvas(&model, &provider);
    canvas.resize(600, 400);

    QVERIFY(canvas.nodeHasRenderedPort("cordicop_1", "value", true));
    QVERIFY(!canvas.nodeHasRenderedPort("cordicop_1", "sin", true));

    model.setParam("cordicop_1", "operation", Json::Value::fromString("sincos"));

    QVERIFY(canvas.nodeHasRenderedPort("cordicop_1", "sin", true));
    QVERIFY(canvas.nodeHasRenderedPort("cordicop_1", "cos", true));
    QVERIFY(!canvas.nodeHasRenderedPort("cordicop_1", "value", true));
}

QTEST_MAIN(GraphCanvasTest)
#include "graph_canvas_test.moc"
