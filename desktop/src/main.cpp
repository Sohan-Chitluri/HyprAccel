#include <QApplication>
#include "theme.h"
#include "hardware_view.h"
#include "clock_view.h"
#include "project_store.h"
#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

static QLabel *label(const QString &text, const QString &name = {}) {
    auto *widget = new QLabel(text);
    widget->setObjectName(name);
    widget->setWordWrap(true);
    return widget;
}

static QWidget *panel(const QString &title, const QString &message) {
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->addWidget(label(title, "sectionTitle"));
    layout->addWidget(label(message, "muted"));
    layout->addStretch();
    return widget;
}

class StudioWindow : public QMainWindow {
public:
    QStackedWidget *pages;
    QTabWidget *workspaces;
    QPushButton *newProject;
    QPushButton *openProjectButton = nullptr;
    QAction *closeProject;
    QLabel *targetSummary = nullptr;
    HardwareView *hardwareView = nullptr;
    ClockView *clockView = nullptr;

    // Persistence state; no dialogs here so tests (and the smoke test) can
    // drive save/open directly.
    QString currentProjectId;
    QString currentProjectName;
    QString currentProjectRoot;
    bool projectDirty = false;

    StudioWindow() {
        setWindowTitle("HyprAccel — Project Manager");
        resize(1440, 900);
        setMinimumSize(1024, 640);
        setFont(QFont("Inter", 10));
        menuBar()->setCornerWidget(label("H  HyprAccel", "brand"), Qt::TopLeftCorner);
        auto *file = menuBar()->addMenu("File");
        auto *newAction = file->addAction("New Project");
        newAction->setShortcut(QKeySequence::New);
        auto *saveProjectAction = file->addAction("Save Project…");
        saveProjectAction->setShortcut(QKeySequence::Save);
        auto *openProjectAction = file->addAction("Open Project…");
        openProjectAction->setShortcut(QKeySequence::Open);
        closeProject = file->addAction("Close Project");
        closeProject->setShortcut(QKeySequence("Ctrl+W"));
        file->addSeparator();
        auto *quit = file->addAction("Quit");
        quit->setShortcut(QKeySequence::Quit);
        connect(quit, &QAction::triggered, this, &QWidget::close);
        pages = new QStackedWidget;
        setCentralWidget(pages);
        pages->addWidget(makeLauncher());
        pages->addWidget(makeWorkspace());
        connect(newProject, &QPushButton::clicked, this, [this] { openWorkspace(); });
        connect(newAction, &QAction::triggered, this, [this] { openWorkspace(); });
        connect(closeProject, &QAction::triggered, this, [this] {
            pages->setCurrentIndex(0);
            setWindowTitle("HyprAccel — Project Manager");
            statusBar()->showMessage("Ready");
        });

        connect(saveProjectAction, &QAction::triggered, this, [this] {
            if (!hardwareView || hardwareView->boardId().isEmpty()) {
                statusBar()->showMessage("Select a board before saving.");
                return;
            }
            QString id = currentProjectId;
            QString name = currentProjectName;
            if (id.isEmpty()) {
                bool ok = false;
                id = QInputDialog::getText(this, "Save Project", "Project ID (starts with a letter; letters, numbers, - or _):",
                                            QLineEdit::Normal, QString(), &ok).trimmed();
                if (!ok || id.isEmpty()) return;
                name = QInputDialog::getText(this, "Save Project", "Project name:", QLineEdit::Normal, id, &ok).trimmed();
                if (!ok) return;
                if (name.isEmpty()) name = id;
            }
            try {
                saveProjectTo(Hypr::ProjectStore::defaultRoot(), id, name);
                statusBar()->showMessage("Saved project '" + currentProjectId + "'.");
            } catch (const std::exception &error) {
                statusBar()->showMessage(QString("Save failed: ") + error.what());
            }
        });

        auto openProjectDialog = [this] {
            const QString root = Hypr::ProjectStore::defaultRoot();
            const auto ids = Hypr::ProjectStore::list(root);
            if (ids.isEmpty()) {
                statusBar()->showMessage("No saved projects found in " + root);
                return;
            }
            bool ok = false;
            const QString id = QInputDialog::getItem(this, "Open Project", "Project:", ids, 0, false, &ok);
            if (!ok || id.isEmpty()) return;
            pages->setCurrentIndex(1);
            workspaces->setCurrentIndex(0);
            try {
                const auto dropped = openProjectFrom(root, id);
                QString message = "Opened project '" + id + "'.";
                if (!dropped.isEmpty())
                    message += QString(" %1 assignment(s) dropped (invalid for this board).").arg(dropped.size());
                statusBar()->showMessage(message);
            } catch (const std::exception &error) {
                statusBar()->showMessage(QString("Open failed: ") + error.what());
            }
        };
        connect(openProjectAction, &QAction::triggered, this, openProjectDialog);
        if (openProjectButton) {
            openProjectButton->setEnabled(true);
            openProjectButton->setToolTip(QString());
            connect(openProjectButton, &QPushButton::clicked, this, openProjectDialog);
        }

        statusBar()->showMessage("Ready");
        auto *status = label("Qt 6 · Backend not connected", "muted");
        status->setWordWrap(false);
        statusBar()->addPermanentWidget(status);
        updateProjectChrome();
    }

    // --- Dialog-free persistence, so tests (and --smoke-test) can drive it. ---
    void saveProjectTo(const QString &root, const QString &id, const QString &name) {
        Hypr::ProjectSnapshot snapshot;
        snapshot.id = id;
        snapshot.name = name;
        snapshot.board = hardwareView->boardId();
        snapshot.pinAssignments = hardwareView->pinModel()->assignments();
        snapshot.clock = clockView->config();
        snapshot.clockResult = clockView->result();
        Hypr::ProjectStore::save(root, snapshot, QStringLiteral(BOARD_CATALOG_PATH));
        currentProjectId = id;
        currentProjectName = name;
        currentProjectRoot = root;
        projectDirty = false;
        updateProjectChrome();
    }

    // Returns the assignments dropped because they were invalid for the board.
    QMap<QString, QString> openProjectFrom(const QString &root, const QString &id) {
        const auto snapshot = Hypr::ProjectStore::load(root, id);
        hardwareView->selectBoard(snapshot.board);
        const auto dropped = hardwareView->pinModel()->setAssignments(snapshot.pinAssignments);
        clockView->setBoard(snapshot.board);
        clockView->setConfig(snapshot.clock);
        currentProjectId = id;
        currentProjectName = snapshot.name;
        currentProjectRoot = root;
        projectDirty = false;
        updateProjectChrome();
        return dropped;
    }

    void updateProjectChrome() {
        const QString marker = projectDirty ? QStringLiteral(" •") : QString();
        if (currentProjectId.isEmpty())
            setWindowTitle("HyprAccel — Untitled workspace" + marker);
        else
            setWindowTitle("HyprAccel — " + currentProjectName + marker);
        if (!targetSummary) return;
        const QString target = hardwareView ? hardwareView->boardId() : QString();
        const QString storage = currentProjectId.isEmpty()
            ? QStringLiteral("unsaved")
            : currentProjectRoot + "/" + currentProjectId;
        const QString heading = currentProjectId.isEmpty() ? QStringLiteral("Untitled workspace") : currentProjectName;
        targetSummary->setText(QString("%1%2\n\nTarget: %3\nStorage: %4\n\nSave writes project.json, hardware/hardware.json and hardware/clock.json; graphs are never touched.")
            .arg(heading, marker, target.isEmpty() ? QStringLiteral("not selected") : target, storage));
    }

private:
    QWidget *makeLauncher() {
        auto *launcher = new QWidget;
        auto *outer = new QVBoxLayout(launcher);
        outer->addStretch(2);
        auto *content = new QWidget;
        content->setMaximumWidth(820);
        auto *layout = new QVBoxLayout(content);
        layout->setSpacing(14);
        layout->addWidget(label("Start a session", "sectionTitle"));
        layout->addWidget(label("Configure MCU peripherals, clocks and firmware for embedded targets.", "muted"));
        auto *grid = new QGridLayout;
        grid->setSpacing(0);
        newProject = new QPushButton("CREATE\n\nNew Project\nStart from an MCU or board selection");
        newProject->setMinimumSize(380, 94);
        grid->addWidget(newProject, 0, 0);
        auto addDisabled = [&](const QString &text, int row, int column) {
            auto *button = new QPushButton(text);
            button->setMinimumSize(380, 94);
            button->setEnabled(false);
            button->setToolTip("Not connected in this first UI slice");
            grid->addWidget(button, row, column);
            return button;
        };
        openProjectButton = addDisabled("OPEN\n\nOpen Project\nOpen an existing .hypr workspace", 0, 1);
        addDisabled("OPEN\n\nOpen Recent\nReopen the last used workspace", 1, 0);
        addDisabled("IMPORT\n\nImport Schematic\nKiCad / netlist → pin mapping", 1, 1);
        layout->addLayout(grid);
        layout->addSpacing(12);
        layout->addWidget(label("RECENT PROJECTS", "eyebrow"));
        layout->addWidget(label("No local projects loaded. New Project opens an unsaved UI workspace.", "muted"));
        outer->addWidget(content, 0, Qt::AlignHCenter);
        outer->addStretch(3);
        return launcher;
    }

    QWidget *makeWorkspace() {
        workspaces = new QTabWidget;
        workspaces->setObjectName("workspaces");
        hardwareView = new HardwareView(QStringLiteral(BOARD_CATALOG_PATH));
        hardwareView->setObjectName("hardwareView");
        workspaces->addTab(hardwareView, "Pinout && Configuration");
        clockView = new ClockView(QStringLiteral(BOARD_CATALOG_PATH));
        clockView->setObjectName("clockView");
        workspaces->addTab(clockView, "Clock Configuration");
        workspaces->addTab(panel("Project", "Untitled workspace\n\nTarget: not selected\nStorage: unsaved\n\nSave writes project.json, hardware/hardware.json and hardware/clock.json; graphs are never touched."), "Project");
        targetSummary = workspaces->widget(2)->findChild<QLabel*>("muted");
        targetSummary->setTextFormat(Qt::PlainText);
        auto *boardSelector = hardwareView->findChild<QComboBox*>("boardSelector");
        connect(boardSelector, &QComboBox::currentIndexChanged, this, [this, boardSelector] {
            const QString id = boardSelector->currentData().toString();
            clockView->setBoard(id);
            updateProjectChrome();
        });
        connect(hardwareView->pinModel(), &Hypr::PinAssignmentModel::assignmentChanged, this,
                [this](const QString &, const QString &) { projectDirty = true; updateProjectChrome(); });
        connect(clockView, &ClockView::configChanged, this, [this] { projectDirty = true; updateProjectChrome(); });
        workspaces->addTab(panel("Firmware", "Generation, compile, flash and verification will use the existing HyprAccel backend.\n\nNo firmware job has been started."), "Firmware");
        auto *mbd = new QSplitter;
        mbd->addWidget(panel("BLOCK LIBRARY", "The existing node schema will supply supported blocks."));
        mbd->addWidget(panel("Model-Based Development", "No model loaded\n\nThe native block canvas is the next implementation slice, not a simulated execution view."));
        mbd->addWidget(panel("BLOCK PARAMETERS", "Select a block to edit its parameters."));
        mbd->setSizes({240, 880, 320});
        workspaces->addTab(mbd, "MBD");
        return workspaces;
    }

    void openWorkspace() {
        if (auto *selector = findChild<QComboBox*>("boardSelector")) selector->setCurrentIndex(0);
        currentProjectId.clear();
        currentProjectName.clear();
        currentProjectRoot.clear();
        projectDirty = false;
        pages->setCurrentIndex(1);
        workspaces->setCurrentIndex(0);
        statusBar()->showMessage("Unsaved workspace · Choose a board in Pinout & Configuration");
        updateProjectChrome();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("HyprAccel Dev Studio");
    StudioTheme::apply(app);
    StudioWindow window;
    window.show();
    if (app.arguments().contains("--theme-test")) {
        QTimer::singleShot(100, &app, [&] {
            // Independent reference conversion of Lovable oklch(.17 .008 250).
            const QColor expected(12, 16, 19);
            const QColor actual = app.palette().color(QPalette::Window);
            bool ok = qAbs(actual.red()-expected.red()) <= 1 && qAbs(actual.green()-expected.green()) <= 1 && qAbs(actual.blue()-expected.blue()) <= 1;
            QTextStream(stdout) << "launcher-palette: " << (ok ? "PASS" : "FAIL") << " actual=" << actual.name() << Qt::endl;
            app.exit(ok ? 0 : 1);
        });
    }
    if (app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(100, &app, [&] {
            QTextStream out(stdout);
            bool passed = window.isVisible() && window.pages->currentIndex() == 0;
            window.newProject->click();
            bool opened = window.pages->currentIndex() == 1;
            auto *boardSelector = window.findChild<QComboBox*>("boardSelector");
            bool boardReady = boardSelector && boardSelector->findData("esp32") >= 0;
            if (boardReady) boardSelector->setCurrentIndex(boardSelector->findData("esp32"));
            out << "board-selection: " << (boardReady ? "PASS" : "FAIL") << Qt::endl;
            passed = passed && boardReady;
            bool summaryReady = window.targetSummary && window.targetSummary->text().contains("esp32");
            out << "project-target-summary: " << (summaryReady ? "PASS" : "FAIL") << Qt::endl;
            passed = passed && summaryReady;
            out << "launcher-to-workspace: " << (opened ? "PASS" : "FAIL") << Qt::endl;
            bool navigated = window.workspaces->count() == 5;
            for (int i = 0; i < 5; ++i) {
                window.workspaces->setCurrentIndex(i);
                navigated = navigated && window.workspaces->currentIndex() == i && window.workspaces->currentWidget()->isVisible();
            }
            out << "workspace-navigation: " << (navigated ? "PASS" : "FAIL") << Qt::endl;

            // Persistence round trip: assign a pin, nudge a clock combo if any
            // exists, save to a scratch root, reset in-memory state, reopen,
            // and check pins + clock config come back unchanged.
            bool roundtrip = window.hardwareView && window.clockView;
            QMap<QString, QString> beforePins;
            Hypr::ClockConfig beforeClock;
            if (roundtrip) {
                window.workspaces->setCurrentIndex(0);
                window.hardwareView->pinModel()->assign("GPIO1", "uart.uart0.tx");
                const auto combos = window.clockView->findChildren<QComboBox*>();
                for (auto *combo : combos) {
                    if (combo->objectName().startsWith("clock:") && combo->count() > 1) {
                        combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
                        break;
                    }
                }
                beforePins = window.hardwareView->pinModel()->assignments();
                beforeClock = window.clockView->config();
            }
            QTemporaryDir scratchRoot;
            roundtrip = roundtrip && scratchRoot.isValid();
            if (roundtrip) {
                try {
                    window.saveProjectTo(scratchRoot.path(), "smoketest", "Smoke Test Project");
                } catch (const std::exception &error) {
                    roundtrip = false;
                    out << "persistence-save-error: " << error.what() << Qt::endl;
                }
            }
            if (roundtrip) {
                // Reset the workspace's in-memory state before reopening.
                window.hardwareView->pinModel()->setAssignments({});
                window.clockView->setBoard(window.hardwareView->boardId());
            }
            if (roundtrip) {
                try {
                    window.openProjectFrom(scratchRoot.path(), "smoketest");
                } catch (const std::exception &error) {
                    roundtrip = false;
                    out << "persistence-open-error: " << error.what() << Qt::endl;
                }
            }
            const bool pinsMatch = roundtrip && window.hardwareView->pinModel()->assignments() == beforePins;
            const bool clockMatch = roundtrip && window.clockView->config() == beforeClock;
            roundtrip = roundtrip && pinsMatch && clockMatch;
            out << "persistence-roundtrip: " << (roundtrip ? "PASS" : "FAIL") << Qt::endl;
            passed = passed && roundtrip;

            if (app.arguments().contains("--screenshots")) {
                window.workspaces->setCurrentIndex(0);
                window.grab().save("desktop/build/workspace.png");
            }
            window.closeProject->trigger();
            bool closed = window.pages->currentIndex() == 0;
            window.newProject->click();
            bool fresh = boardSelector && boardSelector->currentData().toString().isEmpty();
            out << "new-workspace-reset: " << (fresh ? "PASS" : "FAIL") << Qt::endl;
            passed = passed && fresh;
            window.closeProject->trigger();
            out << "close-project: " << (closed ? "PASS" : "FAIL") << Qt::endl;
            if (app.arguments().contains("--screenshots")) window.grab().save("desktop/build/launcher.png");
            app.exit(passed && opened && navigated && closed ? 0 : 1);
        });
    }
    return app.exec();
}
