#include "project_store.h"

#include "board_catalog.h"
#include "pin_assignment_model.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QHash>
#include <QSaveFile>

#include <algorithm>
#include <stdexcept>

namespace Hypr {
namespace {

[[noreturn]] void fail(const QString &reason)
{
    throw std::runtime_error(reason.toStdString());
}

const QRegularExpression &projectIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_-]{0,63}$"));
    return pattern;
}

void assertProjectId(const QString &id)
{
    if (!projectIdPattern().match(id).hasMatch())
        fail(QStringLiteral("Project id must start with a letter and contain only letters, numbers, hyphens, or underscores."));
}

void assertProjectName(const QString &name)
{
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.length() > 120)
        fail(QStringLiteral("Project name must be a non-empty string no longer than 120 characters."));
}

QJsonDocument readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonDocument();
    const auto bytes = file.readAll();
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        return QJsonDocument();
    return document;
}

void writeJsonFile(const QString &path, const QJsonDocument &document)
{
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        fail(QStringLiteral("Could not create directory: ") + dir.path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Could not open for writing: ") + path);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit())
        fail(QStringLiteral("Could not save: ") + path);
}

// Instance is the portion of the resource id after the first '.': for bus
// resources ("uart.uart0") that's the instance name, for pwm/adc/gpio
// ("pwm.GPIO25") that's the pin, for accelerators ("accelerator.cordic")
// that's the accelerator name.
QString instanceOf(const QString &resourceId)
{
    return resourceId.section(QLatin1Char('.'), 1);
}

// Builds the resources object exactly as mbd/editor/server.js
// defaultResourceConfig(board) does, from Hypr::BoardCatalog data.
QJsonObject buildResources(const Board &board)
{
    QJsonObject resources;
    for (const auto &resource : board.resources) {
        QJsonArray assignments;
        // Deterministic ordering for a stable diff / round trip: sort by role.
        QStringList roles = resource.signals.keys();
        std::sort(roles.begin(), roles.end());
        for (const auto &role : roles) {
            QJsonObject entry;
            entry["role"] = role;
            entry["pin"] = resource.signals.value(role);
            assignments.append(entry);
        }
        QJsonObject entry;
        entry["id"] = resource.id;
        entry["type"] = resource.type;
        entry["instance"] = instanceOf(resource.id);
        entry["available"] = true;
        entry["assignments"] = assignments;
        entry["configuration"] = QJsonObject();
        resources[resource.id] = entry;
    }
    return resources;
}

const Board *findBoard(const QList<Board> &boards, const QString &id)
{
    for (const auto &board : boards)
        if (board.id == id)
            return &board;
    return nullptr;
}

// existing: the project's current hardware.json (may be empty). When it targets
// the same board, state the desktop does not edit yet — per-resource
// configuration, device profiles and graph-node bindings written by the web
// editor — is carried over instead of being reset.
QJsonObject buildHardwareJson(const Board &board, const QMap<QString, QString> &pinAssignments,
                              const QJsonObject &existing)
{
    const bool sameBoard = existing.value("board").toString() == board.id;
    QHash<QString, QString> existingNodes; // "pin|resource|role" -> node
    if (sameBoard) {
        for (const auto &value : existing.value("assignments").toArray()) {
            const auto entry = value.toObject();
            existingNodes.insert(entry.value("pin").toString() + '|' + entry.value("resource").toString() + '|'
                                     + entry.value("role").toString(),
                                 entry.value("node").toString());
        }
    }
    QJsonObject root;
    root["version"] = 1;
    root["board"] = board.id;
    auto resources = buildResources(board);
    if (sameBoard) {
        const auto previous = existing.value("resources").toObject();
        for (auto it = resources.begin(); it != resources.end(); ++it) {
            const auto configuration = previous.value(it.key()).toObject().value("configuration");
            if (configuration.isObject()) {
                auto entry = it.value().toObject();
                entry["configuration"] = configuration;
                it.value() = entry;
            }
        }
    }
    root["resources"] = resources;

    QJsonArray assignments;
    QStringList pins = pinAssignments.keys();
    std::sort(pins.begin(), pins.end());
    for (const auto &pin : pins) {
        const auto function = pinAssignments.value(pin);
        if (function.isEmpty())
            continue;
        const auto resourceRole = resourceRoleFor(pin, function);
        if (resourceRole.resource.isEmpty())
            continue;
        QJsonObject entry;
        entry["node"] = existingNodes.value(pin + '|' + resourceRole.resource + '|' + resourceRole.role);
        entry["role"] = resourceRole.role;
        entry["pin"] = pin;
        entry["resource"] = resourceRole.resource;
        assignments.append(entry);
    }
    root["assignments"] = assignments;
    root["devices"] = sameBoard && existing.value("devices").isArray() ? existing.value("devices").toArray() : QJsonArray();
    return root;
}

QJsonObject buildClockJson(const QString &boardId, const ClockConfig &clock, const ClockResult &result)
{
    QJsonObject root;
    root["version"] = 1;
    root["board"] = boardId;
    QJsonObject selections;
    for (auto it = clock.selections.begin(); it != clock.selections.end(); ++it)
        selections[it.key()] = it.value();
    root["selections"] = selections;
    QJsonObject resolved;
    for (auto it = result.freqMHz.begin(); it != result.freqMHz.end(); ++it)
        resolved[it.key()] = it.value();
    root["resolved"] = resolved;
    root["errors"] = QJsonArray::fromStringList(result.errors);
    return root;
}

struct ProjectPaths {
    QString projectDir;
    QString manifest;
    QString hardware;
    QString clock;
    QString graphsDir;
};

ProjectPaths pathsFor(const QString &root, const QString &id)
{
    ProjectPaths paths;
    paths.projectDir = QDir(root).filePath(id);
    paths.manifest = QDir(paths.projectDir).filePath(QStringLiteral("project.json"));
    const QString hardwareDir = QDir(paths.projectDir).filePath(QStringLiteral("hardware"));
    paths.hardware = QDir(hardwareDir).filePath(QStringLiteral("hardware.json"));
    paths.clock = QDir(hardwareDir).filePath(QStringLiteral("clock.json"));
    paths.graphsDir = QDir(paths.projectDir).filePath(QStringLiteral("graphs"));
    return paths;
}

bool isValidManifest(const QJsonObject &manifest, const QString &id)
{
    return manifest.value("format").toString() == QLatin1String("hypraccel.project")
        && manifest.value("version").toInt(-1) == 1
        && manifest.value("id").toString() == id;
}

} // namespace

QString ProjectStore::defaultRoot()
{
    const auto env = qEnvironmentVariable("HYPRACCEL_PROJECTS_ROOT");
    if (!env.isEmpty())
        return env;
#ifdef HYPRACCEL_REPO_ROOT
    return QString(QStringLiteral(HYPRACCEL_REPO_ROOT)) + QStringLiteral("/.hypraccel/projects");
#else
    return QDir::current().filePath(QStringLiteral(".hypraccel/projects"));
#endif
}

QStringList ProjectStore::list(const QString &root)
{
    QStringList ids;
    QDir dir(root);
    if (!dir.exists())
        return ids;
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (!projectIdPattern().match(entry).hasMatch())
            continue;
        const auto paths = pathsFor(root, entry);
        const auto document = readJsonFile(paths.manifest);
        if (!document.isObject())
            continue;
        if (isValidManifest(document.object(), entry))
            ids.append(entry);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void ProjectStore::save(const QString &root, const ProjectSnapshot &snapshot, const QString &catalogPath)
{
    assertProjectId(snapshot.id);
    assertProjectName(snapshot.name);

    const auto boards = BoardCatalog::load(catalogPath);
    const Board *board = findBoard(boards, snapshot.board);
    if (!board)
        fail(QStringLiteral("Unknown board '") + snapshot.board + QStringLiteral("'."));

    const auto paths = pathsFor(root, snapshot.id);
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    const auto existingDocument = readJsonFile(paths.manifest);
    QJsonObject manifest;
    bool isNew = true;
    if (existingDocument.isObject() && isValidManifest(existingDocument.object(), snapshot.id)) {
        manifest = existingDocument.object();
        isNew = false;
    }

    if (isNew) {
        manifest = QJsonObject();
        manifest["format"] = QStringLiteral("hypraccel.project");
        manifest["version"] = 1;
        manifest["id"] = snapshot.id;
        QJsonObject hardwareRef;
        hardwareRef["path"] = QStringLiteral("hardware/hardware.json");
        manifest["hardware"] = hardwareRef;
        QJsonObject graphsRef;
        graphsRef["path"] = QStringLiteral("graphs");
        manifest["graphs"] = graphsRef;
        manifest["createdAt"] = now;
    }
    manifest["name"] = snapshot.name.trimmed();
    manifest["updatedAt"] = now;

    QDir().mkpath(paths.projectDir);
    if (isNew)
        QDir().mkpath(paths.graphsDir);

    writeJsonFile(paths.manifest, QJsonDocument(manifest));
    const auto existingHardware = readJsonFile(paths.hardware).object();
    writeJsonFile(paths.hardware, QJsonDocument(buildHardwareJson(*board, snapshot.pinAssignments, existingHardware)));
    writeJsonFile(paths.clock, QJsonDocument(buildClockJson(snapshot.board, snapshot.clock, snapshot.clockResult)));
}

ProjectSnapshot ProjectStore::load(const QString &root, const QString &id)
{
    assertProjectId(id);
    const auto paths = pathsFor(root, id);

    const auto manifestDocument = readJsonFile(paths.manifest);
    if (!manifestDocument.isObject() || !isValidManifest(manifestDocument.object(), id))
        fail(QStringLiteral("Project '") + id + QStringLiteral("' has an invalid or missing manifest."));
    const auto manifest = manifestDocument.object();

    ProjectSnapshot snapshot;
    snapshot.id = id;
    snapshot.name = manifest.value("name").toString();

    const auto hardwareDocument = readJsonFile(paths.hardware);
    if (hardwareDocument.isObject()) {
        const auto hardware = hardwareDocument.object();
        snapshot.board = hardware.value("board").toString();
        const auto assignments = hardware.value("assignments").toArray();
        for (const auto &value : assignments) {
            if (!value.isObject())
                continue;
            const auto entry = value.toObject();
            const auto pin = entry.value("pin").toString();
            const auto resource = entry.value("resource").toString();
            const auto role = entry.value("role").toString();
            if (pin.isEmpty())
                continue;
            const auto function = functionForResourceRole(resource, role);
            if (function.isEmpty())
                continue; // Unknown resource/role pairs are skipped.
            snapshot.pinAssignments.insert(pin, function);
        }
    }

    const auto clockDocument = readJsonFile(paths.clock);
    if (clockDocument.isObject()) {
        const auto clock = clockDocument.object();
        const auto selections = clock.value("selections").toObject();
        for (auto it = selections.begin(); it != selections.end(); ++it)
            snapshot.clock.selections.insert(it.key(), it.value().toString());
    }

    return snapshot;
}

} // namespace Hypr
