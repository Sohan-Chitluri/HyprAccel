#include "theme.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtTest>

class ThemeTest : public QObject {
    Q_OBJECT
private slots:
    void tokensAndPalette() {
        struct Reference { const char *name; double l, c, h; };
        const Reference refs[] = {
            {"background",.17,.008,250}, {"foreground",.93,.005,250},
            {"card",.205,.008,250}, {"popover",.225,.009,250},
            {"primary",.68,.13,205}, {"primaryFG",.16,.01,250},
            {"secondary",.26,.008,250}, {"muted",.24,.008,250},
            {"mutedFG",.63,.012,250}, {"accent",.28,.012,250},
            {"border",.28,.008,250}, {"input",.30,.008,250},
            {"ok",.72,.15,155}, {"warn",.78,.14,80}, {"err",.63,.2,25},
            {"power",.62,.1,300}, {"surface1",.19,.008,250},
            {"surface2",.225,.008,250}, {"surface3",.26,.009,250}
        };
        for (const auto &r : refs)
            QCOMPARE(StudioTheme::token(r.name), StudioTheme::fromOklch(r.l,r.c,r.h));
        QVERIFY(!StudioTheme::token("not-a-token").isValid());
        const auto p = StudioTheme::palette();
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            QCOMPARE(p.color(group,QPalette::Window), StudioTheme::token("background"));
            QCOMPARE(p.color(group,QPalette::Base), StudioTheme::token("card"));
            QCOMPARE(p.color(group,QPalette::AlternateBase), StudioTheme::token("surface2"));
            QCOMPARE(p.color(group,QPalette::Highlight), StudioTheme::token("primary"));
            QCOMPARE(p.color(group,QPalette::HighlightedText), StudioTheme::token("primaryFG"));
        }
        QCOMPARE(p.color(QPalette::Active,QPalette::Text), StudioTheme::token("foreground"));
        QCOMPARE(p.color(QPalette::Disabled,QPalette::Text), StudioTheme::token("mutedFG"));
        QCOMPARE(p.color(QPalette::ToolTipBase), StudioTheme::token("popover"));
    }
    void stylesheetAndApply() {
        const QString css = StudioTheme::stylesheet();
        for (const auto *selector : {"QComboBox", "QLineEdit", "QTableWidget", "QTreeWidget", "QSplitter::handle", "QDialog", "QLabel#muted", "QLabel#sectionTitle", "QLabel#eyebrow", "QLabel#brand", "QTabBar::tab:selected"})
            QVERIFY2(css.contains(selector), selector);
        QVERIFY(css.contains(StudioTheme::token("background").name()));
        QVERIFY(css.contains(StudioTheme::token("primary").name()));
        QVERIFY(!css.contains('@'));
        auto &app = *qobject_cast<QApplication *>(QCoreApplication::instance());
        StudioTheme::apply(app);
        QCOMPARE(app.styleSheet(), css);
        QCOMPARE(app.palette().color(QPalette::Window),StudioTheme::token("background"));
        QTest::failOnWarning();
        QDialog dialog;
        auto *layout = new QVBoxLayout(&dialog);
        auto *brand = new QLabel("HyprAccel");
        brand->setObjectName("brand");
        layout->addWidget(brand);
        layout->addWidget(new QLineEdit("Peripheral name"));
        auto *combo = new QComboBox;
        combo->addItems({"GPIO", "UART"});
        layout->addWidget(combo);
        auto *splitter = new QSplitter;
        auto *tree = new QTreeWidget;
        tree->setHeaderLabel("Peripherals");
        new QTreeWidgetItem(tree, {"GPIO"});
        splitter->addWidget(tree);
        splitter->addWidget(new QTableWidget(2,2));
        layout->addWidget(splitter);
        dialog.resize(640,480);
        dialog.ensurePolished();
        const QImage image = dialog.grab().toImage();
        QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(0,0).name(),StudioTheme::token("background").name());
        QCOMPARE(brand->palette().color(QPalette::WindowText).name(),StudioTheme::token("primary").name());
        StudioTheme::apply(app);
        QCOMPARE(app.styleSheet(),css);
    }
    void conversion() {
        QCOMPARE(StudioTheme::fromOklch(0, 0, 0), QColor(Qt::black));
        QCOMPARE(StudioTheme::fromOklch(1, 0, 0), QColor(Qt::white));
        // Neutral OKLab L=.5 has linear sRGB=.125; gamma encoding gives .38857286.
        const QColor gray = StudioTheme::fromOklch(.5, 0, 250);
        QVERIFY(qAbs(gray.redF() - .38857286) < .0001);
        QVERIFY(qAbs(gray.greenF() - .38857286) < .0001);
        QVERIFY(qAbs(gray.blueF() - .38857286) < .0001);
        // Published sRGB red in OKLab: L=.6279553606, a=.2248630611, b=.1258462985.
        const QColor red = StudioTheme::fromOklch(.6279553606, .2576833077, 29.23388519);
        QCOMPARE(red.red(), 255);
        QCOMPARE(red.green(), 0);
        QCOMPARE(red.blue(), 0);
        QCOMPARE(StudioTheme::fromOklch(.5, 0, 0, -1).alpha(), 0);
        QCOMPARE(StudioTheme::fromOklch(.5, 0, 0, 2).alpha(), 255);
        QVERIFY(qAbs(StudioTheme::fromOklch(.5, 0, 0, .4).alphaF() - .4) < .0001);
    }
};
QTEST_MAIN(ThemeTest)
#include "theme_test.moc"
