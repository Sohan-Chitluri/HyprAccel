#include "board_catalog.h"

#include <QtTest>

class BoardCatalogTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void rejectsMalformedDescriptors_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<QString>("errorField");
        const QByteArray base = "boards:\n  demo:\n    name: Demo\n    architecture: riscv32\n    clock_freq_mhz: 100\n    pins: {gpio: [P0], pwm: [], adc: []}\n";
        const auto row = [&](const char *name, const QByteArray &from, const QByteArray &to, const char *field) {
            auto text = base;
            text.replace(from, to);
            QTest::newRow(name) << text << QString::fromLatin1(field);
        };
        QTest::newRow("empty-file") << QByteArray() << QStringLiteral("root");
        QTest::newRow("root-sequence") << QByteArray("[]") << QStringLiteral("root");
        QTest::newRow("missing-boards") << QByteArray("{}") << QStringLiteral("boards");
        QTest::newRow("empty-boards") << QByteArray("boards: {}") << QStringLiteral("boards");
        QTest::newRow("boards-sequence") << QByteArray("boards: []") << QStringLiteral("boards");
        QTest::newRow("syntax") << QByteArray("boards: [unclosed") << QStringLiteral("YAML");
        QTest::newRow("multiple-documents") << base + "---\n" + base << QStringLiteral("document");
        row("board-scalar", "    name: Demo\n    architecture: riscv32\n    clock_freq_mhz: 100\n    pins: {gpio: [P0], pwm: [], adc: []}", "    invalid", "demo");
        row("missing-name", "    name: Demo\n", "", "name");
        row("empty-name", "name: Demo", "name: ' '", "name");
        row("name-map", "name: Demo", "name: {}", "name");
        row("missing-architecture", "    architecture: riscv32\n", "", "architecture");
        row("missing-clock", "    clock_freq_mhz: 100\n", "", "clock_freq_mhz");
        for (const auto &clock : {"0", "-1", "1.5", "fast", "true", "[]", "null", "99999999999999999999"})
            row(qPrintable(QString("clock-%1").arg(clock)), "clock_freq_mhz: 100", QByteArray("clock_freq_mhz: ") + clock, "clock_freq_mhz");
        row("missing-pins", "    pins: {gpio: [P0], pwm: [], adc: []}\n", "", "pins");
        row("pins-sequence", "{gpio: [P0], pwm: [], adc: []}", "[]", "pins");
        row("empty-pins", "{gpio: [P0], pwm: [], adc: []}", "{}", "pins");
        row("gpio-map", "gpio: [P0]", "gpio: {}", "gpio");
        row("gpio-empty-string", "[P0]", "['']", "gpio");
        row("gpio-number", "[P0]", "[5]", "gpio");
        row("gpio-boolean", "[P0]", "[true]", "gpio");
        row("gpio-null", "[P0]", "[null]", "gpio");
        row("gpio-map-entry", "[P0]", "[{}]", "gpio");
        row("spi-sequence", "adc: []", "adc: [], spi: []", "spi");
        row("spi-scalar-instance", "adc: []", "adc: [], spi: {spi0: text}", "spi0");
        row("spi-empty-instance", "adc: []", "adc: [], spi: {spi0: {}}", "spi0");
        row("signal-map", "adc: []", "adc: [], uart: {uart0: {tx: {nested: P1}}}", "tx");
        row("signal-number", "adc: []", "adc: [], uart: {uart0: {tx: 3}}", "tx");
        row("accelerators-scalar", "name: Demo", "name: Demo\n    accelerators: CORDIC", "accelerators");
        row("accelerator-number", "name: Demo", "name: Demo\n    accelerators: [7]", "accelerators");
        row("duplicate-board", "  demo:", "  demo: {}\n  demo:", "duplicate");
        row("duplicate-field", "name: Demo", "name: Demo\n    name: Other", "duplicate");
        row("duplicate-role", "adc: []", "adc: [], uart: {uart0: {tx: P1, tx: P2}}", "duplicate");
    }

    void rejectsMalformedDescriptors()
    {
        QFETCH(QByteArray, contents);
        QFETCH(QString, errorField);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("descriptor.yaml");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
        file.close();
        try {
            Hypr::BoardCatalog::load(path);
            QFAIL("Expected std::runtime_error");
        } catch (const std::runtime_error &error) {
            const auto message = QString::fromUtf8(error.what());
            QVERIFY2(message.contains(path), qPrintable(message));
            QVERIFY2(message.contains(errorField, Qt::CaseInsensitive), qPrintable(message));
        } catch (...) {
            QFAIL("Parser must normalize errors to std::runtime_error");
        }
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), contents);
    }

    void missingFileReportsPath()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("missing.yaml");
        try {
            Hypr::BoardCatalog::load(path);
            QFAIL("Expected std::runtime_error");
        } catch (const std::runtime_error &error) {
            QVERIFY(QString::fromUtf8(error.what()).contains(path));
        }
        QVERIFY(!QFile::exists(path));
    }

    void mergesRepeatedPinsWithoutLosingDescriptorStrings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QString::fromUtf8("board-µ.yaml"));
        const QByteArray contents = R"(boards:
  demo:
    name: Demo
    architecture: riscv32
    clock_freq_mhz: 1
    pins:
      gpio: [P0, P0, '9']
      pwm: [P0]
      uart:
        uart0: {tx: P0, rx: BusOnly}
      i2c:
        bus: {sda: P0, scl: ClockOnly, devices: {sensor: {address: 0x68, pins: [NotAPin]}}}
    accelerators: [CORDIC, cordic]
)";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
        file.close();
        const auto boards = Hypr::BoardCatalog::load(path);
        QCOMPARE(boards.size(), 1);
        QCOMPARE(boards[0].pins.size(), 4);
        QCOMPARE(boards[0].resources.size(), 6);
        QCOMPARE(boards[0].pins[0].name, QStringLiteral("P0"));
        const QStringList expected{"gpio", "pwm", "i2c.bus.sda", "uart.uart0.tx"};
        QCOMPARE(boards[0].pins[0].functions, expected);
        QCOMPARE(boards[0].pins[1].name, QStringLiteral("9"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), contents);
    }

    void realDescriptorRemainsUnchanged()
    {
        QFile file(QStringLiteral(BOARD_CATALOG_FIXTURE));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        const auto boards = Hypr::BoardCatalog::load(file.fileName());
        QCOMPARE(boards.size(), 2);
        QVERIFY(file.seek(0));
        QCOMPARE(file.readAll(), before);
    }

    void loadsRealBoardCapabilities()
    {
        const auto boards = Hypr::BoardCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        QCOMPARE(boards[0].pins.size(), 72);
        QCOMPARE(boards[0].resources.size(), 55);
        QCOMPARE(boards[1].pins.size(), 28);
        QCOMPARE(boards[1].resources.size(), 44);
        for (const auto &board : boards) {
            QSet<QString> names;
            QSet<QString> ids;
            for (const auto &pin : board.pins) {
                QVERIFY(!pin.name.isEmpty());
                QVERIFY(!pin.functions.isEmpty());
                QVERIFY(!names.contains(pin.name));
                names.insert(pin.name);
            }
            for (const auto &resource : board.resources) {
                QVERIFY(!ids.contains(resource.id));
                ids.insert(resource.id);
                if (resource.type == "accelerator") {
                    QCOMPARE(resource.id, QStringLiteral("accelerator.cordic"));
                    QVERIFY(resource.signals.isEmpty());
                } else {
                    for (const auto &pin : resource.signals)
                        QVERIFY(names.contains(pin));
                }
            }
            QVERIFY(ids.contains("accelerator.cordic"));
        }
        QMap<QString, Hypr::Resource> resources;
        QMap<QString, QStringList> pins;
        for (const auto &resource : boards[1].resources)
            resources.insert(resource.id, resource);
        for (const auto &pin : boards[1].pins)
            pins.insert(pin.name, pin.functions);
        QCOMPARE(resources["gpio.GPIO0"].signals.value("gpio"), QStringLiteral("GPIO0"));
        QCOMPARE(resources["pwm.GPIO32"].signals.value("output"), QStringLiteral("GPIO32"));
        QCOMPARE(resources["adc.GPIO32"].signals.value("input"), QStringLiteral("GPIO32"));
        QCOMPARE(resources["spi.hspi"].signals.value("sck"), QStringLiteral("GPIO14"));
        QCOMPARE(resources["i2c.i2c0"].signals.size(), 2);
        QVERIFY(!resources.contains("i2c.imu"));
        QVERIFY(!pins.contains("devices"));
        QVERIFY(!pins.contains("104"));
        QCOMPARE(resources["uart.uart1"].signals.value("tx"), QStringLiteral("GPIO10"));
        QCOMPARE(resources["uart.uart1"].signals.value("rx"), QStringLiteral("GPIO9"));
        QVERIFY(pins.contains("GPIO9"));
        QVERIFY(pins.contains("GPIO10"));
        QVERIFY(pins["GPIO32"].contains("gpio"));
        QVERIFY(pins["GPIO32"].contains("pwm"));
        QVERIFY(pins["GPIO32"].contains("adc"));
        QVERIFY(pins["GPIO14"].contains("spi.hspi.sck"));
    }

    void loadsRealBoardMetadata()
    {
        const auto boards = Hypr::BoardCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        QCOMPARE(boards.size(), 2);
        QCOMPARE(boards[0].id, QStringLiteral("thejas32"));
        QCOMPARE(boards[0].name, QStringLiteral("THEJAS32 (ARIES v2.0)"));
        QCOMPARE(boards[0].architecture, QStringLiteral("riscv32"));
        QCOMPARE(boards[0].clockMHz, 100);
        QCOMPARE(boards[1].id, QStringLiteral("esp32"));
        QCOMPARE(boards[1].name, QStringLiteral("ESP32 (WROOM-32)"));
        QCOMPARE(boards[1].architecture, QStringLiteral("xtensa-lx6"));
        QCOMPARE(boards[1].clockMHz, 240);
    }
};

QTEST_GUILESS_MAIN(BoardCatalogTest)
#include "board_catalog_test.moc"
