#pragma once

// MBD tab: node palette | graph canvas | node inspector, with graph
// open/save/new and Generate (save → graph_to_c.js → output panel).
// Contract: desktop/docs/mbd_graph_contract.md. All graph state lives in the
// GraphModel; persistence goes through GraphStore, codegen through
// GraphCodegenRunner — this view only wires them together.

#include "graph_codegen_runner.h"
#include "graph_model.h"
#include "node_type_registry.h"

#include <QJsonObject>
#include <QWidget>

#include <memory>

class QAction;
class QComboBox;
class QLabel;

namespace Hypr {
class CodegenOutputView;
class GraphCanvas;
class NodeInspector;
class NodePalette;
}

class MbdView : public QWidget {
    Q_OBJECT
public:
    explicit MbdView(QWidget *parent = nullptr);
    ~MbdView() override;

    // Binds the view to a saved project (empty id → no project; editing disabled).
    // Loads the manifest's active graph (or the first listed), like the web editor.
    void setProject(const QString &root, const QString &projectId);
    // Live Hardware Setup state in hardware.json shape (ProjectStore::hardwareJson);
    // feeds the inspector's resource dropdowns and the saved metadata.targetBoard.
    void setHardware(const QJsonObject &hardware);

    // Dialog-free operations (used by the toolbar and by tests). Throw
    // std::runtime_error with the store's message on failure.
    void newGraph(const QString &name);
    void openGraph(const QString &graphId);
    void saveGraph();
    // Saves, then runs graph_to_c.js asynchronously; emits generated().
    void generate();

    Hypr::GraphModel *model() const { return model_; }
    const Hypr::NodeTypeRegistry &registry() const { return registry_; }
    QString graphId() const { return model_->document().id; }
    bool graphPersisted() const { return persisted_; }
    Hypr::GraphCodegenResult lastResult() const { return lastResult_; }

Q_SIGNALS:
    void generated(const Hypr::GraphCodegenResult &result);
    void statusMessage(const QString &message);

private:
    class InspectorSource;

    Hypr::NodeTypeRegistry registry_;
    Hypr::GraphModel *model_;
    Hypr::GraphCanvas *canvas_;
    Hypr::NodePalette *palette_;
    Hypr::NodeInspector *inspector_;
    Hypr::CodegenOutputView *output_;
    Hypr::GraphCodegenRunner *runner_;
    std::unique_ptr<InspectorSource> source_;
    QComboBox *graphSelector_;
    QLabel *graphState_;
    QLabel *notice_;
    QAction *newAction_, *saveAction_, *generateAction_, *undoAction_, *redoAction_, *fitAction_;

    QString root_;
    QString projectId_;
    QJsonObject hardware_;
    bool persisted_ = false;
    Hypr::GraphCodegenResult lastResult_;

    QString targetBoard() const;
    void loadDocument(const Hypr::GraphDocument &doc, bool persisted);
    void refreshGraphList();
    void updateChrome();
    bool confirmDiscard(const QString &action);
    void report(const QString &message);
};
