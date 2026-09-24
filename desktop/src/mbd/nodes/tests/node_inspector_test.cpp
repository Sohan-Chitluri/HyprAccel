#include "fake_inspector_source.h"
#include "node_inspector.h"
#include "node_type_registry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTest>

using namespace Hypr;

namespace {

GraphNode makeNode(const QString &id, const QString &type, Json::Object params)
{
    GraphNode node;
    node.id = id;
    node.type = type;
    node.label = id;
    node.params = std::move(params);
    return node;
}

QJsonObject loadHardwareFixture()
{
    QFile file(QStringLiteral(HARDWARE_FIXTURE_PATH));
    if (!file.open(QIODevice::ReadOnly)) qFatal("could not open hardware fixture");
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class NodeInspectorTest : public QObject {
    Q_OBJECT
private:
    NodeTypeRegistry registry_;

private Q_SLOTS:
    void initTestCase() { registry_ = NodeTypeRegistry::loadFromResource(); }

    void emptyStateShownWithNoSelection()
    {
        NodeInspector inspector(&registry_);
        auto *label = inspector.findChild<QLabel *>("emptyStateLabel");
        QVERIFY(label != nullptr);
        QCOMPARE(label->text(), QStringLiteral("Select a node on the canvas to configure its parameters."));
    }

    void fieldKindsBuiltForType()
    {
        FakeInspectorSource source;
        source.nodes.insert("gain1", makeNode("gain1", "Gain", {{"gain", Json::Value::fromNumber(2)}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.showNode("gain1");

        auto *gainEdit = inspector.findChild<QLineEdit *>("field_gain");
        QVERIFY(gainEdit != nullptr);
        QCOMPARE(gainEdit->text(), QStringLiteral("2"));
    }

    void numberFieldIgnoresPartialsAndBlurFallsBackToZero()
    {
        FakeInspectorSource source;
        source.nodes.insert("gain1", makeNode("gain1", "Gain", {{"gain", Json::Value::fromNumber(1)}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.showNode("gain1");
        auto *edit = inspector.findChild<QLineEdit *>("field_gain");
        QVERIFY(edit != nullptr);

        edit->clear();
        source.setParamCalls.clear();
        QTest::keyClicks(edit, "-");
        QVERIFY(source.setParamCalls.isEmpty()); // '-' alone is an ignored partial

        QTest::keyClicks(edit, ".");
        QVERIFY(source.setParamCalls.isEmpty()); // '-.' is still an ignored partial

        QTest::keyClicks(edit, "5");
        QVERIFY(!source.setParamCalls.isEmpty());
        QCOMPARE(source.setParamCalls.last().value.number, -0.5);

        // Blur with an incomplete value falls back to 0.
        edit->clear();
        QTest::keyClicks(edit, "-");
        source.setParamCalls.clear();
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(edit->text(), QStringLiteral("0"));
        QVERIFY(!source.setParamCalls.isEmpty());
        QCOMPARE(source.setParamCalls.last().value.number, 0.0);
    }

    void selectStoresTypedValueAndDisplaysLoadedStringAsMatchingOption()
    {
        // UARTInput.dataBits option values are numbers (5,6,7,8) per D4.
        FakeInspectorSource source;
        source.nodes.insert("uart1", makeNode("uart1", "UARTInput", {{"dataBits", Json::Value::fromString("7")}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.showNode("uart1");

        auto *combo = inspector.findChild<QComboBox *>("field_dataBits");
        QVERIFY(combo != nullptr);
        // A loaded string "7" must display as the numeric 7 option (D4).
        QCOMPARE(combo->currentText(), QStringLiteral("7"));

        // Selecting a different option stores its value with its JSON type
        // (a number, not a string), even though it came from a QComboBox.
        const int idxFor8 = combo->findText(QStringLiteral("8"));
        QVERIFY(idxFor8 >= 0);
        source.setParamCalls.clear();
        combo->setCurrentIndex(idxFor8);
        QVERIFY(!source.setParamCalls.isEmpty());
        const auto &value = source.setParamCalls.last().value;
        QVERIFY(value.isNumber());
        QCOMPARE(value.number, 8.0);
    }

    void customCodeInputsParsedFromCommaList()
    {
        FakeInspectorSource source;
        Json::Array inputs;
        inputs.push_back(Json::Value::fromString("a"));
        inputs.push_back(Json::Value::fromString("b"));
        source.nodes.insert("cc1", makeNode("cc1", "CustomCode", {{"inputs", Json::Value::fromArray(inputs)}, {"code", Json::Value::fromString("x")}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.showNode("cc1");

        auto *inputsEdit = inspector.findChild<QLineEdit *>("customCodeInputsEdit");
        QVERIFY(inputsEdit != nullptr);
        QCOMPARE(inputsEdit->text(), QStringLiteral("a, b"));

        inputsEdit->setText("c, d ,, e");
        source.setParamCalls.clear();
        QTest::keyClick(inputsEdit, Qt::Key_Return);
        QVERIFY(!source.setParamCalls.isEmpty());
        const auto &value = source.setParamCalls.last().value;
        QVERIFY(value.isArray());
        QCOMPARE(value.array.size(), size_t(3));
        QCOMPARE(value.array[0].string, QStringLiteral("c"));
        QCOMPARE(value.array[1].string, QStringLiteral("d"));
        QCOMPARE(value.array[2].string, QStringLiteral("e"));

        auto *codeEdit = inspector.findChild<QPlainTextEdit *>("customCodeEdit");
        QVERIFY(codeEdit != nullptr);
        QCOMPARE(codeEdit->toPlainText(), QStringLiteral("x"));
    }

    void hardwareDropdownFiltersAndLabels()
    {
        FakeInspectorSource source;
        source.nodes.insert("sensor1", makeNode("sensor1", "SensorInput", {{"hardwareResource", Json::Value::fromString("gpio.GPIO5")}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.setHardware(loadHardwareFixture());
        inspector.showNode("sensor1");

        auto *combo = inspector.findChild<QComboBox *>("hardwareResourceCombo");
        QVERIFY(combo != nullptr);

        QStringList items;
        for (int i = 0; i < combo->count(); ++i) items.push_back(combo->itemText(i));
        QVERIFY(items.contains(QStringLiteral("gpio.GPIO4")));            // configured, no suffix
        QVERIFY(items.contains(QStringLiteral("gpio.GPIO5 (unassigned pin)"))); // not in assignments
        QVERIFY(items.contains(QStringLiteral("adc.GPIO32 (unassigned pin)"))); // SensorInput accepts adc too
        QVERIFY(!items.contains(QStringLiteral("gpio.GPIO6")));           // unavailable, filtered out
        QCOMPARE(combo->currentText(), QStringLiteral("gpio.GPIO5 (unassigned pin)"));

        // Selecting the empty entry removes the param.
        source.removeParamCalls.clear();
        combo->setCurrentIndex(0);
        QVERIFY(!source.removeParamCalls.isEmpty());
        QCOMPARE(source.removeParamCalls.last().key, QStringLiteral("hardwareResource"));
    }

    void acceleratorDropdownFiltersByConfiguration()
    {
        FakeInspectorSource source;
        source.nodes.insert("cordic1", makeNode("cordic1", "CordicOp", {{"operation", Json::Value::fromString("sin")}}));
        NodeInspector inspector(&registry_);
        inspector.setSource(&source);
        inspector.setHardware(loadHardwareFixture());
        inspector.showNode("cordic1");

        auto *combo = inspector.findChild<QComboBox *>("acceleratorResourceCombo");
        QVERIFY(combo != nullptr);
        QStringList items;
        for (int i = 0; i < combo->count(); ++i) items.push_back(combo->itemText(i));
        QVERIFY(items.contains(QStringLiteral("accelerator.cordic")));       // has non-empty configuration
        QVERIFY(!items.contains(QStringLiteral("accelerator.unconfigured"))); // empty configuration
        QVERIFY(items.first().contains(QStringLiteral("no accelerator resource")));
    }
};

QTEST_MAIN(NodeInspectorTest)
#include "node_inspector_test.moc"
