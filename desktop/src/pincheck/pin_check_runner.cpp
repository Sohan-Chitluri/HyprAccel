#include "pin_check_runner.h"
#include "project_store.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace Hypr {

QByteArray buildPinCheckPayload(const Board &board, const QMap<QString, QString> &pinAssignments,
                                 const QJsonObject &existingHardware)
{
    QJsonObject payload;
    payload["board"] = board.id;
    payload["hardware"] = ProjectStore::hardwareJson(board, pinAssignments, existingHardware);
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

namespace {

PinCheckIssue issueFromJson(const QJsonObject &obj)
{
    PinCheckIssue issue;
    issue.code = obj.value("code").toString();
    issue.message = obj.value("message").toString();
    issue.pin = obj.value("pin").toString();
    issue.resource = obj.value("resource").toString();
    issue.node = obj.value("node").toString();
    return issue;
}

QList<PinCheckIssue> issueListFromJson(const QJsonValue &value)
{
    QList<PinCheckIssue> issues;
    for (const auto &entry : value.toArray()) {
        if (entry.isObject())
            issues.append(issueFromJson(entry.toObject()));
    }
    return issues;
}

} // namespace

PinCheckResult parsePinCheckOutput(const QByteArray &stdoutBytes, int exitCode, bool crashed,
                                    const QString &crashReason)
{
    PinCheckResult result;
    if (crashed) {
        result.available = false;
        result.unavailableReason = crashReason.isEmpty() ? QStringLiteral("process failed to run.") : crashReason;
        return result;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(stdoutBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.available = false;
        result.unavailableReason = QStringLiteral("could not parse check output (%1).").arg(parseError.errorString());
        return result;
    }
    const auto obj = document.object();

    // check_pin_conflicts.js exits 2 with {"error": "..."} for malformed
    // input / an unknown board — that is a tooling failure, not "no issues".
    if (exitCode != 0 || obj.contains("error")) {
        result.available = false;
        result.unavailableReason = obj.value("error").toString(
            QStringLiteral("script exited with code %1.").arg(exitCode));
        return result;
    }

    if (!obj.contains("errors") || !obj.contains("warnings")) {
        result.available = false;
        result.unavailableReason = QStringLiteral("unexpected check output shape.");
        return result;
    }

    result.available = true;
    result.errors = issueListFromJson(obj.value("errors"));
    result.warnings = issueListFromJson(obj.value("warnings"));
    return result;
}

QString findNodeExecutable()
{
    // An explicit override always wins, even if it turns out not to point at
    // a real executable — the caller asked for this path specifically, and
    // QProcess::start() failing on it produces a clear "could not start"
    // reason rather than silently falling back to whatever is on PATH.
    const auto override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HYPRACCEL_NODE"));
    if (!override.isEmpty())
        return override;
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

PinCheckRunner::PinCheckRunner(QString scriptPath, QString boardsYamlPath, QObject *parent)
    : QObject(parent)
    , scriptPath_(std::move(scriptPath))
    , boardsYamlPath_(std::move(boardsYamlPath))
{
    qRegisterMetaType<Hypr::PinCheckResult>("Hypr::PinCheckResult");
    debounceTimer_.setSingleShot(true);
    connect(&debounceTimer_, &QTimer::timeout, this, &PinCheckRunner::fireDebounced);
}

PinCheckRunner::~PinCheckRunner()
{
    if (inFlight_) {
        inFlight_->kill();
        inFlight_->waitForFinished(200);
    }
}

void PinCheckRunner::requestCheck(const Board &board, const QMap<QString, QString> &pinAssignments,
                                   const QJsonObject &existingHardware)
{
    pendingBoard_ = board;
    pendingAssignments_ = pinAssignments;
    pendingExisting_ = existingHardware;
    debounceTimer_.start(debounceMs_);
}

void PinCheckRunner::fireDebounced()
{
    startAsync(pendingBoard_, pendingAssignments_, pendingExisting_, ++generation_);
}

void PinCheckRunner::startAsync(const Board &board, const QMap<QString, QString> &pinAssignments,
                                 const QJsonObject &existingHardware, quint64 generation)
{
    // A stale in-flight run is superseded, not awaited: kill it so it does
    // not consume resources or race the new result.
    if (inFlight_) {
        inFlight_->kill();
        inFlight_->deleteLater();
        inFlight_ = nullptr;
    }

    const QString node = findNodeExecutable();
    if (node.isEmpty()) {
        PinCheckResult result;
        result.available = false;
        result.unavailableReason = QStringLiteral("node executable not found (set HYPRACCEL_NODE or add node to PATH).");
        Q_EMIT resultReady(result);
        return;
    }
    if (!QFileInfo::exists(scriptPath_)) {
        PinCheckResult result;
        result.available = false;
        result.unavailableReason = QStringLiteral("check script not found at ") + scriptPath_;
        Q_EMIT resultReady(result);
        return;
    }

    auto *process = new QProcess(this);
    inFlight_ = process;
    inFlightGeneration_ = generation;
    const QByteArray payload = buildPinCheckPayload(board, pinAssignments, existingHardware);

    auto *timeoutTimer = new QTimer(process);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, process, [process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });

    connect(process, &QProcess::started, process, [process, payload]() {
        process->write(payload);
        process->closeWriteChannel();
    });

    connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, process, generation](int exitCode, QProcess::ExitStatus status) {
        const bool crashed = status == QProcess::CrashExit;
        const QByteArray out = process->readAllStandardOutput();
        const auto result = parsePinCheckOutput(out, exitCode, crashed,
            crashed ? QStringLiteral("process crashed (%1).").arg(QString::fromUtf8(process->readAllStandardError())) : QString());
        if (inFlight_ == process) inFlight_ = nullptr;
        process->deleteLater();
        if (generation == generation_) // still the latest request
            Q_EMIT resultReady(result);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error == QProcess::Crashed) return; // handled by finished()
        if (process->state() != QProcess::NotRunning) return; // will still emit finished()
        PinCheckResult result;
        result.available = false;
        result.unavailableReason = QStringLiteral("could not start check process (%1).").arg(process->errorString());
        if (inFlight_ == process) inFlight_ = nullptr;
        process->deleteLater();
        if (generation == generation_)
            Q_EMIT resultReady(result);
    });

    timeoutTimer->start(5000);
    process->start(node, { scriptPath_, boardsYamlPath_ });
}

PinCheckResult PinCheckRunner::runSync(const Board &board, const QMap<QString, QString> &pinAssignments,
                                        const QJsonObject &existingHardware, int timeoutMs)
{
    const QString node = findNodeExecutable();
    PinCheckResult result;
    if (node.isEmpty()) {
        result.available = false;
        result.unavailableReason = QStringLiteral("node executable not found (set HYPRACCEL_NODE or add node to PATH).");
        Q_EMIT resultReady(result);
        return result;
    }
    if (!QFileInfo::exists(scriptPath_)) {
        result.available = false;
        result.unavailableReason = QStringLiteral("check script not found at ") + scriptPath_;
        Q_EMIT resultReady(result);
        return result;
    }

    QProcess process;
    const QByteArray payload = buildPinCheckPayload(board, pinAssignments, existingHardware);
    process.start(node, { scriptPath_, boardsYamlPath_ });
    if (!process.waitForStarted(timeoutMs)) {
        result.available = false;
        result.unavailableReason = QStringLiteral("could not start check process (%1).").arg(process.errorString());
        Q_EMIT resultReady(result);
        return result;
    }
    process.write(payload);
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(200);
        result.available = false;
        result.unavailableReason = QStringLiteral("check process timed out.");
        Q_EMIT resultReady(result);
        return result;
    }
    const bool crashed = process.exitStatus() == QProcess::CrashExit;
    const QByteArray out = process.readAllStandardOutput();
    result = parsePinCheckOutput(out, process.exitCode(), crashed,
        crashed ? QStringLiteral("process crashed (%1).").arg(QString::fromUtf8(process.readAllStandardError())) : QString());
    Q_EMIT resultReady(result);
    return result;
}

} // namespace Hypr
