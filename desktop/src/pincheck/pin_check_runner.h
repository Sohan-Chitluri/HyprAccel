#pragma once

// CONTRACT (owned by the integrator; implement in pin_check_runner.cpp).
//
// Live pin-conflict checking for the Pin Inspector. Reuses the SAME
// boards/codegen/pin_conflicts.js rules codegen would block on by shelling
// out to boards/codegen/check_pin_conflicts.js (a thin CLI wrapper around
// that module) via QProcess — no conflict-rule logic is reimplemented here.
//
// The stdin payload uses ProjectStore::hardwareJson(), the SAME hardware.json
// serialization ProjectStore uses when saving a project, so the check sees
// exactly what codegen would.

#include "board_catalog.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace Hypr {

struct PinCheckIssue {
    QString code;
    QString message;
    QString pin;      // may be empty
    QString resource;  // may be empty
    QString node;      // may be empty
};

struct PinCheckResult {
    // false when the check itself could not run (node missing, script
    // failed/timed out, malformed output) — callers must show a clear
    // "Conflict check unavailable: <reason>" and never silently show "no
    // issues" in that case.
    bool available = false;
    QString unavailableReason;
    QList<PinCheckIssue> errors;
    QList<PinCheckIssue> warnings;
};

// Builds the stdin JSON payload boards/codegen/check_pin_conflicts.js
// expects: {"board": <id>, "hardware": <ProjectStore::hardwareJson(...)>}.
QByteArray buildPinCheckPayload(const Board &board, const QMap<QString, QString> &pinAssignments,
                                 const QJsonObject &existingHardware = QJsonObject());

// Parses check_pin_conflicts.js stdout. `crashed` covers QProcess::crashed /
// failed-to-start / timeout. Any shape that isn't the documented
// {"errors":[...],"warnings":[...]} (or {"error": "..."} from a non-zero
// exit) yields available=false with a readable reason.
PinCheckResult parsePinCheckOutput(const QByteArray &stdoutBytes, int exitCode, bool crashed,
                                    const QString &crashReason = QString());

// Locates the node executable: $HYPRACCEL_NODE if set, else
// QStandardPaths::findExecutable("node"). Empty when not found.
QString findNodeExecutable();

// Runs the check asynchronously via QProcess, debounced (~250ms) so bursts of
// assignment changes collapse into one run, and with a generation counter so
// a stale in-flight run's result is dropped once a newer request has been
// made ("latest request wins"); the stale QProcess is killed rather than left
// to finish uselessly.
class PinCheckRunner : public QObject {
    Q_OBJECT
public:
    // scriptPath: boards/codegen/check_pin_conflicts.js (PIN_CHECK_SCRIPT_PATH).
    // boardsYamlPath: boards/boards.yaml (BOARD_CATALOG_PATH), passed as argv[1].
    PinCheckRunner(QString scriptPath, QString boardsYamlPath, QObject *parent = nullptr);
    ~PinCheckRunner() override;

    // Debounced entry point: call on every pin-assignment change.
    void requestCheck(const Board &board, const QMap<QString, QString> &pinAssignments,
                       const QJsonObject &existingHardware = QJsonObject());

    // Test/CLI convenience: bypasses the debounce and blocks until the
    // process exits or `timeoutMs` elapses. Emits resultReady() same as async.
    PinCheckResult runSync(const Board &board, const QMap<QString, QString> &pinAssignments,
                            const QJsonObject &existingHardware = QJsonObject(), int timeoutMs = 5000);

    int debounceMs() const { return debounceMs_; }
    void setDebounceMs(int ms) { debounceMs_ = ms; }

Q_SIGNALS:
    void resultReady(const Hypr::PinCheckResult &result);

private:
    QString scriptPath_;
    QString boardsYamlPath_;
    QTimer debounceTimer_;
    int debounceMs_ = 250;
    quint64 generation_ = 0;

    Board pendingBoard_;
    QMap<QString, QString> pendingAssignments_;
    QJsonObject pendingExisting_;

    QProcess *inFlight_ = nullptr;
    quint64 inFlightGeneration_ = 0;

    void fireDebounced();
    void startAsync(const Board &board, const QMap<QString, QString> &pinAssignments,
                     const QJsonObject &existingHardware, quint64 generation);
};

} // namespace Hypr

Q_DECLARE_METATYPE(Hypr::PinCheckResult)
