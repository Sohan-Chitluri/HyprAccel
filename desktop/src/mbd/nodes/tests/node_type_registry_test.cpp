#include "node_type_registry.h"

#include <QFile>
#include <QSet>
#include <QTest>

using namespace Hypr;

namespace {

QSet<QString> portNames(const QList<PortSpec> &ports)
{
    QSet<QString> names;
    for (const auto &p : ports) names.insert(p.name);
    return names;
}

GraphNode makeNode(const QString &id, const QString &type, Json::Object params = {})
{
    GraphNode node;
    node.id = id;
    node.type = type;
    node.label = id;
    node.params = std::move(params);
    return node;
}

} // namespace

class NodeTypeRegistryTest : public QObject {
    Q_OBJECT
private:
    NodeTypeRegistry loadFixture()
    {
        QFile file(QStringLiteral(NODE_TYPES_JSON_PATH));
        if (!file.open(QIODevice::ReadOnly)) qFatal("could not open node_types.json fixture");
        return NodeTypeRegistry::loadFromJson(file.readAll());
    }

private Q_SLOTS:
    void loadsResourceCopy()
    {
        // Also exercise loadFromResource() (the Qt-resource path used by the
        // real app), independent of the file-path fixture used everywhere
        // else in this test.
        NodeTypeRegistry registry = NodeTypeRegistry::loadFromResource();
        QCOMPARE(registry.types().size(), 24);
    }

    void loads24TypesMatchingCodegenSupportedTypes()
    {
        NodeTypeRegistry registry = loadFixture();
        const QStringList typesList = registry.types();
        const QStringList supportedList = registry.codegenSupportedTypes();
        QCOMPARE(typesList.size(), 24);
        QCOMPARE(supportedList.size(), 24);
        const QSet<QString> types(typesList.begin(), typesList.end());
        const QSet<QString> supported(supportedList.begin(), supportedList.end());
        QCOMPARE(types, supported);
    }

    void portsMatchTable()
    {
        NodeTypeRegistry registry = loadFixture();

        struct Case { QString type; QStringList in; QStringList out; };
        const QList<Case> cases = {
            {"SensorInput", {}, {"value", "timestamp_us", "valid"}},
            {"ControlLoop", {"setpoint", "measurement", "enable"}, {"command", "error"}},
            {"ActuatorOutput", {"command", "enable"}, {"applied", "active"}},
            {"Publish", {"value", "timestamp_us"}, {"published"}},
            {"Constant", {}, {"value"}},
            {"Add", {"a", "b"}, {"value"}},
            {"Subtract", {"a", "b"}, {"value"}},
            {"Multiply", {"a", "b"}, {"value"}},
            {"Gain", {"input"}, {"value"}},
            {"Compare", {"input"}, {"result"}},
            {"Saturation", {"input"}, {"value"}},
            {"Switch", {"condition", "true_value", "false_value"}, {"value"}},
            {"Time", {}, {"value"}},
            {"GPIOInput", {}, {"value"}},
            {"ADCInput", {}, {"value"}},
            {"PWMOutput", {"value", "enable"}, {"applied", "active"}},
            {"UARTInput", {}, {"value", "valid"}},
            {"UARTOutput", {"data"}, {"sent"}},
            {"EncoderInput", {}, {"position", "velocity", "valid"}},
            {"MotorOutput", {"command", "enable"}, {"applied", "active"}},
            {"WheelSpeed", {"encoder_count"}, {"speed"}},
            {"DifferentialDrive", {"linear_velocity", "angular_velocity"}, {"left_cmd", "right_cmd"}},
        };
        for (const auto &c : cases) {
            const auto params = registry.defaultParams(c.type);
            QCOMPARE(portNames(registry.inputs(c.type, params)), QSet<QString>(c.in.begin(), c.in.end()));
            QCOMPARE(portNames(registry.outputs(c.type, params)), QSet<QString>(c.out.begin(), c.out.end()));
        }
    }

    void cordicOpDynamicPorts()
    {
        NodeTypeRegistry registry = loadFixture();
        auto withOp = [](const QString &op) { return Json::Object{{"operation", Json::Value::fromString(op)}}; };

        QCOMPARE(portNames(registry.inputs("CordicOp", withOp("sin"))), QSet<QString>{"angle_rad"});
        QCOMPARE(portNames(registry.outputs("CordicOp", withOp("sin"))), QSet<QString>{"value"});
        QCOMPARE(portNames(registry.inputs("CordicOp", withOp("cos"))), QSet<QString>{"angle_rad"});
        QCOMPARE(portNames(registry.outputs("CordicOp", withOp("cos"))), QSet<QString>{"value"});
        QCOMPARE(portNames(registry.inputs("CordicOp", withOp("sincos"))), QSet<QString>{"angle_rad"});
        QCOMPARE(portNames(registry.outputs("CordicOp", withOp("sincos"))), (QSet<QString>{"sin", "cos"}));
        QCOMPARE(portNames(registry.inputs("CordicOp", withOp("atan2"))), (QSet<QString>{"y", "x"}));
        QCOMPARE(portNames(registry.outputs("CordicOp", withOp("atan2"))), QSet<QString>{"angle_rad"});
    }

    void customCodeDynamicPorts()
    {
        NodeTypeRegistry registry = loadFixture();
        Json::Object params;
        Json::Array inputs;
        inputs.push_back(Json::Value::fromString("foo"));
        inputs.push_back(Json::Value::fromString("bar"));
        params.emplace_back("inputs", Json::Value::fromArray(inputs));

        const auto in = registry.inputs("CustomCode", params);
        QCOMPARE(in.size(), 2);
        QCOMPARE(in[0].name, QStringLiteral("foo"));
        QCOMPARE(in[0].typeLabel, QStringLiteral("number"));
        QCOMPARE(in[1].name, QStringLiteral("bar"));
        QVERIFY(registry.outputs("CustomCode", params).isEmpty());
    }

    void checkConnectionSelfLoop()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        const auto check = registry.checkConnection(doc, "c1", "value", "c1", "value");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
        QVERIFY(!check.reason.isEmpty());
    }

    void checkConnectionOutputToOutputBlocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        doc.nodes.push_back(makeNode("c2", "Constant"));
        // c2's "value" is an output, not an input -> direction error.
        const auto check = registry.checkConnection(doc, "c1", "value", "c2", "value");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
    }

    void checkConnectionInputToInputBlocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("g1", "Gain"));
        doc.nodes.push_back(makeNode("g2", "Gain"));
        // g1's "input" is an input, not an output -> direction error.
        const auto check = registry.checkConnection(doc, "g1", "input", "g2", "input");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
    }

    void checkConnectionSecondEdgeIntoInputBlocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        doc.nodes.push_back(makeNode("c2", "Constant"));
        doc.nodes.push_back(makeNode("g1", "Gain"));
        GraphEdge existing;
        existing.id = "e1"; existing.fromNode = "c1"; existing.fromPort = "value";
        existing.toNode = "g1"; existing.toPort = "input";
        doc.edges.push_back(existing);

        const auto check = registry.checkConnection(doc, "c2", "value", "g1", "input");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
    }

    void checkConnectionCycleBlocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("g1", "Gain"));
        doc.nodes.push_back(makeNode("g2", "Gain"));
        GraphEdge e1; e1.id = "e1"; e1.fromNode = "g1"; e1.fromPort = "value"; e1.toNode = "g2"; e1.toPort = "input";
        doc.edges.push_back(e1);

        // g2 -> g1 would close a cycle since g1 -> g2 already exists.
        const auto check = registry.checkConnection(doc, "g2", "value", "g1", "input");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
    }

    void checkConnectionCodegenRejectedSourceBlocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("a1", "ActuatorOutput"));
        doc.nodes.push_back(makeNode("p1", "Publish"));
        // ActuatorOutput.applied is editor-visible but codegen rejects it as
        // a generated source (contract §3.2).
        const auto check = registry.checkConnection(doc, "a1", "applied", "p1", "value");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
    }

    void checkConnectionCordicAtan2Blocked()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        Json::Object atan2Params{{"operation", Json::Value::fromString("atan2")}};
        doc.nodes.push_back(makeNode("cordic1", "CordicOp", atan2Params));
        doc.nodes.push_back(makeNode("p1", "Publish"));
        const auto check = registry.checkConnection(doc, "cordic1", "angle_rad", "p1", "value");
        QCOMPARE(check.verdict, ConnectionVerdict::Block);
        QVERIFY(check.reason.contains("atan2"));
    }

    void checkConnectionWarnNumberToBoolean()
    {
        // Mirrors mbd/test_graphs/pid_test.json: Constant.value (number) ->
        // ControlLoop.enable (boolean).
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        doc.nodes.push_back(makeNode("pid1", "ControlLoop"));
        const auto check = registry.checkConnection(doc, "c1", "value", "pid1", "enable");
        QCOMPARE(check.verdict, ConnectionVerdict::Warn);
        QVERIFY(!check.reason.isEmpty());
    }

    void checkConnectionOkNumberToAny()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        doc.nodes.push_back(makeNode("p1", "Publish"));
        const auto check = registry.checkConnection(doc, "c1", "value", "p1", "value");
        QCOMPARE(check.verdict, ConnectionVerdict::Ok);
    }

    void checkConnectionOkNumberToOptionalNumber()
    {
        NodeTypeRegistry registry = loadFixture();
        GraphDocument doc;
        doc.nodes.push_back(makeNode("c1", "Constant"));
        doc.nodes.push_back(makeNode("p1", "Publish"));
        // Publish.timestamp_us is "number (optional)"; Constant.value is "number".
        const auto check = registry.checkConnection(doc, "c1", "value", "p1", "timestamp_us");
        QCOMPARE(check.verdict, ConnectionVerdict::Ok);
    }
};

QTEST_MAIN(NodeTypeRegistryTest)
#include "node_type_registry_test.moc"
