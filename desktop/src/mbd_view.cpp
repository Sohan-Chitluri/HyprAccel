#include "mbd_view.h"

#include "codegen_output_view.h"
#include "graph_canvas.h"
#include "graph_store.h"
#include "node_inspector.h"
#include "node_palette.h"

#include <QAction>
#include <QComboBox>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>

#include <stdexcept>

using namespace Hypr;

// Bridges NodeInspector's edit adapter to the single GraphModel.
class MbdView::InspectorSource : public NodeInspector::Source {
public:
    explicit InspectorSource(GraphModel *model) : model_(model) {}
    const GraphNode *node(const QString &nodeId) const override { return model_->node(nodeId); }
    void setParam(const QString &nodeId, const QString &key, const Json::Value &value) override
    {
        model_->setParam(nodeId, key, value);
    }
    void removeParam(const QString &nodeId, const QString &key) override { model_->removeParam(nodeId, key); }

private:
    GraphModel *model_;
};

MbdView::MbdView(QWidget *parent)
    : QWidget(parent)
    , registry_(NodeTypeRegistry::loadFromResource())
    , model_(new GraphModel(this))
    , runner_(new GraphCodegenRunner(QStringLiteral(GRAPH_TO_C_SCRIPT_PATH), this))
    , source_(std::make_unique<InspectorSource>(model_))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QToolBar;
    toolbar->setObjectName("mbdToolbar");
    graphSelector_ = new QComboBox;
    graphSelector_->setObjectName("mbdGraphSelector");
    graphSelector_->setMinimumWidth(220);
    toolbar->addWidget(new QLabel(QStringLiteral(" Graph ")));
    toolbar->addWidget(graphSelector_);
    newAction_ = toolbar->addAction(QStringLiteral("New Graph"));
    saveAction_ = toolbar->addAction(QStringLiteral("Save Graph"));
    saveAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    toolbar->addSeparator();
    undoAction_ = toolbar->addAction(QStringLiteral("Undo"));
    redoAction_ = toolbar->addAction(QStringLiteral("Redo"));
    fitAction_ = toolbar->addAction(QStringLiteral("Fit"));
    toolbar->addSeparator();
    generateAction_ = toolbar->addAction(QStringLiteral("Generate C"));
    generateAction_->setObjectName("mbdGenerate");
    graphState_ = new QLabel;
    graphState_->setObjectName("mbdGraphState");
    toolbar->addWidget(graphState_);
    layout->addWidget(toolbar);

    notice_ = new QLabel;
    notice_->setObjectName("mbdNotice");
    notice_->setWordWrap(true);
    notice_->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(notice_);

    palette_ = new NodePalette(&registry_);
    palette_->setObjectName("mbdPalette");
    canvas_ = new GraphCanvas(model_, &registry_);
    canvas_->setObjectName("mbdCanvas");
    inspector_ = new NodeInspector(&registry_);
    inspector_->setObjectName("mbdInspector");
    inspector_->setSource(source_.get());
    output_ = new CodegenOutputView;
    output_->setObjectName("mbdOutput");

    auto *horizontal = new QSplitter(Qt::Horizontal);
    horizontal->addWidget(palette_);
    horizontal->addWidget(canvas_);
    horizontal->addWidget(inspector_);
    horizontal->setStretchFactor(1, 1);
    horizontal->setSizes({240, 880, 320});
    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(horizontal);
    vertical->addWidget(output_);
    vertical->setStretchFactor(0, 1);
    vertical->setSizes({600, 200});
    layout->addWidget(vertical, 1);

    connect(model_, &GraphModel::selectionChanged, this, [this] { inspector_->showNode(model_->selectedNodeId()); });
    // Undo/redo restore whole snapshots; re-render the inspector from the model.
    connect(model_, &GraphModel::documentReset, this, [this] { inspector_->showNode(model_->selectedNodeId()); });
    connect(model_, &GraphModel::dirtyChanged, this, [this] { updateChrome(); });
    for (auto signal : {&GraphModel::nodeAdded, &GraphModel::nodeRemoved, &GraphModel::edgeAdded,
                        &GraphModel::edgeRemoved, &GraphModel::nodeChanged})
        connect(model_, signal, this, [this](const QString &) { updateChrome(); });
    connect(canvas_, &GraphCanvas::connectionRejected, this,
            [this](const QString &reason) { report(QStringLiteral("Connection refused: ") + reason); });
    connect(palette_, &NodePalette::nodeActivated, this, [this](const QString &type) {
        if (projectId_.isEmpty() || !registry_.hasType(type)) return;
        const QPointF centre = canvas_->mapToScene(canvas_->viewport()->rect().center());
        model_->addNode(type, registry_.defaultParams(type), centre, registry_.displayName(type));
    });

    connect(graphSelector_, &QComboBox::activated, this, [this](int index) {
        const QString id = graphSelector_->itemData(index).toString();
        if (id.isEmpty() || id == model_->document().id) return;
        if (!confirmDiscard(QStringLiteral("switching graphs"))) { refreshGraphList(); return; }
        try { openGraph(id); } catch (const std::exception &e) { report(QString::fromUtf8(e.what())); refreshGraphList(); }
    });
    connect(newAction_, &QAction::triggered, this, [this] {
        if (!confirmDiscard(QStringLiteral("creating a new graph"))) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New Graph"), QStringLiteral("Graph name:"),
                                                   QLineEdit::Normal, QStringLiteral("untitled_graph"), &ok);
        if (!ok) return;
        try { newGraph(name); } catch (const std::exception &e) { report(QString::fromUtf8(e.what())); }
    });
    connect(saveAction_, &QAction::triggered, this, [this] {
        try { saveGraph(); report(QStringLiteral("Saved graph '%1'.").arg(graphId())); }
        catch (const std::exception &e) { report(QStringLiteral("Save failed: ") + QString::fromUtf8(e.what())); }
    });
    connect(generateAction_, &QAction::triggered, this, [this] {
        try { generate(); } catch (const std::exception &e) { report(QStringLiteral("Generation failed: ") + QString::fromUtf8(e.what())); }
    });
    connect(undoAction_, &QAction::triggered, model_, &GraphModel::undo);
    connect(redoAction_, &QAction::triggered, model_, &GraphModel::redo);
    connect(fitAction_, &QAction::triggered, canvas_, &GraphCanvas::fitToView);
    connect(runner_, &GraphCodegenRunner::finished, this, [this](const GraphCodegenResult &result) {
        lastResult_ = result;
        output_->showResult(result);
        generateAction_->setEnabled(!projectId_.isEmpty());
        report(result.ok ? QStringLiteral("Generated C for '%1'.").arg(graphId())
                         : QStringLiteral("Generation failed for '%1'.").arg(graphId()));
        Q_EMIT generated(result);
    });

    setProject(QString(), QString());
}

MbdView::~MbdView()
{
    // The inspector holds a raw pointer to source_; detach before either dies.
    inspector_->setSource(nullptr);
}

QString MbdView::targetBoard() const
{
    return hardware_.value(QStringLiteral("board")).toString();
}

void MbdView::setHardware(const QJsonObject &hardware)
{
    hardware_ = hardware;
    inspector_->setHardware(hardware);
}

void MbdView::setProject(const QString &root, const QString &projectId)
{
    // Saving the project (pins/clock) rebinds to the same project; keep the
    // open graph and its unsaved edits rather than reloading from disk.
    if (!projectId.isEmpty() && root == root_ && projectId == projectId_) return;
    root_ = root;
    projectId_ = projectId;
    persisted_ = false;
    model_->reset(GraphDocument());
    output_->clear();
    if (!projectId_.isEmpty()) {
        try {
            GraphStore::migrateLegacyGraphIfNeeded(root_, projectId_);
            const auto graphs = GraphStore::listGraphs(root_, projectId_);
            if (!graphs.isEmpty()) openGraph(GraphStore::resolveActiveGraphId(root_, projectId_));
        } catch (const std::exception &e) {
            report(QStringLiteral("Could not load project graphs: ") + QString::fromUtf8(e.what()));
        }
    }
    refreshGraphList();
    updateChrome();
}

void MbdView::loadDocument(const GraphDocument &doc, bool persisted)
{
    persisted_ = persisted;
    output_->clear();   // previous graph's C must not look like this one's
    model_->reset(doc);
    canvas_->fitToView();
    refreshGraphList();
    updateChrome();
}

void MbdView::openGraph(const QString &graphId)
{
    if (projectId_.isEmpty()) throw std::runtime_error("Open or save a project before opening graphs.");
    const auto displayName = [this](const QString &type) {
        return registry_.hasType(type) ? registry_.displayName(type) : type;
    };
    GraphDocument doc = GraphStore::load(root_, projectId_, graphId, displayName);
    GraphStore::setActive(root_, projectId_, graphId);   // the web's /activate on load
    loadDocument(doc, true);
}

void MbdView::newGraph(const QString &name)
{
    if (projectId_.isEmpty()) throw std::runtime_error("Open or save a project before creating a graph.");
    const QString id = GraphStore::graphIdFromName(name);
    if (!GraphStore::isValidGraphId(id))
        throw std::runtime_error("Enter a graph name that produces a valid graph filename.");
    for (const auto &entry : GraphStore::listGraphs(root_, projectId_))
        if (entry.id == id) throw std::runtime_error(QStringLiteral("Graph '%1' already exists.").arg(id).toStdString());
    GraphDocument doc;
    doc.id = id;
    doc.name = name.trimmed().isEmpty() ? id : name.trimmed();
    loadDocument(doc, false);
    model_->markSaved(doc.id, doc.name);   // clean state for an empty graph
    updateChrome();
}

void MbdView::saveGraph()
{
    if (projectId_.isEmpty()) throw std::runtime_error("Save failed: open or create a project first.");
    if (model_->document().id.isEmpty()) throw std::runtime_error("Choose a graph name before saving.");
    GraphDocument doc = model_->document();
    GraphStore::save(root_, projectId_, doc, targetBoard(), !persisted_);
    persisted_ = true;
    model_->markSaved(doc.id, doc.name);
    refreshGraphList();
    updateChrome();
}

void MbdView::generate()
{
    if (projectId_.isEmpty()) throw std::runtime_error("Open or save a project before generating.");
    if (model_->document().id.isEmpty()) throw std::runtime_error("Create or open a graph before generating.");
    // D2: exactly the web's /api/build step — save first, then codegen the
    // saved document. Nothing is written into generated/.
    GraphDocument doc = model_->document();
    const Json::Value saved = GraphStore::saveAndPrepareForGenerate(root_, projectId_, doc, targetBoard(), !persisted_);
    persisted_ = true;
    model_->markSaved(doc.id, doc.name);
    refreshGraphList();
    updateChrome();
    output_->showRunning();
    generateAction_->setEnabled(false);
    runner_->generate(saved);
}

void MbdView::refreshGraphList()
{
    const QSignalBlocker blocker(graphSelector_);
    graphSelector_->clear();
    if (projectId_.isEmpty()) return;
    QList<GraphListEntry> graphs;
    try { graphs = GraphStore::listGraphs(root_, projectId_); } catch (const std::exception &) {}
    const QString current = model_->document().id;
    bool listed = false;
    for (const auto &entry : graphs) {
        graphSelector_->addItem(entry.name == entry.id ? entry.id : entry.name + " (" + entry.id + ")", entry.id);
        listed = listed || entry.id == current;
    }
    if (!current.isEmpty() && !listed)
        graphSelector_->addItem(model_->document().name + QStringLiteral(" (unsaved)"), current);
    graphSelector_->setCurrentIndex(graphSelector_->findData(current));
}

void MbdView::updateChrome()
{
    const bool hasProject = !projectId_.isEmpty();
    const bool hasGraph = hasProject && !model_->document().id.isEmpty();
    palette_->setEnabled(hasGraph);
    canvas_->setEnabled(hasGraph);
    inspector_->setEnabled(hasGraph);
    graphSelector_->setEnabled(hasProject);
    newAction_->setEnabled(hasProject);
    saveAction_->setEnabled(hasGraph);
    generateAction_->setEnabled(hasGraph);
    undoAction_->setEnabled(hasGraph && model_->canUndo());
    redoAction_->setEnabled(hasGraph && model_->canRedo());
    fitAction_->setEnabled(hasGraph);
    if (!hasProject)
        notice_->setText(QStringLiteral("Save or open a project to edit its MBD graphs (stored in <project>/graphs/, shared with the web editor)."));
    else if (!hasGraph)
        notice_->setText(QStringLiteral("This project has no graphs yet. Use New Graph to create one."));
    else
        notice_->clear();
    notice_->setVisible(!notice_->text().isEmpty());
    if (!hasGraph) graphState_->clear();
    else if (!persisted_) graphState_->setText(QStringLiteral("  Unsaved new graph"));
    else graphState_->setText(model_->isDirty() ? QStringLiteral("  Unsaved changes") : QStringLiteral("  Saved"));
}

bool MbdView::confirmDiscard(const QString &action)
{
    if (!model_->isDirty() && persisted_) return true;
    if (model_->document().id.isEmpty() || (!persisted_ && model_->document().nodes.isEmpty())) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Unsaved graph"),
        QStringLiteral("Graph '%1' has unsaved changes. Save before %2?").arg(model_->document().name, action),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) {
        try { saveGraph(); } catch (const std::exception &e) { report(QStringLiteral("Save failed: ") + QString::fromUtf8(e.what())); return false; }
    }
    return true;
}

void MbdView::report(const QString &message)
{
    Q_EMIT statusMessage(message);
}
