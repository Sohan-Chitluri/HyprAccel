#pragma once

// Property inspector for the selected graph node (contract §2.2, §5 B, §7
// "Inspector editing"). Reads field definitions from NodeTypeRegistry and
// edits params through an abstract Source so this library never depends on
// Agent A's GraphModel.

#include "graph_document.h"
#include "node_type_registry.h"

#include <QJsonObject>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QVBoxLayout;

namespace Hypr {

class NodeInspector : public QWidget {
    Q_OBJECT
public:
    // Phase-2 bridge point to GraphModel. B builds and tests against this
    // small abstract adapter (contract §7).
    class Source {
    public:
        virtual ~Source();
        virtual const GraphNode *node(const QString &nodeId) const = 0;
        virtual void setParam(const QString &nodeId, const QString &key, const Json::Value &value) = 0;
        virtual void removeParam(const QString &nodeId, const QString &key) = 0;
    };

    explicit NodeInspector(const NodeTypeRegistry *registry, QWidget *parent = nullptr);

    void setSource(Source *source);
    // The project hardware.json shape (ProjectStore::hardwareJson()); never
    // read from disk by this widget (contract §7).
    void setHardware(const QJsonObject &hardware);

    QString currentNodeId() const { return currentNodeId_; }

public Q_SLOTS:
    void showNode(const QString &nodeId);
    void refresh();

private:
    const NodeTypeRegistry *registry_ = nullptr;
    Source *source_ = nullptr;
    QJsonObject hardware_;
    QString currentNodeId_;
    QVBoxLayout *bodyLayout_ = nullptr;

    void rebuild();
    void clearBody();
    void buildGroup(const InspectorGroup &group, const GraphNode &node);
    void buildCustomCodeGroup(const GraphNode &node);
    void buildHardwareGroup(const GraphNode &node);
    void buildAcceleratorGroup(const GraphNode &node);
    void showEmptyState();
};

} // namespace Hypr

Q_DECLARE_METATYPE(Hypr::Json::Value)
