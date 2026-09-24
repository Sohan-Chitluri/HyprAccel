#include "graph_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <stdexcept>

namespace Hypr {

using Json::Value;

namespace {

QString projectDir(const QString &root, const QString &projectId)
{
    return QDir(root).filePath(projectId);
}

QString manifestPath(const QString &root, const QString &projectId)
{
    return QDir(projectDir(root, projectId)).filePath(QStringLiteral("project.json"));
}

QString legacyGraphPath(const QString &root, const QString &projectId)
{
    return QDir(projectDir(root, projectId)).filePath(QStringLiteral("graph/graph.json"));
}

QString generatedDir(const QString &root, const QString &projectId)
{
    return QDir(projectDir(root, projectId)).filePath(QStringLiteral("generated"));
}

QString buildDir(const QString &root, const QString &projectId)
{
    return QDir(projectDir(root, projectId)).filePath(QStringLiteral("build"));
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")) + QLatin1Char('Z');
}

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

QByteArray readFileBytes(const QString &path, const QString &label)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Could not read %1: %2").arg(label, file.errorString()));
    return file.readAll();
}

// writeJson(): JSON.stringify(value, null, 2) + "\n", written atomically.
void writeJsonFile(const QString &path, const Value &value)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
    QByteArray bytes = Json::stringify(value);
    bytes += '\n';
    file.write(bytes);
    if (!file.commit())
        fail(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
}

// Returns Value::null() (Kind::Null) if the file does not exist, matching
// server.js readJson()'s ENOENT -> null behaviour. Throws on parse failure.
Value readJsonFileOrNull(const QString &path, const QString &label, bool *existed = nullptr)
{
    QFileInfo info(path);
    if (existed) *existed = info.exists();
    if (!info.exists()) return Value::null();
    QByteArray bytes = readFileBytes(path, label);
    QString error;
    Value v = Json::parse(bytes, &error);
    if (!error.isEmpty()) fail(QStringLiteral("Could not read %1: %2").arg(label, error));
    return v;
}

// Reproduces Node's `String.prototype.localeCompare` (ICU root collation)
// for the restricted alphabet server.js's filenames are built from:
// GRAPH_ID (^[A-Za-z][A-Za-z0-9_-]{0,63}$) plus a literal ".json" suffix.
// listProjectGraphs() sorts by filename with exactly that comparator
// (server.js:435), and QString::operator</localeAwareCompare do not match
// it: ICU root collation is primary-level case-insensitive (so letters
// case-fold together, ordered after '_','-','.' and digits) and only case
// as a level-3 tiebreak when the whole strings are otherwise identical.
// Verified against `node -e` for the demo project's filenames plus a mixed
// case/underscore/hyphen/digit corpus (see tests/graph_store_test.cpp
// localeCompareMatchesNode()); do not "simplify" this without re-checking
// against node, the ordering is not obvious from ASCII values alone (e.g.
// lowercase sorts before uppercase, the reverse of ASCII).
int rankOfChar(QChar c)
{
    const ushort u = c.unicode();
    if (u == '_') return 0;
    if (u == '-') return 1;
    if (u == '.') return 2;
    if (u >= '0' && u <= '9') return 3 + (u - '0');
    if (u >= 'a' && u <= 'z') return 13 + (u - 'a');
    if (u >= 'A' && u <= 'Z') return 13 + (u - 'A');
    return 1000 + u; // outside the expected alphabet; keep deterministic
}

int localeCompareLikeNode(const QString &a, const QString &b)
{
    const int len = std::min(a.size(), b.size());
    for (int i = 0; i < len; ++i) {
        const int ra = rankOfChar(a.at(i));
        const int rb = rankOfChar(b.at(i));
        if (ra != rb) return ra < rb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i) != b.at(i))
            return a.at(i).isLower() ? -1 : 1;
    }
    return 0;
}

void removeDirRecursively(const QString &path)
{
    QDir dir(path);
    if (dir.exists()) dir.removeRecursively();
}

Value readManifestRaw(const QString &root, const QString &projectId)
{
    bool existed = false;
    Value v = readJsonFileOrNull(manifestPath(root, projectId), QStringLiteral("project manifest"), &existed);
    if (!existed) fail(QStringLiteral("Project '%1' was not found.").arg(projectId));
    if (!v.isObject()) fail(QStringLiteral("Project '%1' has an invalid manifest.").arg(projectId));
    const Value *id = Json::find(v.object, QStringLiteral("id"));
    const Value *format = Json::find(v.object, QStringLiteral("format"));
    const Value *version = Json::find(v.object, QStringLiteral("version"));
    const bool idOk = id && id->isString() && id->string == projectId;
    const bool formatOk = format && format->isString() && format->string == QStringLiteral("hypraccel.project");
    const bool versionOk = version && version->isNumber() && version->number == 1.0;
    if (!idOk || !formatOk || !versionOk)
        fail(QStringLiteral("Project '%1' has an invalid manifest.").arg(projectId));
    return v;
}

// migrateLegacyProjectGraph, run unconditionally at the top of every
// server.js graph operation via readProjectManifest().
Value ensureMigrated(const QString &root, const QString &projectId)
{
    Value manifest = readManifestRaw(root, projectId);
    const Value *graphs = Json::find(manifest.object, QStringLiteral("graphs"));
    if (graphs && graphs->isObject()) {
        const Value *path = Json::find(graphs->object, QStringLiteral("path"));
        if (path && path->isString() && path->string == QStringLiteral("graphs"))
            return manifest; // already migrated
    }

    const QString legacyPath = legacyGraphPath(root, projectId);
    if (QFileInfo::exists(legacyPath)) {
        QString err;
        Value legacy = Json::parse(readFileBytes(legacyPath, QStringLiteral("legacy project graph")), &err);
        if (err.isEmpty() && legacy.isObject()) {
            const Value *format = Json::find(legacy.object, QStringLiteral("format"));
            const Value *version = Json::find(legacy.object, QStringLiteral("version"));
            const Value *id = Json::find(legacy.object, QStringLiteral("id"));
            const Value *nodes = Json::find(legacy.object, QStringLiteral("nodes"));
            const Value *edges = Json::find(legacy.object, QStringLiteral("edges"));
            const bool valid = format && format->isString() && format->string == QStringLiteral("hypraccel.mbd.graph")
                && version && version->isNumber() && version->number == 1.0
                && id && id->isString() && GraphStore::isValidGraphId(id->string)
                && nodes && nodes->isArray() && edges && edges->isArray();
            if (valid) {
                const QString graphId = id->string;
                const QString destination = QDir(GraphStore::graphsDir(root, projectId)).filePath(graphId + QStringLiteral(".json"));
                if (!QFileInfo::exists(destination))
                    writeJsonFile(destination, legacy);
                const Value *active = Json::find(manifest.object, QStringLiteral("activeGraphId"));
                if (!active || !active->isString() || active->string.isEmpty())
                    Json::set(manifest.object, QStringLiteral("activeGraphId"), Value::fromString(graphId));
            }
        }
    }
    Json::remove(manifest.object, QStringLiteral("graph"));
    Json::Object graphsObj;
    Json::set(graphsObj, QStringLiteral("path"), Value::fromString(QStringLiteral("graphs")));
    Json::set(manifest.object, QStringLiteral("graphs"), Value::fromObject(std::move(graphsObj)));
    const Value *updatedAt = Json::find(manifest.object, QStringLiteral("updatedAt"));
    if (!updatedAt || !updatedAt->isString() || updatedAt->string.isEmpty())
        Json::set(manifest.object, QStringLiteral("updatedAt"), Value::fromString(nowIso()));
    writeJsonFile(manifestPath(root, projectId), manifest);
    return manifest;
}

QString stringField(const Value &object, const QString &key)
{
    const Value *v = Json::find(object.object, key);
    return (v && v->isString()) ? v->string : QString();
}

GraphDocument documentFromJson(const Value &graph, const QString &graphId,
                                const std::function<QString(const QString &)> &displayName = {})
{
    GraphDocument doc;
    doc.id = graphId;
    const QString rawName = stringField(graph, QStringLiteral("name"));
    doc.name = rawName.isEmpty() ? graphId : rawName;

    const Value *nodesArr = Json::find(graph.object, QStringLiteral("nodes"));
    if (nodesArr && nodesArr->isArray()) {
        for (const Value &n : nodesArr->array) {
            if (!n.isObject()) continue;
            GraphNode node;
            node.id = stringField(n, QStringLiteral("id"));
            node.type = stringField(n, QStringLiteral("type"));
            const QString label = stringField(n, QStringLiteral("label"));
            if (!label.isEmpty()) {
                node.label = label;
            } else {
                const QString displayed = displayName && !displayName(node.type).isEmpty()
                    ? displayName(node.type) : node.type;
                node.label = displayed + QLatin1Char(' ') + node.id;
            }
            const Value *paramsV = Json::find(n.object, QStringLiteral("params"));
            if (paramsV && paramsV->isObject()) node.params = paramsV->object;
            const Value *posV = Json::find(n.object, QStringLiteral("position"));
            if (posV && posV->isObject()) {
                const Value *x = Json::find(posV->object, QStringLiteral("x"));
                const Value *y = Json::find(posV->object, QStringLiteral("y"));
                node.position = QPointF(x && x->isNumber() ? x->number : 0.0, y && y->isNumber() ? y->number : 0.0);
            } else {
                node.position = QPointF(0.0, 0.0);
            }
            doc.nodes.push_back(std::move(node));
        }
    }

    const Value *edgesArr = Json::find(graph.object, QStringLiteral("edges"));
    if (edgesArr && edgesArr->isArray()) {
        int index = 0;
        for (const Value &e : edgesArr->array) {
            if (!e.isObject()) { ++index; continue; }
            GraphEdge edge;
            const Value *fromV = Json::find(e.object, QStringLiteral("from"));
            const Value *toV = Json::find(e.object, QStringLiteral("to"));
            if (fromV && fromV->isObject()) {
                edge.fromNode = stringField(*fromV, QStringLiteral("node"));
                edge.fromPort = stringField(*fromV, QStringLiteral("port"));
            }
            if (toV && toV->isObject()) {
                edge.toNode = stringField(*toV, QStringLiteral("node"));
                edge.toPort = stringField(*toV, QStringLiteral("port"));
            }
            const QString idField = stringField(e, QStringLiteral("id"));
            if (!idField.isEmpty()) {
                edge.id = idField;
            } else {
                edge.id = edge.fromNode + QLatin1Char('-') + edge.fromPort + QLatin1Char('-')
                    + edge.toNode + QLatin1Char('-') + edge.toPort + QLatin1Char('-') + QString::number(index);
            }
            doc.edges.push_back(std::move(edge));
            ++index;
        }
    }
    return doc;
}

Value assertGraphDocumentJson(const Value &graph)
{
    if (!graph.isObject()) fail(QStringLiteral("Graph must be a hypraccel.mbd.graph version 1 document with a valid id, nodes, and edges."));
    const Value *format = Json::find(graph.object, QStringLiteral("format"));
    const Value *version = Json::find(graph.object, QStringLiteral("version"));
    const Value *id = Json::find(graph.object, QStringLiteral("id"));
    const Value *nodes = Json::find(graph.object, QStringLiteral("nodes"));
    const Value *edges = Json::find(graph.object, QStringLiteral("edges"));
    const bool ok = format && format->isString() && format->string == QStringLiteral("hypraccel.mbd.graph")
        && version && version->isNumber() && version->number == 1.0
        && id && id->isString() && GraphStore::isValidGraphId(id->string)
        && nodes && nodes->isArray() && edges && edges->isArray();
    if (!ok) fail(QStringLiteral("Graph must be a hypraccel.mbd.graph version 1 document with a valid id, nodes, and edges."));
    return graph;
}

} // namespace

int GraphStore::compareLikeNodeLocale(const QString &a, const QString &b)
{
    return localeCompareLikeNode(a, b);
}

bool GraphStore::isValidGraphId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,63}$"));
    return re.match(id).hasMatch();
}

QString GraphStore::graphsDir(const QString &root, const QString &projectId)
{
    return QDir(projectDir(root, projectId)).filePath(QStringLiteral("graphs"));
}

QString GraphStore::displayName(const QString &name, const QString &fallback)
{
    if (name.isEmpty()) return fallback;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 120)
        fail(QStringLiteral("Graph name must be a non-empty string no longer than 120 characters."));
    return trimmed;
}

QString GraphStore::graphIdFromName(const QString &name)
{
    QString words = name.trimmed();
    words.replace(QRegularExpression(QStringLiteral("([a-z0-9])([A-Z])")), QStringLiteral("\\1-\\2"));
    words.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("_"));
    words.replace(QRegularExpression(QStringLiteral("^[_-]+|[_-]+$")), QString());
    if (words.isEmpty()) return QString();
    if (QRegularExpression(QStringLiteral("^[A-Za-z]")).match(words).hasMatch())
        return words.left(64);
    return (QStringLiteral("graph_") + words).left(64);
}

void GraphStore::migrateLegacyGraphIfNeeded(const QString &root, const QString &projectId)
{
    ensureMigrated(root, projectId);
}

QList<GraphListEntry> GraphStore::listGraphs(const QString &root, const QString &projectId)
{
    ensureMigrated(root, projectId);
    QList<GraphListEntry> entries;
    QDir dir(graphsDir(root, projectId));
    if (!dir.exists()) return entries;
    const auto files = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files);
    for (const QString &filename : files) {
        if (!filename.endsWith(QStringLiteral(".json"))) continue;
        const QString stem = filename.left(filename.size() - 5);
        if (!isValidGraphId(stem)) continue;
        const QString path = dir.filePath(filename);
        QString err;
        Value graph = Json::parse(readFileBytes(path, QStringLiteral("graph '%1'").arg(stem)), &err);
        if (!err.isEmpty()) fail(QStringLiteral("Could not read graph '%1': %2").arg(stem, err));
        assertGraphDocumentJson(graph);
        const QString id = stringField(graph, QStringLiteral("id"));
        if (id != stem) fail(QStringLiteral("Graph file '%1' has an id that does not match its filename.").arg(filename));
        GraphListEntry entry;
        entry.id = stem;
        entry.name = displayName(stringField(graph, QStringLiteral("name")), stem);
        entry.filename = filename;
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const GraphListEntry &a, const GraphListEntry &b) {
        return localeCompareLikeNode(a.filename, b.filename) < 0;
    });
    return entries;
}

QString GraphStore::resolveActiveGraphId(const QString &root, const QString &projectId)
{
    Value manifest = ensureMigrated(root, projectId);
    const Value *active = Json::find(manifest.object, QStringLiteral("activeGraphId"));
    if (active && active->isString() && !active->string.isEmpty())
        return active->string;
    const auto graphs = listGraphs(root, projectId);
    if (!graphs.isEmpty()) return graphs.first().id;
    return QString();
}

GraphDocument GraphStore::load(const QString &root, const QString &projectId, const QString &graphId,
                                const std::function<QString(const QString &)> &displayName)
{
    ensureMigrated(root, projectId);
    if (!isValidGraphId(graphId))
        fail(QStringLiteral("Graph id must start with a letter and contain only letters, numbers, hyphens, or underscores."));
    const QString path = QDir(graphsDir(root, projectId)).filePath(graphId + QStringLiteral(".json"));
    QFileInfo info(path);
    if (info.isSymLink())
        fail(QStringLiteral("Graph files may not be symbolic links."));
    if (!info.exists()) {
        QString err = QStringLiteral("Graph '%1' was not found in project '%2'.").arg(graphId, projectId);
        fail(err);
    }
    QString err;
    Value graph = Json::parse(readFileBytes(path, QStringLiteral("project graph '%1'").arg(graphId)), &err);
    if (!err.isEmpty()) fail(QStringLiteral("Could not read project graph '%1': %2").arg(graphId, err));
    assertGraphDocumentJson(graph);
    const QString id = stringField(graph, QStringLiteral("id"));
    if (id != graphId) fail(QStringLiteral("Graph '%1' has an inconsistent document id.").arg(graphId));
    return documentFromJson(graph, graphId, displayName);
}

Value GraphStore::toDocument(const GraphDocument &doc, const QString &targetBoard)
{
    Json::Object root;
    Json::set(root, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.mbd.graph")));
    Json::set(root, QStringLiteral("version"), Value::fromNumber(1));
    const QString graphId = doc.id.isEmpty() ? QStringLiteral("editor-graph") : doc.id;
    Json::set(root, QStringLiteral("id"), Value::fromString(graphId));
    Json::set(root, QStringLiteral("name"), Value::fromString(doc.name.isEmpty() ? graphId : doc.name));

    Json::Array nodes;
    QStringList cordicIds;
    for (const GraphNode &node : doc.nodes) {
        Json::Object n;
        Json::set(n, QStringLiteral("id"), Value::fromString(node.id));
        Json::set(n, QStringLiteral("type"), Value::fromString(node.type));
        Json::set(n, QStringLiteral("label"), Value::fromString(node.label));
        Json::set(n, QStringLiteral("params"), Value::fromObject(node.params));
        Json::Object pos;
        Json::set(pos, QStringLiteral("x"), Value::fromNumber(node.position.x()));
        Json::set(pos, QStringLiteral("y"), Value::fromNumber(node.position.y()));
        Json::set(n, QStringLiteral("position"), Value::fromObject(std::move(pos)));
        nodes.push_back(Value::fromObject(std::move(n)));
        if (node.type == QStringLiteral("CordicOp")) cordicIds.push_back(node.id);
    }
    Json::set(root, QStringLiteral("nodes"), Value::fromArray(std::move(nodes)));

    Json::Array edges;
    for (const GraphEdge &edge : doc.edges) {
        Json::Object e;
        Json::set(e, QStringLiteral("id"), Value::fromString(edge.id));
        Json::Object from;
        Json::set(from, QStringLiteral("node"), Value::fromString(edge.fromNode));
        Json::set(from, QStringLiteral("port"), Value::fromString(edge.fromPort));
        Json::set(e, QStringLiteral("from"), Value::fromObject(std::move(from)));
        Json::Object to;
        Json::set(to, QStringLiteral("node"), Value::fromString(edge.toNode));
        Json::set(to, QStringLiteral("port"), Value::fromString(edge.toPort));
        Json::set(e, QStringLiteral("to"), Value::fromObject(std::move(to)));
        edges.push_back(Value::fromObject(std::move(e)));
    }
    Json::set(root, QStringLiteral("edges"), Value::fromArray(std::move(edges)));

    Json::Array inputs;
    if (!cordicIds.isEmpty()) {
        Json::Object input;
        Json::set(input, QStringLiteral("name"), Value::fromString(QStringLiteral("angle_rad")));
        Json::set(input, QStringLiteral("type"), Value::fromString(QStringLiteral("number")));
        Json::set(input, QStringLiteral("unit"), Value::fromString(QStringLiteral("rad")));
        inputs.push_back(Value::fromObject(std::move(input)));
    }
    Json::set(root, QStringLiteral("inputs"), Value::fromArray(std::move(inputs)));

    Json::Object metadata;
    if (!targetBoard.isEmpty()) Json::set(metadata, QStringLiteral("targetBoard"), Value::fromString(targetBoard));
    if (!cordicIds.isEmpty()) {
        Json::Array bindings;
        for (const QString &nodeId : cordicIds) {
            Json::Object binding;
            Json::set(binding, QStringLiteral("input"), Value::fromString(QStringLiteral("angle_rad")));
            Json::Object to;
            Json::set(to, QStringLiteral("node"), Value::fromString(nodeId));
            Json::set(to, QStringLiteral("port"), Value::fromString(QStringLiteral("angle_rad")));
            Json::set(binding, QStringLiteral("to"), Value::fromObject(std::move(to)));
            bindings.push_back(Value::fromObject(std::move(binding)));
        }
        Json::set(metadata, QStringLiteral("externalBindings"), Value::fromArray(std::move(bindings)));
    }
    Json::set(root, QStringLiteral("metadata"), Value::fromObject(std::move(metadata)));

    return Value::fromObject(std::move(root));
}

namespace {

void invalidateArtifacts(const QString &root, const QString &projectId, const QString &graphId, Value &manifest)
{
    const Value *gen = Json::find(manifest.object, QStringLiteral("generatedGraphId"));
    const Value *build = Json::find(manifest.object, QStringLiteral("buildGraphId"));
    const bool genMatches = gen && gen->isString() && gen->string == graphId;
    const bool buildMatches = build && build->isString() && build->string == graphId;
    if (!genMatches && !buildMatches) return;
    removeDirRecursively(generatedDir(root, projectId));
    removeDirRecursively(buildDir(root, projectId));
    if (genMatches) Json::set(manifest.object, QStringLiteral("generatedGraphId"), Value::null());
    if (buildMatches) Json::set(manifest.object, QStringLiteral("buildGraphId"), Value::null());
}

} // namespace

void GraphStore::save(const QString &root, const QString &projectId, GraphDocument &doc,
                       const QString &targetBoard, bool create)
{
    Value manifest = ensureMigrated(root, projectId);
    if (!isValidGraphId(doc.id))
        fail(QStringLiteral("Graph id must start with a letter and contain only letters, numbers, hyphens, or underscores."));
    const QString graphId = doc.id;
    Value graph = toDocument(doc, targetBoard);
    const QString trimmedName = displayName(stringField(graph, QStringLiteral("name")), graphId);
    Json::set(graph.object, QStringLiteral("name"), Value::fromString(trimmedName));
    doc.name = trimmedName;

    const QString path = QDir(graphsDir(root, projectId)).filePath(graphId + QStringLiteral(".json"));
    if (create && QFileInfo::exists(path))
        fail(QStringLiteral("Graph '%1' already exists.").arg(graphId));

    writeJsonFile(path, graph);

    invalidateArtifacts(root, projectId, graphId, manifest);
    Json::set(manifest.object, QStringLiteral("activeGraphId"), Value::fromString(graphId));
    Json::set(manifest.object, QStringLiteral("updatedAt"), Value::fromString(nowIso()));
    writeJsonFile(manifestPath(root, projectId), manifest);
}

Value GraphStore::saveAndPrepareForGenerate(const QString &root, const QString &projectId, GraphDocument &doc,
                                             const QString &targetBoard, bool create)
{
    save(root, projectId, doc, targetBoard, create);
    return toDocument(doc, targetBoard);
}

void GraphStore::setActive(const QString &root, const QString &projectId, const QString &graphId)
{
    // readProjectGraph() validates existence first.
    load(root, projectId, graphId);
    Value manifest = ensureMigrated(root, projectId);
    Json::set(manifest.object, QStringLiteral("activeGraphId"), Value::fromString(graphId));
    Json::set(manifest.object, QStringLiteral("updatedAt"), Value::fromString(nowIso()));
    writeJsonFile(manifestPath(root, projectId), manifest);
}

GraphDocument GraphStore::rename(const QString &root, const QString &projectId,
                                  const QString &oldGraphId, const QString &newGraphId,
                                  const QString &newName)
{
    if (!isValidGraphId(oldGraphId) || !isValidGraphId(newGraphId))
        fail(QStringLiteral("Graph id must start with a letter and contain only letters, numbers, hyphens, or underscores."));

    if (newGraphId == oldGraphId) {
        // Server still round-trips through readProjectGraph()/projectResponse();
        // for us that just means "no-op, return the current document".
        return load(root, projectId, oldGraphId);
    }

    const QString oldPath = QDir(graphsDir(root, projectId)).filePath(oldGraphId + QStringLiteral(".json"));
    const QString newPath = QDir(graphsDir(root, projectId)).filePath(newGraphId + QStringLiteral(".json"));

    QString err;
    Value graph = Json::parse(readFileBytes(oldPath, QStringLiteral("project graph '%1'").arg(oldGraphId)), &err);
    if (!err.isEmpty()) fail(err);
    assertGraphDocumentJson(graph);

    if (QFileInfo::exists(newPath))
        fail(QStringLiteral("Graph '%1' already exists.").arg(newGraphId));

    Json::set(graph.object, QStringLiteral("id"), Value::fromString(newGraphId));
    const QString fallbackName = stringField(graph, QStringLiteral("name"));
    Json::set(graph.object, QStringLiteral("name"),
              Value::fromString(displayName(newName, fallbackName.isEmpty() ? newGraphId : fallbackName)));

    writeJsonFile(newPath, graph);
    QFile::remove(oldPath);

    Value manifest = ensureMigrated(root, projectId);
    const Value *gen = Json::find(manifest.object, QStringLiteral("generatedGraphId"));
    const Value *build = Json::find(manifest.object, QStringLiteral("buildGraphId"));
    const bool hadGen = gen && gen->isString() && gen->string == oldGraphId;
    const bool hadBuild = build && build->isString() && build->string == oldGraphId;

    const Value *active = Json::find(manifest.object, QStringLiteral("activeGraphId"));
    if (active && active->isString() && active->string == oldGraphId)
        Json::set(manifest.object, QStringLiteral("activeGraphId"), Value::fromString(newGraphId));
    if (hadGen) Json::set(manifest.object, QStringLiteral("generatedGraphId"), Value::null());
    if (hadBuild) Json::set(manifest.object, QStringLiteral("buildGraphId"), Value::null());
    if (hadGen || hadBuild) {
        removeDirRecursively(generatedDir(root, projectId));
        removeDirRecursively(buildDir(root, projectId));
    }
    Json::set(manifest.object, QStringLiteral("updatedAt"), Value::fromString(nowIso()));
    writeJsonFile(manifestPath(root, projectId), manifest);

    return documentFromJson(graph, newGraphId);
}

void GraphStore::remove(const QString &root, const QString &projectId, const QString &graphId)
{
    const auto graphs = listGraphs(root, projectId);
    auto it = std::find_if(graphs.begin(), graphs.end(), [&](const GraphListEntry &e) { return e.id == graphId; });
    if (it == graphs.end())
        fail(QStringLiteral("Graph '%1' was not found in project '%2'.").arg(graphId, projectId));
    if (graphs.size() <= 1)
        fail(QStringLiteral("A project must retain at least one graph file. Create another graph before deleting this one."));

    const QString path = QDir(graphsDir(root, projectId)).filePath(graphId + QStringLiteral(".json"));
    QFile::remove(path);

    Value manifest = ensureMigrated(root, projectId);
    QString nextGraphId;
    for (const GraphListEntry &e : graphs) {
        if (e.id != graphId) { nextGraphId = e.id; break; }
    }
    const Value *active = Json::find(manifest.object, QStringLiteral("activeGraphId"));
    if (active && active->isString() && active->string == graphId)
        Json::set(manifest.object, QStringLiteral("activeGraphId"), Value::fromString(nextGraphId));

    const Value *gen = Json::find(manifest.object, QStringLiteral("generatedGraphId"));
    const Value *build = Json::find(manifest.object, QStringLiteral("buildGraphId"));
    const bool genMatches = gen && gen->isString() && gen->string == graphId;
    const bool buildMatches = build && build->isString() && build->string == graphId;
    if (genMatches || buildMatches) {
        removeDirRecursively(generatedDir(root, projectId));
        removeDirRecursively(buildDir(root, projectId));
        Json::set(manifest.object, QStringLiteral("generatedGraphId"), Value::null());
        Json::set(manifest.object, QStringLiteral("buildGraphId"), Value::null());
    }
    Json::set(manifest.object, QStringLiteral("updatedAt"), Value::fromString(nowIso()));
    writeJsonFile(manifestPath(root, projectId), manifest);
}

} // namespace Hypr
