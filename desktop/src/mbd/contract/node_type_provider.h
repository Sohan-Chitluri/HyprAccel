#pragma once

// CONTRACT (integrator-owned). The seam between the canvas (desktop/src/mbd,
// Agent A) and the data-driven node registry (desktop/src/mbd/nodes, Agent B).
// The canvas never hardcodes node types; it asks this interface.

#include "graph_document.h"

#include <QList>
#include <QString>

namespace Hypr {

struct PortSpec {
    QString name;
    QString typeLabel;     // display label from the web portTypes ("number", "any", "number (optional)", ...)
};

enum class ConnectionVerdict {
    Ok,          // allowed, no remark
    Warn,        // allowed; typeLabel mismatch (not "-> any"), see contract D5
    Block,       // refused; see `reason`
};

struct ConnectionCheck {
    ConnectionVerdict verdict = ConnectionVerdict::Ok;
    QString reason;        // user-facing; empty for Ok
};

class NodeTypeProvider {
public:
    virtual ~NodeTypeProvider() = default;

    virtual QStringList types() const = 0;                           // web `defaults` key order
    virtual bool hasType(const QString &type) const = 0;
    virtual QString displayName(const QString &type) const = 0;      // displayNames[type] || type
    virtual Json::Object defaultParams(const QString &type) const = 0;
    // Ports as a function of params (contract §2.1: CordicOp operation, CustomCode inputs).
    virtual QList<PortSpec> inputs(const QString &type, const Json::Object &params) const = 0;
    virtual QList<PortSpec> outputs(const QString &type, const Json::Object &params) const = 0;
    // Contract D5. `doc` is the current graph (for duplicate-input / cycle checks).
    virtual ConnectionCheck checkConnection(const GraphDocument &doc,
                                            const QString &fromNode, const QString &fromPort,
                                            const QString &toNode, const QString &toPort) const = 0;
};

} // namespace Hypr
