#pragma once

#include "board_catalog.h"
#include "clock_tree.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

// CONTRACT (owned by the integrator; implement in project_store.cpp).
// Persists desktop configuration into the same project store the web editor
// uses (mbd/editor/server.js):
//   <root>/<id>/project.json            manifest (created if missing, see below)
//   <root>/<id>/hardware/hardware.json  pins, server.js projectHardware() shape
//   <root>/<id>/hardware/clock.json     {"version":1,"board":<id>,"selections":{...},
//                                        "resolved":{<nodeId>:MHz},"errors":[...]}
// "resolved"/"errors" are the ClockView's computeClocks() result at save time,
// so codegen consumes the single (C++) clock computation instead of redoing it.
namespace Hypr {

struct ProjectSnapshot {
    QString id;                              // PROJECT_ID grammar ^[A-Za-z][A-Za-z0-9_-]{0,63}$
    QString name;                            // display name, 1..120 chars
    QString board;                           // boards.yaml key
    QMap<QString, QString> pinAssignments;   // PinAssignmentModel::assignments()
    ClockConfig clock;
    ClockResult clockResult;                 // written to clock.json, ignored by load()
};

class ProjectStore {
public:
    // Default root: $HYPRACCEL_PROJECTS_ROOT, else <repo>/.hypraccel/projects.
    static QString defaultRoot();
    static QStringList list(const QString &root);          // ids with a valid manifest
    // Writes atomically (QSaveFile). New project: manifest {format:"hypraccel.project",
    // version:1,id,name,hardware:{path:"hardware/hardware.json"},graphs:{path:"graphs"},
    // createdAt,updatedAt} + empty graphs/. Existing project: keep manifest fields,
    // update name/updatedAt only; never touch graphs/ or other files.
    // Throws std::runtime_error on invalid id/name/board or I/O failure.
    static void save(const QString &root, const ProjectSnapshot &snapshot, const QString &catalogPath);
    // Reads manifest + hardware.json (+ clock.json if present). Throws on missing
    // or invalid manifest. Unknown resource/role pairs are skipped.
    static ProjectSnapshot load(const QString &root, const QString &id);

    // Pure: the exact hardware.json object save() writes, exposed so other
    // callers (e.g. the live pin-conflict check, desktop/src/pincheck) can
    // build the SAME payload without duplicating this serialization.
    // `existing` is the project's current hardware.json (may be empty
    // QJsonObject()); when it targets the same board, per-resource
    // configuration/devices/graph-node bindings are carried over.
    static QJsonObject hardwareJson(const Board &board, const QMap<QString, QString> &pinAssignments,
                                     const QJsonObject &existing = QJsonObject());
};

} // namespace Hypr
