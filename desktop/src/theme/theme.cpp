#include "theme.h"
#include <QApplication>
#include <cmath>
#include <algorithm>

namespace StudioTheme {
QColor fromOklch(double L, double C, double degrees, double alpha) {
    const double angle = degrees * 3.14159265358979323846 / 180.;
    const double a = C * std::cos(angle), b = C * std::sin(angle);
    const double lp = L + .3963377774 * a + .2158037573 * b;
    const double mp = L - .1055613458 * a - .0638541728 * b;
    const double sp = L - .0894841775 * a - 1.2914855480 * b;
    const double l = lp * lp * lp, m = mp * mp * mp, s = sp * sp * sp;
    const auto encode = [](double v) {
        return std::clamp(v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1. / 2.4) - .055, 0., 1.);
    };
    return QColor::fromRgbF(encode(4.0767416621*l - 3.3077115913*m + .2309699292*s),
                            encode(-1.2684380046*l + 2.6097574011*m - .3413193965*s),
                            encode(-.0041960863*l - .7034186147*m + 1.7076147010*s),
                            std::clamp(alpha, 0., 1.));
}
namespace {
struct Token { const char *name; double l, c, h; };
constexpr Token tokens[] = {
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
}
QColor token(const QString &name) {
    for (const auto &t : tokens)
        if (name == QLatin1String(t.name)) return fromOklch(t.l,t.c,t.h);
    return {};
}
QPalette palette() {
    QPalette p;
    const auto bind = [&p](QPalette::ColorRole role, const char *name) {
        p.setColor(QPalette::All, role, token(name));
    };
    bind(QPalette::Window,"background"); bind(QPalette::WindowText,"foreground");
    bind(QPalette::Base,"card"); bind(QPalette::AlternateBase,"surface2");
    bind(QPalette::Text,"foreground"); bind(QPalette::Button,"secondary");
    bind(QPalette::ButtonText,"foreground"); bind(QPalette::Highlight,"primary");
    bind(QPalette::HighlightedText,"primaryFG"); bind(QPalette::ToolTipBase,"popover");
    bind(QPalette::ToolTipText,"foreground"); bind(QPalette::PlaceholderText,"mutedFG");
    bind(QPalette::Link,"primary"); bind(QPalette::LinkVisited,"power");
    bind(QPalette::Light,"input"); bind(QPalette::Midlight,"border");
    bind(QPalette::Mid,"accent"); bind(QPalette::Dark,"surface1");
    bind(QPalette::Shadow,"background"); bind(QPalette::BrightText,"foreground");
    for (auto role : {QPalette::Text,QPalette::WindowText,QPalette::ButtonText})
        p.setColor(QPalette::Disabled,role,token("mutedFG"));
    return p;
}
QString stylesheet() {
    QString css = QStringLiteral(R"QSS(
QWidget { color: @foreground; font-family: "Inter", "Noto Sans", sans-serif; font-size: 12px; }
QMainWindow, QDialog, QStackedWidget, QTabWidget { background: @background; }
QLabel { background: transparent; }
QLabel#muted, QLabel#eyebrow { color: @mutedFG; }
QLabel#sectionTitle { font-size: 15px; font-weight: 600; }
QLabel#eyebrow { font-size: 10px; }
QLabel#brand { color: @primary; font-weight: 600; padding: 5px 12px; }
QWidget#card { background: @card; border: 1px solid @border; border-radius: 4px; }
QWidget#surface1 { background: @surface1; }
QWidget#surface2 { background: @surface2; }
QWidget#surface3 { background: @surface3; }
QLabel[status="ok"] { color: @ok; }
QLabel[status="warn"] { color: @warn; }
QLabel[status="err"] { color: @err; }
QLabel[status="power"] { color: @power; }
QMenuBar, QStatusBar { background: @surface1; border-bottom: 1px solid @border; }
QMenuBar { padding: 3px; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected, QMenu::item:selected { background: @accent; }
QMenu { background: @popover; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 8px 20px; }
QMenu::separator { height: 1px; background: @border; margin: 4px; }
QMenu::item:disabled { color: @mutedFG; }
QPushButton, QToolButton { background: @card; border: 1px solid @border; border-radius: 4px; padding: 10px 12px; }
QPushButton { text-align: left; }
QPushButton:hover, QToolButton:hover { background: @accent; border-color: @input; }
QPushButton:focus, QToolButton:focus { border-color: @primary; }
QPushButton:pressed, QToolButton:pressed { background: @secondary; }
QPushButton:disabled, QToolButton:disabled { color: @mutedFG; background: @muted; }
QPushButton[primary="true"], QPushButton:default { background: @primary; color: @primaryFG; border-color: @primary; }
QTabWidget::pane { border: 0; border-top: 1px solid @border; }
QTabBar::tab { background: @background; color: @mutedFG; padding: 14px 18px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @foreground; border-bottom: 2px solid @primary; }
QTabBar::tab:hover { background: @surface1; }
QTabBar::tab:disabled { color: @mutedFG; }
QSplitter::handle { background: @border; width: 1px; height: 1px; }
QSplitter::handle:hover { background: @primary; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: @card; color: @foreground; border: 1px solid @input;
    border-radius: 4px; padding: 6px 8px; selection-background-color: @primary;
    selection-color: @primaryFG;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @primary; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background: @muted; color: @mutedFG; }
QComboBox { padding-right: 26px; }
QComboBox::drop-down { width: 22px; border: 0; border-left: 1px solid @border; }
QComboBox QAbstractItemView { background: @popover; border: 1px solid @border; selection-background-color: @accent; selection-color: @foreground; padding: 4px; }
QTreeWidget, QTreeView, QTableWidget, QTableView, QListWidget, QListView {
    background: @surface1; alternate-background-color: @surface2; color: @foreground;
    border: 0; gridline-color: @border; selection-background-color: @accent;
    selection-color: @foreground; outline: 0;
}
QTreeWidget { padding: 10px; }
QTreeWidget::item, QTreeView::item { padding: 8px 4px; }
QTableWidget::item, QTableView::item { padding: 6px; }
QTreeWidget::item:selected, QTableWidget::item:selected { background: @accent; color: @foreground; }
QTreeWidget::item:hover, QTableWidget::item:hover { background: @secondary; }
QHeaderView::section { background: @surface2; color: @mutedFG; border: 0; border-bottom: 1px solid @border; border-right: 1px solid @border; padding: 8px; font-weight: 600; }
QTableCornerButton::section { background: @surface2; border: 0; }
QStatusBar { color: @mutedFG; font-size: 10px; border-top: 1px solid @border; }
QStatusBar::item { border: 0; }
QToolTip { background: @popover; color: @foreground; border: 1px solid @border; padding: 6px; }
QGroupBox { border: 1px solid @border; border-radius: 4px; margin-top: 10px; padding-top: 12px; }
QGroupBox::title { color: @mutedFG; subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox:disabled, QRadioButton:disabled { color: @mutedFG; }
QProgressBar { background: @muted; border: 1px solid @border; border-radius: 3px; text-align: center; }
QProgressBar::chunk { background: @primary; }
QScrollArea { border: 0; background: @background; }
QScrollBar:vertical { background: @surface1; width: 8px; margin: 0; }
QScrollBar:horizontal { background: @surface1; height: 8px; margin: 0; }
QScrollBar::handle { background: @input; border-radius: 3px; min-width: 20px; min-height: 20px; }
QScrollBar::handle:hover { background: @mutedFG; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)QSS");
    // Delimit tokens so "primary" cannot consume the prefix of "primaryFG".
    for (const auto &t : tokens)
        css.replace(QStringLiteral("@") + QLatin1String(t.name) + QLatin1Char(';'), token(t.name).name() + QLatin1Char(';'));
    return css;
}
void apply(QApplication &app) {
    app.setStyle("Fusion");
    app.setPalette(palette());
    app.setStyleSheet(stylesheet());
}
}
