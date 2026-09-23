#include "pinout_scene.h"

#include <QGraphicsRectItem>
#include <QSet>
#include <QSignalSpy>
#include <QtTest>

using namespace Hypr;

class PinoutSceneTest : public QObject {
    Q_OBJECT
    QList<Board> boards;
    const Board *board(const QString &id) const
    {
        for (const auto &candidate : boards)
            if (candidate.id == id) return &candidate;
        return nullptr;
    }
private Q_SLOTS:
    void initTestCase() { boards = BoardCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE)); }

    void esp32HasAllPinLeads()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        PinoutScene scene(&model);

        int pinItems = 0;
        for (auto *item : scene.items())
            if (!item->data(0).toString().isEmpty()) ++pinItems;
        QCOMPARE(pinItems, 28);
    }

    void assignmentRecoloursSameItem()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        PinoutScene scene(&model);

        QGraphicsItem *before = scene.pinItem("GPIO1");
        QVERIFY(before != nullptr);
        QVERIFY(model.assign("GPIO1", "uart.uart0.tx"));
        QGraphicsItem *after = scene.pinItem("GPIO1");
        QCOMPARE(after, before);

        auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(after);
        QVERIFY(rect != nullptr);
        QCOMPARE(rect->brush().color(), PinoutStyle::colorFor(FunctionKind::Uart));
    }

    void selectionSurvivesRecolor()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        PinoutScene scene(&model);

        QGraphicsItem *lead = scene.pinItem("GPIO1");
        QVERIFY(lead != nullptr);
        lead->setSelected(true);
        QVERIFY(lead->isSelected());

        QVERIFY(model.assign("GPIO1", "uart.uart0.tx"));
        QVERIFY(lead->isSelected());
        QCOMPARE(scene.pinItem("GPIO1"), lead);
    }

    void setBoardRebuilds()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        PinoutScene scene(&model);
        QVERIFY(scene.pinItem("GPIO1") != nullptr);
        QVERIFY(scene.pinItem("PWM7") == nullptr);

        model.setBoard(board("thejas32"));
        QVERIFY(scene.pinItem("PWM7") != nullptr);
    }

    void noBoardHasNoPinItems()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        PinoutScene scene(&model);
        QVERIFY(scene.pinItem("GPIO1") != nullptr);

        model.setBoard(nullptr);
        int pinItems = 0;
        for (auto *item : scene.items())
            if (item->data(1).toString() == "pin") ++pinItems;
        QCOMPARE(pinItems, 0);
    }

    void colorsAreDistinct()
    {
        const FunctionKind kinds[] = {
            FunctionKind::Unassigned, FunctionKind::Gpio, FunctionKind::Uart, FunctionKind::Spi,
            FunctionKind::I2c,        FunctionKind::Pwm,  FunctionKind::Adc,
        };
        QSet<QRgb> seen;
        for (auto kind : kinds) seen.insert(PinoutStyle::colorFor(kind).rgb());
        QCOMPARE(seen.size(), 7);
    }

    void legendHasSevenKindRows()
    {
        QWidget *legend = PinoutStyle::createLegend();
        QCOMPARE(legend->objectName(), QString("pinLegend"));
        int withKind = 0;
        const auto children = legend->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
        for (auto *child : children)
            if (!child->property("kind").toString().isEmpty()) ++withKind;
        QCOMPARE(withKind, 7);
        delete legend;
    }
};

QTEST_MAIN(PinoutSceneTest)
#include "pinout_scene_test.moc"
