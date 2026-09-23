#include "pin_check_runner.h"
#include "project_store.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

using namespace Hypr;

namespace {
const Board *findBoard(const QList<Board> &boards, const QString &id)
{
    for (const auto &board : boards)
        if (board.id == id) return &board;
    return nullptr;
}
}

class PinCheckRunnerTest : public QObject {
    Q_OBJECT
    QList<Board> boards;
private Q_SLOTS:
    void initTestCase() { boards = BoardCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE)); }

    void payloadSerializationMatchesProjectStoreShape()
    {
        const Board *esp32 = findBoard(boards, "esp32");
        QVERIFY(esp32);
        QMap<QString, QString> assignments{{"GPIO1", "uart.uart0.tx"}};
        const auto payload = buildPinCheckPayload(*esp32, assignments);
        QJsonParseError error{};
        const auto doc = QJsonDocument::fromJson(payload, &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        const auto obj = doc.object();
        QCOMPARE(obj.value("board").toString(), QString("esp32"));
        const auto hardware = obj.value("hardware").toObject();
        QCOMPARE(hardware.value("board").toString(), QString("esp32"));
        QVERIFY(hardware.value("assignments").isArray());
        const auto assignmentsArr = hardware.value("assignments").toArray();
        QCOMPARE(assignmentsArr.size(), 1);
        const auto entry = assignmentsArr.at(0).toObject();
        QCOMPARE(entry.value("pin").toString(), QString("GPIO1"));
        QCOMPARE(entry.value("role").toString(), QString("tx"));
        QCOMPARE(entry.value("resource").toString(), QString("uart.uart0"));
        // Same shape ProjectStore::hardwareJson() produces directly.
        const auto direct = ProjectStore::hardwareJson(*esp32, assignments);
        QCOMPARE(QJsonDocument(hardware).toJson(QJsonDocument::Compact),
                 QJsonDocument(direct).toJson(QJsonDocument::Compact));
    }

    void parsesSuccessOutput()
    {
        const QByteArray out = R"({"errors":[{"code":"BUS_INCOMPLETE","message":"I2C0 is missing SCL.","resource":"i2c.i2c0"}],"warnings":[]})";
        const auto result = parsePinCheckOutput(out, 0, false);
        QVERIFY(result.available);
        QCOMPARE(result.errors.size(), 1);
        QCOMPARE(result.errors[0].code, QString("BUS_INCOMPLETE"));
        QCOMPARE(result.warnings.size(), 0);
    }

    void parsesWarningOnlyOutput()
    {
        const QByteArray out = R"({"errors":[],"warnings":[{"code":"BUS_PARTIAL","message":"UART0 TX assigned without RX.","resource":"uart.uart0"}]})";
        const auto result = parsePinCheckOutput(out, 0, false);
        QVERIFY(result.available);
        QCOMPARE(result.errors.size(), 0);
        QCOMPARE(result.warnings.size(), 1);
        QCOMPARE(result.warnings[0].code, QString("BUS_PARTIAL"));
    }

    void parsesErrorExitAsUnavailable()
    {
        const QByteArray out = R"({"error":"Unknown board 'nope'."})";
        const auto result = parsePinCheckOutput(out, 2, false);
        QVERIFY(!result.available);
        QVERIFY(result.unavailableReason.contains("Unknown board"));
    }

    void crashIsUnavailable()
    {
        const auto result = parsePinCheckOutput(QByteArray(), -1, true, "boom");
        QVERIFY(!result.available);
        QCOMPARE(result.unavailableReason, QString("boom"));
    }

    void malformedJsonIsUnavailable()
    {
        const auto result = parsePinCheckOutput(QByteArrayLiteral("not json"), 0, false);
        QVERIFY(!result.available);
    }

    // --- real end-to-end runs against the actual script -------------------
    void endToEndI2cSdaOnlyIsErrorCase()
    {
        if (findNodeExecutable().isEmpty())
            QSKIP("node executable not found; set HYPRACCEL_NODE to test end-to-end.");
        const Board *esp32 = findBoard(boards, "esp32");
        QVERIFY(esp32);
        PinCheckRunner runner(QStringLiteral(PIN_CHECK_SCRIPT_FIXTURE), QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto result = runner.runSync(*esp32, {{"GPIO21", "i2c.i2c0.sda"}});
        QVERIFY2(result.available, qPrintable(result.unavailableReason));
        QCOMPARE(result.warnings.size(), 0);
        QCOMPARE(result.errors.size(), 1);
        QCOMPARE(result.errors[0].code, QString("BUS_INCOMPLETE"));
    }

    void endToEndUartTxOnlyIsWarningCase()
    {
        if (findNodeExecutable().isEmpty())
            QSKIP("node executable not found; set HYPRACCEL_NODE to test end-to-end.");
        const Board *esp32 = findBoard(boards, "esp32");
        QVERIFY(esp32);
        PinCheckRunner runner(QStringLiteral(PIN_CHECK_SCRIPT_FIXTURE), QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto result = runner.runSync(*esp32, {{"GPIO1", "uart.uart0.tx"}});
        QVERIFY2(result.available, qPrintable(result.unavailableReason));
        QCOMPARE(result.errors.size(), 0);
        QCOMPARE(result.warnings.size(), 1);
        QCOMPARE(result.warnings[0].code, QString("BUS_PARTIAL"));
    }

    void asyncRequestCheckEmitsResultReady()
    {
        if (findNodeExecutable().isEmpty())
            QSKIP("node executable not found; set HYPRACCEL_NODE to test end-to-end.");
        const Board *esp32 = findBoard(boards, "esp32");
        QVERIFY(esp32);
        PinCheckRunner runner(QStringLiteral(PIN_CHECK_SCRIPT_FIXTURE), QStringLiteral(BOARD_CATALOG_FIXTURE));
        runner.setDebounceMs(10);
        QSignalSpy spy(&runner, &PinCheckRunner::resultReady);
        runner.requestCheck(*esp32, {{"GPIO1", "uart.uart0.tx"}});
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);
    }

    void nodeMissingReportsUnavailableNotSilentSuccess()
    {
        qputenv("HYPRACCEL_NODE", "/definitely/not/a/real/node/executable");
        const Board *esp32 = findBoard(boards, "esp32");
        QVERIFY(esp32);
        PinCheckRunner runner(QStringLiteral(PIN_CHECK_SCRIPT_FIXTURE), QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto result = runner.runSync(*esp32, {{"GPIO1", "uart.uart0.tx"}});
        qunsetenv("HYPRACCEL_NODE");
        QVERIFY(!result.available);
        QVERIFY(!result.unavailableReason.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PinCheckRunnerTest)
#include "pin_check_runner_test.moc"
