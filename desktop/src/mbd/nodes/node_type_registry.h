#pragma once

// Data-driven Hypr::NodeTypeProvider implementation. Loads
// desktop/src/mbd/nodes/node_types.json (embedded as a Qt resource) and
// answers every query the canvas, palette and inspector need. See
// desktop/docs/mbd_graph_contract.md §2, §6 D1, §7.

#include "node_type_provider.h"

#include <QByteArray>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

namespace Hypr {

// One option in a `select` inspector field. `value` keeps its original JSON
// type (contract D4: dataBits/stopBits stay numbers).
struct SelectOption {
    Json::Value value;
    QString label;
};

struct InspectorField {
    QString key;
    QString kind; // "text" | "number" | "check" | "select"
    QList<SelectOption> options; // only for kind == "select"
};

struct InspectorGroup {
    QString name;
    QList<InspectorField> fields;
};

struct PaletteCategory {
    QString name;
    QStringList items;
    QString placeholder; // non-empty only for empty categories ("Future expansion")
};

class NodeTypeRegistry : public NodeTypeProvider {
public:
    NodeTypeRegistry() = default;

    // Loads node_types.json from a Qt resource path
    // (default ":/mbd/nodes/node_types.json"). Throws std::runtime_error on
    // any I/O or shape problem.
    static NodeTypeRegistry loadFromResource(const QString &resourcePath = QStringLiteral(":/mbd/nodes/node_types.json"));
    // Loads from an already-read JSON byte buffer (tests, and the resource
    // loader itself). Throws std::runtime_error on any shape problem.
    static NodeTypeRegistry loadFromJson(const QByteArray &jsonBytes);

    // --- NodeTypeProvider ---
    QStringList types() const override;
    bool hasType(const QString &type) const override;
    QString displayName(const QString &type) const override;
    Json::Object defaultParams(const QString &type) const override;
    QList<PortSpec> inputs(const QString &type, const Json::Object &params) const override;
    QList<PortSpec> outputs(const QString &type, const Json::Object &params) const override;
    ConnectionCheck checkConnection(const GraphDocument &doc,
                                     const QString &fromNode, const QString &fromPort,
                                     const QString &toNode, const QString &toPort) const override;

    // --- Extra accessors for the palette / inspector ---
    QString description(const QString &type) const;
    QString category(const QString &type) const;
    QList<InspectorGroup> inspectorGroups(const QString &type) const;
    QStringList hardwareResourceTypes(const QString &type) const;
    bool isCustomCode(const QString &type) const;
    bool isAccelerator(const QString &type) const;
    QList<PaletteCategory> categories() const { return categories_; }
    QStringList codegenSupportedTypes() const { return codegenSupportedTypes_; }

private:
    struct CodegenSourceSpec {
        bool isCordic = false;
        QStringList flatPorts;                      // non-CordicOp types
        QHash<QString, QStringList> byOperation;     // CordicOp: operation -> output ports (atan2 absent)
    };

    struct TypeInfo {
        QString key;
        QString displayName;
        QString description;
        QString category;
        Json::Object defaults;
        bool dynamicPorts = false;
        QList<PortSpec> staticInputs;
        QList<PortSpec> staticOutputs;
        QList<InspectorGroup> inspectorGroups;
        QStringList hardwareResourceTypes;
        bool isCustomCode = false;
        bool isAccelerator = false;
    };

    QList<TypeInfo> typeList_;
    QHash<QString, int> typeIndex_; // key -> index into typeList_
    QList<PaletteCategory> categories_;
    QStringList codegenSupportedTypes_;
    QHash<QString, CodegenSourceSpec> codegenSources_;

    const TypeInfo *findType(const QString &type) const;
    // Mirrors the web's `ports(type, params)` (contract §2.1).
    QList<PortSpec> dynamicOutputs(const TypeInfo &info, const Json::Object &params) const;
    QList<PortSpec> dynamicInputs(const TypeInfo &info, const Json::Object &params) const;
    // Codegen-acceptable source ports for a given (from) node's type/params.
    // For CordicOp this is operation-dependent; atan2 yields an empty list
    // with `atan2Blocked` set so the caller can give the exact contract
    // wording ("CORDIC atan2 is not supported by codegen").
    QStringList codegenSourcePorts(const QString &type, const Json::Object &params, bool *atan2Blocked) const;
};

} // namespace Hypr
