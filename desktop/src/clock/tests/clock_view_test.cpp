#include "clock_view.h"

#include <QComboBox>
#include <QLabel>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>

class ClockViewTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void setBoardCreatesEditableCombos()
    {
        ClockView view(QStringLiteral(BOARD_CATALOG_FIXTURE));
        view.setBoard(QStringLiteral("esp32"));
        QCOMPARE(view.boardId(), QStringLiteral("esp32"));

        auto *cpuDiv = view.findChild<QComboBox *>(QStringLiteral("clock:cpu_div"));
        QVERIFY(cpuDiv);
        auto *apb = view.findChild<QComboBox *>(QStringLiteral("clock:apb_clk"));
        QVERIFY(!apb); // apb_clk is not editable, so it must not get a combo.

        auto *outputs = view.findChild<QTableWidget *>(QStringLiteral("clockOutputs"));
        QVERIFY(outputs);
        QCOMPARE(outputs->rowCount(), 4);

        auto *errors = view.findChild<QLabel *>(QStringLiteral("clockErrors"));
        QVERIFY(errors);
    }

    void changingComboEmitsSignalAndUpdatesOutputs()
    {
        ClockView view(QStringLiteral(BOARD_CATALOG_FIXTURE));
        view.setBoard(QStringLiteral("esp32"));
        QSignalSpy spy(&view, &ClockView::configChanged);

        auto *bbpll = view.findChild<QComboBox *>(QStringLiteral("clock:bbpll"));
        QVERIFY(bbpll);
        const auto index320 = bbpll->findData(QStringLiteral("8"));
        QVERIFY(index320 >= 0);
        bbpll->setCurrentIndex(index320);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(view.config().selections.value("bbpll"), QStringLiteral("8"));
        QCOMPARE(view.result().freqMHz.value("bbpll"), 320.0);

        auto *cpuValue = view.findChild<QLabel *>(QStringLiteral("clockValue:cpu_div"));
        QVERIFY(cpuValue);
        QVERIFY2(cpuValue->text().contains("160"), qPrintable(cpuValue->text()));
    }

    void setConfigRoundTripsWithoutEmitting()
    {
        ClockView view(QStringLiteral(BOARD_CATALOG_FIXTURE));
        view.setBoard(QStringLiteral("esp32"));

        Hypr::ClockConfig config;
        config.selections.insert("bbpll", "8");
        config.selections.insert("cpu_div", "4");
        config.selections.insert("bogus_node", "irrelevant"); // dropped: unknown node
        config.selections.insert("cpu_mux", "does_not_exist"); // dropped: not a valid input

        QSignalSpy spy(&view, &ClockView::configChanged);
        view.setConfig(config);
        QCOMPARE(spy.count(), 0);

        const auto roundTripped = view.config();
        QCOMPARE(roundTripped.selections.value("bbpll"), QStringLiteral("8"));
        QCOMPARE(roundTripped.selections.value("cpu_div"), QStringLiteral("4"));
        QVERIFY(!roundTripped.selections.contains("bogus_node"));
        QVERIFY(!roundTripped.selections.contains("cpu_mux"));
        QCOMPARE(view.result().freqMHz.value("cpu_div"), 80.0);
    }

    void unknownBoardShowsPlaceholder()
    {
        ClockView view(QStringLiteral(BOARD_CATALOG_FIXTURE));
        view.setBoard(QStringLiteral("does-not-exist"));
        auto *placeholder = view.findChild<QLabel *>(QStringLiteral("clockPlaceholder"));
        QVERIFY(placeholder);
        QVERIFY(placeholder->isVisible() || !placeholder->text().isEmpty());
        QVERIFY(!placeholder->text().isEmpty());
        auto *combo = view.findChild<QComboBox *>(QStringLiteral("clock:cpu_div"));
        QVERIFY(!combo);
    }

    void thejas32PlaceholderOrFixedTree()
    {
        ClockView view(QStringLiteral(BOARD_CATALOG_FIXTURE));
        view.setBoard(QStringLiteral("thejas32"));
        QCOMPARE(view.result().freqMHz.value("cpu_clk"), 100.0);
        // No editable nodes documented for thejas32.
        auto *anyCombo = view.findChild<QComboBox *>(QStringLiteral("clock:osc"));
        QVERIFY(!anyCombo);
    }
};

QTEST_MAIN(ClockViewTest)
#include "clock_view_test.moc"
