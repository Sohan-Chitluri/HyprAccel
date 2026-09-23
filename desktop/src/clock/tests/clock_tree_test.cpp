#include "clock_tree.h"

#include <QtTest>

using Hypr::ClockCatalog;
using Hypr::ClockConfig;
using Hypr::ClockNode;
using Hypr::ClockTree;
using Hypr::computeClocks;
using Hypr::defaultClockConfig;

namespace {

const ClockNode *find(const ClockTree &tree, const QString &id)
{
    for (const auto &node : tree.nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}

QString writeFixture(QTemporaryDir &dir, const QByteArray &contents)
{
    const auto path = dir.filePath("boards.yaml");
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(contents);
    file.close();
    return path;
}

} // namespace

class ClockTreeTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void loadsRealBoards()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        QVERIFY(trees.contains("esp32"));
        QVERIFY(trees.contains("thejas32"));
        QVERIFY(!trees.value("esp32").nodes.isEmpty());
        QVERIFY(!trees.value("thejas32").nodes.isEmpty());
        // Topological order: every input id appears earlier in the list.
        for (const auto &tree : {trees.value("esp32"), trees.value("thejas32")}) {
            QSet<QString> seen;
            for (const auto &node : tree.nodes) {
                for (const auto &input : node.inputs)
                    QVERIFY2(seen.contains(input), qPrintable(tree.boardId + "." + node.id));
                seen.insert(node.id);
            }
        }
    }

    void esp32DefaultsGive240CpuAnd80Apb()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("esp32");
        const auto config = defaultClockConfig(tree);
        const auto result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 240.0);
        QCOMPARE(result.freqMHz.value("apb_clk"), 80.0);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
    }

    void esp32AlternativeCpuFrequencyIsDocumented()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("esp32");
        auto config = defaultClockConfig(tree);
        // PLL 320 MHz (x8) / CPU_CLKDIV 2 -> 160 MHz (documented ESP-IDF combination).
        config.selections.insert("bbpll", "8");
        auto result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 160.0);
        QVERIFY(result.errors.isEmpty());

        // PLL 320 MHz (x8) / CPU_CLKDIV 4 -> 80 MHz.
        config.selections.insert("cpu_div", "4");
        result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 80.0);
        QVERIFY(result.errors.isEmpty());
    }

    void esp32ApbTracksCpuSource()
    {
        const auto tree = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE)).value("esp32");
        auto config = defaultClockConfig(tree);
        // PLL 320 MHz path: hardware still holds APB (and APB peripherals) at 80 MHz.
        config.selections.insert("bbpll", "8");
        auto result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("apb_clk"), 80.0);
        QCOMPARE(result.freqMHz.value("uart_clk"), 80.0);
        QCOMPARE(result.freqMHz.value("ref_tick"), 1.0);
        // XTAL path: APB_CLK = CPU_CLK.
        config.selections.insert("cpu_mux", "xtal");
        config.selections.insert("cpu_div", "1");
        result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 40.0);
        QCOMPARE(result.freqMHz.value("apb_clk"), 40.0);
        QCOMPARE(result.freqMHz.value("spi_clk"), 40.0);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
        // A config cannot override a hardware-coupled node.
        config.selections.insert("apb_clk", "apb_pll");
        QCOMPARE(computeClocks(tree, config).freqMHz.value("apb_clk"), 40.0);
    }

    void rejectsInvalidFollows()
    {
        const QByteArray base = "boards:\n  demo:\n    clocks:\n      nodes:\n"
            "        - {id: a, label: A, kind: source, freq_mhz: 10}\n"
            "        - {id: m, label: M, kind: mux, inputs: [a], default: a, editable: true}\n"
            "        - {id: f, label: F, kind: mux, inputs: [a], default: a, FOLLOWS}\n";
        const auto check = [&](const QByteArray &follows, const char *field) {
            QTemporaryDir dir;
            auto text = base;
            text.replace("FOLLOWS", follows);
            try {
                ClockCatalog::load(writeFixture(dir, text));
                QFAIL(qPrintable("expected failure for " + QString(follows)));
            } catch (const std::runtime_error &error) {
                QVERIFY2(QString(error.what()).contains(field), error.what());
            }
        };
        check("follows: {node: nope, map: {a: a}}", "follows.node");
        check("follows: {node: m, map: {zz: a}}", "follows.map.zz");
        check("follows: {node: m, map: {a: zz}}", "follows.map.a");
        check("editable: true, follows: {node: m, map: {a: a}}", "follows");
    }

    void overLimitSelectionProducesError()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("esp32");
        auto config = defaultClockConfig(tree);
        config.selections.insert("cpu_div", "1"); // 480 MHz / 1 = 480 MHz > 240 MHz max.
        const auto result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 480.0);
        QVERIFY2(!result.errors.isEmpty(), "expected an over-limit error");
        QVERIFY(result.errors.join("; ").contains("exceeds max", Qt::CaseInsensitive));
    }

    void invalidSelectionFallsBackToDefault()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("esp32");
        auto config = defaultClockConfig(tree);
        config.selections.insert("cpu_div", "5"); // not one of [1,2,3,4,6]
        const auto result = computeClocks(tree, config);
        QCOMPARE(result.freqMHz.value("cpu_div"), 240.0); // falls back to default "2"
        QVERIFY2(!result.errors.isEmpty(), "expected an invalid-selection error");
        QVERIFY(result.errors.join("; ").contains("invalid selection", Qt::CaseInsensitive));

        auto muxConfig = defaultClockConfig(tree);
        muxConfig.selections.insert("cpu_mux", "does_not_exist");
        const auto muxResult = computeClocks(tree, muxConfig);
        QCOMPARE(muxResult.freqMHz.value("cpu_mux"), muxResult.freqMHz.value("bbpll"));
        QVERIFY(!muxResult.errors.isEmpty());
    }

    void thejas32LoadsAndIsFixedAt100MHz()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("thejas32");
        const auto result = computeClocks(tree, defaultClockConfig(tree));
        QCOMPARE(result.freqMHz.value("cpu_clk"), 100.0);
        QVERIFY(result.errors.isEmpty());
        const auto *osc = find(tree, "osc");
        QVERIFY(osc);
        QVERIFY(!osc->editable);
        QVERIFY(osc->note.contains("unverified", Qt::CaseInsensitive));
    }

    void defaultConfigOnlyListsEditableNodes()
    {
        const auto trees = ClockCatalog::load(QStringLiteral(BOARD_CATALOG_FIXTURE));
        const auto tree = trees.value("esp32");
        const auto config = defaultClockConfig(tree);
        for (auto it = config.selections.constBegin(); it != config.selections.constEnd(); ++it) {
            const auto *node = find(tree, it.key());
            QVERIFY(node);
            QVERIFY(node->editable);
        }
        QVERIFY(config.selections.contains("cpu_div"));
        QVERIFY(!config.selections.contains("apb_clk")); // not editable
    }

    void rejectsMalformedClocks_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<QString>("errorField");

        const QByteArray base =
            "boards:\n"
            "  demo:\n"
            "    name: Demo\n"
            "    architecture: riscv32\n"
            "    clock_freq_mhz: 100\n"
            "    pins: {gpio: [P0], pwm: [], adc: []}\n"
            "    clocks:\n"
            "      nodes:\n"
            "        - id: osc\n"
            "          label: OSC\n"
            "          kind: source\n"
            "          freq_mhz: 100\n"
            "          editable: false\n"
            "        - id: cpu\n"
            "          label: CPU\n"
            "          kind: gate\n"
            "          inputs: [\"osc\"]\n"
            "          editable: false\n"
            "      peripheral_outputs: [\"cpu\"]\n";

        const auto row = [&](const char *name, const QByteArray &from, const QByteArray &to, const char *field) {
            auto text = base;
            text.replace(from, to);
            QTest::newRow(name) << text << QString::fromLatin1(field);
        };

        row("unknown-input", "inputs: [\"osc\"]", "inputs: [\"missing\"]", "unknown input");
        row("self-cycle", "kind: gate\n          inputs: [\"osc\"]",
            "kind: gate\n          inputs: [\"cpu\"]", "unknown input");
        row("duplicate-id", "          editable: false\n        - id: cpu",
            "          editable: false\n        - id: osc\n          label: Dup\n          kind: source\n          freq_mhz: 1\n          editable: false\n        - id: cpu",
            "duplicate");
        row("bad-kind", "kind: source", "kind: bogus", "kind");
        row("missing-freq-on-source", "          freq_mhz: 100\n", "", "freq_mhz");
        row("source-with-inputs", "kind: gate\n          inputs: [\"osc\"]",
            "kind: source\n          freq_mhz: 5\n          inputs: [\"osc\"]", "inputs");
        row("mux-empty-inputs", "kind: gate\n          inputs: [\"osc\"]",
            "kind: mux\n          inputs: []\n          default: osc", "inputs");
        row("options-on-mux", "kind: gate\n          inputs: [\"osc\"]",
            "kind: mux\n          inputs: [\"osc\"]\n          default: osc\n          options: [1, 2]", "options");
        row("pll-default-not-in-options", "kind: gate\n          inputs: [\"osc\"]",
            "kind: pll\n          inputs: [\"osc\"]\n          options: [8, 12]\n          default: \"10\"", "default");
        row("mux-default-not-input", "kind: gate\n          inputs: [\"osc\"]",
            "kind: mux\n          inputs: [\"osc\"]\n          default: nope", "default");
        row("unknown-peripheral-output", "peripheral_outputs: [\"cpu\"]",
            "peripheral_outputs: [\"missing\"]", "unknown node id");
        row("bad-id-chars", "id: cpu\n          label: CPU", "id: CPU-bad\n          label: CPU", "id");
    }

    void rejectsMalformedClocks()
    {
        QFETCH(QByteArray, contents);
        QFETCH(QString, errorField);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = writeFixture(dir, contents);
        try {
            ClockCatalog::load(path);
            QFAIL("Expected std::runtime_error");
        } catch (const std::runtime_error &error) {
            const auto message = QString::fromUtf8(error.what());
            QVERIFY2(message.contains(path), qPrintable(message));
            QVERIFY2(message.contains(errorField, Qt::CaseInsensitive), qPrintable(message));
        } catch (...) {
            QFAIL("Parser must normalize errors to std::runtime_error");
        }
    }

    void boardsWithoutClocksAreAbsent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray contents =
            "boards:\n"
            "  demo:\n"
            "    name: Demo\n"
            "    architecture: riscv32\n"
            "    clock_freq_mhz: 100\n"
            "    pins: {gpio: [P0], pwm: [], adc: []}\n";
        const auto path = writeFixture(dir, contents);
        const auto trees = ClockCatalog::load(path);
        QVERIFY(!trees.contains("demo"));
        QVERIFY(trees.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ClockTreeTest)
#include "clock_tree_test.moc"
