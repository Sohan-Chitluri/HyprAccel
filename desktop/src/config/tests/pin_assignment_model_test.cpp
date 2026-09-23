#include "pin_assignment_model.h"

#include <QSignalSpy>
#include <QtTest>

using namespace Hypr;

class PinAssignmentModelTest : public QObject {
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

    void kindsAndResourceRoles()
    {
        QCOMPARE(kindOf("uart.uart0.tx"), FunctionKind::Uart);
        QCOMPARE(kindOf(""), FunctionKind::Unassigned);
        QCOMPARE(kindName(FunctionKind::I2c), QString("i2c"));
        const auto rr = resourceRoleFor("GPIO1", "uart.uart0.tx");
        QCOMPARE(rr.resource, QString("uart.uart0"));
        QCOMPARE(rr.role, QString("tx"));
        QCOMPARE(resourceRoleFor("GPIO25", "pwm").resource, QString("pwm.GPIO25"));
        QCOMPARE(functionForResourceRole("uart.uart0", "tx"), QString("uart.uart0.tx"));
        QCOMPARE(functionForResourceRole("adc.GPIO32", "input"), QString("adc"));
        QCOMPARE(functionForResourceRole("accelerator.cordic", ""), QString());
    }

    void assignValidatesAndSignals()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        QSignalSpy changed(&model, &PinAssignmentModel::assignmentChanged);
        QVERIFY(model.validFunctions("GPIO1").contains("uart.uart0.tx"));
        QVERIFY(model.assign("GPIO1", "uart.uart0.tx"));
        QCOMPARE(model.kindFor("GPIO1"), FunctionKind::Uart);
        QCOMPARE(changed.count(), 1);
        QVERIFY(!model.assign("GPIO1", "adc"));      // not a GPIO1 capability
        QVERIFY(!model.assign("NOPE", "gpio"));
        QCOMPARE(model.functionFor("GPIO1"), QString("uart.uart0.tx"));
        QVERIFY(model.assign("GPIO1", "gpio"));
        model.clear("GPIO1");
        QVERIFY(model.assignments().isEmpty());
    }

    void setAssignmentsDropsInvalid()
    {
        PinAssignmentModel model;
        model.setBoard(board("esp32"));
        QSignalSpy reset(&model, &PinAssignmentModel::reset);
        const auto dropped = model.setAssignments({{"GPIO25", "pwm"}, {"GPIO1", "adc"}, {"GPIO99", "gpio"}});
        QCOMPARE(model.assignments().size(), 1);
        QCOMPARE(dropped.size(), 2);
        QCOMPARE(reset.count(), 1);
        model.setBoard(board("thejas32"));
        QVERIFY(model.assignments().isEmpty());
    }
};

QTEST_MAIN(PinAssignmentModelTest)
#include "pin_assignment_model_test.moc"
