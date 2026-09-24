#include "node_palette.h"
#include "node_type_registry.h"

#include <QMimeData>
#include <QTest>

using namespace Hypr;

class NodePaletteTest : public QObject {
    Q_OBJECT
private:
    NodeTypeRegistry registry_;

private Q_SLOTS:
    void initTestCase()
    {
        registry_ = NodeTypeRegistry::loadFromResource();
    }

    void categoriesInWebOrder()
    {
        NodePalette palette(&registry_);
        const QStringList expected = {
            "Inputs & Sensors", "Outputs & Actuators", "Accelerator", "Control",
            "Robotics", "Data / Telemetry", "Signal Processing", "Communication",
            "Timing", "Hardware", "Custom",
        };
        QCOMPARE(palette.visibleCategoryNames(), expected);
    }

    void emptyCategoriesShowPlaceholder()
    {
        NodePalette palette(&registry_);
        QCOMPARE(palette.placeholderFor("Communication"), QStringLiteral("Future expansion"));
        QCOMPARE(palette.placeholderFor("Hardware"), QStringLiteral("Future expansion"));
        QVERIFY(palette.placeholderFor("Timing").isEmpty()); // not empty: has Time
    }

    void categoryItemsMatchWebOrder()
    {
        NodePalette palette(&registry_);
        QCOMPARE(palette.visibleTypesInCategory("Inputs & Sensors"),
                 QStringList({"SensorInput", "GPIOInput", "ADCInput", "UARTInput", "EncoderInput"}));
        QCOMPARE(palette.visibleTypesInCategory("Robotics"),
                 QStringList({"WheelSpeed", "DifferentialDrive"}));
    }

    void searchFiltersByTypeKey()
    {
        NodePalette palette(&registry_);
        palette.setFilterText("cordic");
        QCOMPARE(palette.visibleTypesInCategory("Accelerator"), QStringList({"CordicOp"}));
        QVERIFY(!palette.visibleCategoryNames().contains("Robotics"));
    }

    void searchFiltersByDescription()
    {
        NodePalette palette(&registry_);
        // "PID" only appears in ControlLoop's description, not its key.
        palette.setFilterText("pid controller");
        QCOMPARE(palette.visibleTypesInCategory("Control"), QStringList({"ControlLoop"}));
    }

    void searchClearedShowsEverythingAgain()
    {
        NodePalette palette(&registry_);
        palette.setFilterText("cordic");
        palette.setFilterText("");
        QCOMPARE(palette.visibleCategoryNames().size(), 11);
        QCOMPARE(palette.visibleTypesInCategory("Signal Processing").size(), 8);
    }

    void dragMimePayloadIsUtf8TypeKey()
    {
        auto *mime = NodePalette::mimeDataForType(QStringLiteral("CordicOp"));
        QVERIFY(mime != nullptr);
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-hypraccel-node")));
        QCOMPARE(mime->data(QStringLiteral("application/x-hypraccel-node")), QByteArray("CordicOp"));
        delete mime;
    }
};

QTEST_MAIN(NodePaletteTest)
#include "node_palette_test.moc"
