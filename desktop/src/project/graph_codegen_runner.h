#pragma once

// Shells out to the frozen mbd/codegen/graph_to_c.js exactly as
// mbd/editor/server.js's POST /api/build does (desktop/docs/
// mbd_graph_contract.md §4, decision D2). No graph-to-C logic is
// reimplemented here.

#include "ordered_json.h"

#include <QObject>
#include <QString>

namespace Hypr {

struct GraphCodegenResult {
    bool ok = false;
    QString source;             // generated C, only when ok
    QString stdOut;
    QString stdErr;             // trimmed; keeps the "graph_to_c: " prefix, like server.js's err.stderr.trim()
    int exitCode = -1;
    // Set when the run could not happen at all (node missing, script
    // missing, process failed to start/crashed) rather than graph_to_c.js
    // itself reporting a graph error. `ok` is false in both cases; callers
    // that want to distinguish "your graph is invalid" from "codegen is
    // unavailable" check this field.
    QString unavailableReason;
};

// scriptPath: mbd/codegen/graph_to_c.js (GRAPH_TO_C_SCRIPT_PATH compile
// definition).
class GraphCodegenRunner : public QObject {
    Q_OBJECT
public:
    explicit GraphCodegenRunner(QString scriptPath, QObject *parent = nullptr);

    // Writes <tmp>/graph.json (the filename is load-bearing: graph_to_c.js
    // embeds path.basename() of its input in the generated header) and runs
    // `node <scriptPath> <tmp>/graph.json <tmp>/graph.c` asynchronously.
    void generate(const Json::Value &graphDocument);

    // Same invocation, blocking; used by tests and any synchronous caller.
    // Does not emit finished().
    GraphCodegenResult generateSync(const Json::Value &graphDocument, int timeoutMs = 10000);

Q_SIGNALS:
    void finished(const Hypr::GraphCodegenResult &result);

private:
    QString scriptPath_;
};

} // namespace Hypr

Q_DECLARE_METATYPE(Hypr::GraphCodegenResult)
