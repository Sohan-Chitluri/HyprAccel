#pragma once

// Persistence for hypraccel.mbd.graph v1 documents, matching
// mbd/editor/server.js's graph file API byte-for-byte
// (desktop/docs/mbd_graph_contract.md §1). Works purely on GraphDocument; it
// knows nothing about GraphModel, NodeTypeProvider or the canvas.
//
// All failures are reported as std::runtime_error with the same message text
// the server would produce for the analogous condition, per the contract.

#include "graph_document.h"
#include "ordered_json.h"

#include <QList>
#include <QString>

#include <functional>

namespace Hypr {

struct GraphListEntry {
    QString id;
    QString name;      // graphDisplayName(graph.name, id)
    QString filename;  // "<id>.json"
};

class GraphStore {
public:
    // <root>/<projectId>/graphs
    static QString graphsDir(const QString &root, const QString &projectId);

    // server.js listProjectGraphs: only *.json files whose stem matches the
    // graph id grammar, sorted by filename. Runs the legacy migration first.
    // Throws if any listed file fails assertGraphDocument or id/filename
    // agreement.
    static QList<GraphListEntry> listGraphs(const QString &root, const QString &projectId);

    // §1.3 "On open": manifest.activeGraphId if it names a listed graph,
    // else the first listed graph's id. Empty string if the project has no
    // graphs at all. Runs legacy migration first.
    static QString resolveActiveGraphId(const QString &root, const QString &projectId);

    // Loads and validates one graph file (assertGraphDocument, filename/id
    // match, symlink rejection), applying graphNodesFromDocument /
    // graphEdgesFromDocument defaults (missing label/position, missing edge
    // id). Params are kept verbatim, in file order.
    //
    // `displayName` mirrors the web's `displayNames[type] || type`
    // (graph_editor.html:1486) used only for a node whose `label` is
    // missing/empty. GraphStore intentionally does not depend on
    // NodeTypeProvider / node_types.json (Agent B's registry, §7 module
    // ownership) to build that map itself, so the caller supplies it; the
    // default (`{}`) falls back to `type` verbatim, which is only wrong for
    // CordicOp ("CORDIC" vs "CordicOp") and only when a loaded node has no
    // label at all.
    static GraphDocument load(const QString &root, const QString &projectId, const QString &graphId,
                               const std::function<QString(const QString &type)> &displayName = {});

    // toGraph() (graph_editor.html:1467): derives `inputs` and `metadata`
    // from the document; drops any other top-level keys. `targetBoard` is
    // Hardware Setup's board id; pass an empty string to omit it (the "no
    // board known" case).
    static Json::Value toDocument(const GraphDocument &doc, const QString &targetBoard);

    // writeProjectGraph: builds the document via toDocument(), re-trims the
    // name, writes the file (byte-exact, atomic QSaveFile), invalidates
    // generated/build artifacts pointing at this graph, and sets
    // manifest.activeGraphId + updatedAt. `create=true` matches POST
    // semantics (throws if the file already exists); `create=false` matches
    // PUT (overwrite-or-create). Mutates doc.id/doc.name to the values
    // actually written (mirrors the server mutating `graph` in place).
    static void save(const QString &root, const QString &projectId, GraphDocument &doc,
                      const QString &targetBoard, bool create);

    // D2's "Generate" flow: save the graph, then return the exact document
    // JSON just written (same content GraphCodegenRunner::generate() should
    // be called with) so "Generate" always runs on the saved state, never a
    // stale in-memory one. Throws exactly as save() does.
    static Json::Value saveAndPrepareForGenerate(const QString &root, const QString &projectId, GraphDocument &doc,
                                                  const QString &targetBoard, bool create);

    // POST .../activate: graph must already exist.
    static void setActive(const QString &root, const QString &projectId, const QString &graphId);

    // PUT .../rename (server.js:877). Returns the renamed document (id/name
    // already updated).
    static GraphDocument rename(const QString &root, const QString &projectId,
                                 const QString &oldGraphId, const QString &newGraphId,
                                 const QString &newName);

    // DELETE .../:graphId (server.js:909). Throws if it is the last graph.
    static void remove(const QString &root, const QString &projectId, const QString &graphId);

    // Copies graph/graph.json into graphs/<id>.json exactly as
    // migrateLegacyProjectGraph does. Safe to call unconditionally; it is a
    // no-op once manifest.graphs.path == "graphs". Called internally by
    // every other GraphStore entry point, exposed for direct testing.
    static void migrateLegacyGraphIfNeeded(const QString &root, const QString &projectId);

    // graph_editor.html:1046, copied exactly.
    static QString graphIdFromName(const QString &name);

    // §1.3: `graphDisplayName(name, fallback)`. Throws if `name` is present
    // but not a non-empty, <=120-character string once trimmed.
    static QString displayName(const QString &name, const QString &fallback);

    // Grammar shared with project ids: ^[A-Za-z][A-Za-z0-9_-]{0,63}$.
    static bool isValidGraphId(const QString &id);

    // Reproduces `a.localeCompare(b)`'s sign (server.js:435 sorts
    // listProjectGraphs by filename with exactly that). Exposed for direct
    // testing against a node oracle; listGraphs() uses it internally.
    static int compareLikeNodeLocale(const QString &a, const QString &b);
};

} // namespace Hypr
