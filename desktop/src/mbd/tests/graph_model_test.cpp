#include "fake_node_type_provider.h"
#include "graph_model.h"

#include <QSignalSpy>
#include <QTest>

using namespace Hypr;
using HyprTest::FakeNodeTypeProvider;

namespace {

GraphDocument makeDoc(const QList<GraphNode> &nodes = {}, const QList<GraphEdge> &edges = {})
{
    GraphDocument doc;
    doc.id = QStringLiteral("g1");
    doc.name = QStringLiteral("Graph 1");
    doc.nodes = nodes;
    doc.edges = edges;
    return doc;
}

GraphNode makeNode(const QString &id, const QString &type = QStringLiteral("Constant"))
{
    GraphNode n;
    n.id = id;
    n.type = type;
    n.label = id;
    return n;
}

} // namespace

class GraphModelTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void addNode_idAndLabelRules();
    void nextId_afterReset_overVariousIds();
    void removeNode_removesItsEdges();
    void setParam_existingKeyKeepsPosition_newKeyAppended();
    void setParam_hardwareResourceEmptyDeletesKey();
    void setParam_noopWhenUnchanged();
    void undoRedo_addNode();
    void undoRedo_drag_isOneStep();
    void selection_isMutuallyExclusive();
};

void GraphModelTest::addNode_idAndLabelRules()
{
    GraphModel model;
    FakeNodeTypeProvider provider;

    const QString id1 = model.addNode(QStringLiteral("CordicOp"), provider.defaultParams("CordicOp"),
                                       QPointF(10, 20), provider.displayName("CordicOp"));
    QCOMPARE(id1, QStringLiteral("cordicop_1"));
    const GraphNode *n1 = model.node(id1);
    QVERIFY(n1);
    QCOMPARE(n1->label, QStringLiteral("CORDIC cordicop_1"));
    QCOMPARE(n1->position, QPointF(10, 20));

    const QString id2 = model.addNode(QStringLiteral("Constant"), provider.defaultParams("Constant"),
                                       QPointF(0, 0), provider.displayName("Constant"));
    QCOMPARE(id2, QStringLiteral("constant_2"));
    QCOMPARE(model.node(id2)->label, QStringLiteral("Constant constant_2"));
}

void GraphModelTest::nextId_afterReset_overVariousIds()
{
    GraphModel model;
    FakeNodeTypeProvider provider;

    GraphDocument doc = makeDoc({makeNode("cordicop_3"), makeNode("sin_10"), makeNode("weird")});
    model.reset(doc);

    const QString newId = model.addNode(QStringLiteral("Constant"), {}, QPointF(), QStringLiteral("Constant"));
    QCOMPARE(newId, QStringLiteral("constant_11"));
}

void GraphModelTest::removeNode_removesItsEdges()
{
    GraphModel model;
    GraphNode a = makeNode("a_1");
    GraphNode b = makeNode("b_2");
    GraphEdge e;
    e.id = "a_1-value-b_2-a";
    e.fromNode = "a_1";
    e.fromPort = "value";
    e.toNode = "b_2";
    e.toPort = "a";
    model.reset(makeDoc({a, b}, {e}));

    QSignalSpy edgeRemoved(&model, &GraphModel::edgeRemoved);
    QVERIFY(model.removeNode("a_1"));
    QCOMPARE(edgeRemoved.count(), 1);
    QVERIFY(!model.node("a_1"));
    QCOMPARE(model.document().edges.size(), 0);
}

void GraphModelTest::setParam_existingKeyKeepsPosition_newKeyAppended()
{
    GraphModel model;
    GraphNode n = makeNode("constant_1");
    Json::set(n.params, "a", Json::Value::fromNumber(1));
    Json::set(n.params, "b", Json::Value::fromNumber(2));
    model.reset(makeDoc({n}));

    model.setParam("constant_1", "a", Json::Value::fromNumber(99));
    model.setParam("constant_1", "c", Json::Value::fromNumber(3));

    const GraphNode *result = model.node("constant_1");
    QVERIFY(result);
    QCOMPARE(static_cast<int>(result->params.size()), 3);
    QCOMPARE(result->params[0].first, QStringLiteral("a"));
    QCOMPARE(result->params[0].second.number, 99.0);
    QCOMPARE(result->params[1].first, QStringLiteral("b"));
    QCOMPARE(result->params[2].first, QStringLiteral("c"));
}

void GraphModelTest::setParam_hardwareResourceEmptyDeletesKey()
{
    GraphModel model;
    GraphNode n = makeNode("sensor_1");
    Json::set(n.params, "hardwareResource", Json::Value::fromString("gpio.5"));
    model.reset(makeDoc({n}));

    model.setParam("sensor_1", "hardwareResource", Json::Value::fromString(""));

    const GraphNode *result = model.node("sensor_1");
    QVERIFY(!Json::find(result->params, "hardwareResource"));
}

void GraphModelTest::setParam_noopWhenUnchanged()
{
    GraphModel model;
    GraphNode n = makeNode("constant_1");
    Json::set(n.params, "value", Json::Value::fromNumber(1));
    model.reset(makeDoc({n}));

    QSignalSpy changed(&model, &GraphModel::nodeChanged);
    model.setParam("constant_1", "value", Json::Value::fromNumber(1));
    QCOMPARE(changed.count(), 0);
    QVERIFY(!model.canUndo());
}

void GraphModelTest::undoRedo_addNode()
{
    GraphModel model;
    FakeNodeTypeProvider provider;
    model.reset(makeDoc());

    const QString id = model.addNode(QStringLiteral("Constant"), {}, QPointF(), QStringLiteral("Constant"));
    QVERIFY(model.node(id));
    QVERIFY(model.canUndo());

    model.undo();
    QVERIFY(!model.node(id));
    QVERIFY(model.canRedo());

    model.redo();
    QVERIFY(model.node(id));
}

void GraphModelTest::undoRedo_drag_isOneStep()
{
    GraphModel model;
    GraphNode n = makeNode("constant_1");
    n.position = QPointF(0, 0);
    model.reset(makeDoc({n}));

    model.beginDrag("constant_1");
    model.moveNode("constant_1", QPointF(10, 10));
    model.moveNode("constant_1", QPointF(20, 20));
    model.moveNode("constant_1", QPointF(30, 30));
    model.endDrag();

    QCOMPARE(model.node("constant_1")->position, QPointF(30, 30));
    QVERIFY(model.canUndo());

    model.undo();
    QCOMPARE(model.node("constant_1")->position, QPointF(0, 0));
    QVERIFY(!model.canUndo());
}

void GraphModelTest::selection_isMutuallyExclusive()
{
    GraphModel model;
    GraphNode n = makeNode("constant_1");
    model.reset(makeDoc({n}));

    model.setSelectedNode("constant_1");
    QCOMPARE(model.selectedNodeId(), QStringLiteral("constant_1"));
    QVERIFY(model.selectedEdgeId().isEmpty());

    model.setSelectedEdge("some-edge");
    QVERIFY(model.selectedNodeId().isEmpty());
    QCOMPARE(model.selectedEdgeId(), QStringLiteral("some-edge"));
}

QTEST_MAIN(GraphModelTest)
#include "graph_model_test.moc"
