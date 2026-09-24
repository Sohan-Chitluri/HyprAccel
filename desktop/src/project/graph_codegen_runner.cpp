#include "graph_codegen_runner.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace Hypr {

namespace {

// Mirrors Hypr::findNodeExecutable() (desktop/src/pincheck/pin_check_runner.cpp)
// without adding a link dependency on studio_pincheck.
QString findNode()
{
    const auto override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HYPRACCEL_NODE"));
    if (!override.isEmpty()) return override;
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

QString trimmedStderr(const QByteArray &bytes)
{
    return QString::fromUtf8(bytes).trimmed();
}

} // namespace

GraphCodegenRunner::GraphCodegenRunner(QString scriptPath, QObject *parent)
    : QObject(parent), scriptPath_(std::move(scriptPath))
{
    qRegisterMetaType<Hypr::GraphCodegenResult>("Hypr::GraphCodegenResult");
}

GraphCodegenResult GraphCodegenRunner::generateSync(const Json::Value &graphDocument, int timeoutMs)
{
    GraphCodegenResult result;

    const QString node = findNode();
    if (node.isEmpty()) {
        result.unavailableReason = QStringLiteral("node executable not found (set HYPRACCEL_NODE or add node to PATH).");
        return result;
    }
    if (!QFileInfo::exists(scriptPath_)) {
        result.unavailableReason = QStringLiteral("codegen script not found at ") + scriptPath_;
        return result;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.unavailableReason = QStringLiteral("could not create a temporary directory.");
        return result;
    }
    const QString graphPath = tempDir.filePath(QStringLiteral("graph.json"));
    const QString outPath = tempDir.filePath(QStringLiteral("graph.c"));

    QFile graphFile(graphPath);
    if (!graphFile.open(QIODevice::WriteOnly)) {
        result.unavailableReason = QStringLiteral("could not write ") + graphPath;
        return result;
    }
    graphFile.write(Json::stringify(graphDocument));
    graphFile.close();

    QProcess process;
    process.start(node, { scriptPath_, graphPath, outPath });
    if (!process.waitForStarted(timeoutMs)) {
        result.unavailableReason = QStringLiteral("could not start codegen process (%1).").arg(process.errorString());
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(200);
        result.unavailableReason = QStringLiteral("codegen process timed out.");
        return result;
    }

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
    result.stdErr = trimmedStderr(process.readAllStandardError());

    if (process.exitStatus() == QProcess::CrashExit) {
        result.unavailableReason = QStringLiteral("codegen process crashed.");
        return result;
    }

    if (result.exitCode == 0 && QFileInfo::exists(outPath)) {
        QFile outFile(outPath);
        if (outFile.open(QIODevice::ReadOnly)) {
            result.source = QString::fromUtf8(outFile.readAll());
            result.ok = true;
        } else {
            result.unavailableReason = QStringLiteral("could not read generated output.");
        }
    }
    // else: ok stays false; result.stdErr carries graph_to_c's own
    // "graph_to_c: <message>" error text, same as server.js's err.stderr.trim().
    return result;
}

void GraphCodegenRunner::generate(const Json::Value &graphDocument)
{
    // A simple, correct async implementation: run the (short-lived) process
    // on a QProcess with asynchronous signals rather than QtConcurrent, to
    // match the PinCheckRunner pattern (desktop/src/pincheck).
    const QString node = findNode();
    if (node.isEmpty()) {
        GraphCodegenResult result;
        result.unavailableReason = QStringLiteral("node executable not found (set HYPRACCEL_NODE or add node to PATH).");
        Q_EMIT finished(result);
        return;
    }
    if (!QFileInfo::exists(scriptPath_)) {
        GraphCodegenResult result;
        result.unavailableReason = QStringLiteral("codegen script not found at ") + scriptPath_;
        Q_EMIT finished(result);
        return;
    }

    auto *tempDir = new QTemporaryDir();
    if (!tempDir->isValid()) {
        GraphCodegenResult result;
        result.unavailableReason = QStringLiteral("could not create a temporary directory.");
        Q_EMIT finished(result);
        delete tempDir;
        return;
    }
    const QString graphPath = tempDir->filePath(QStringLiteral("graph.json"));
    const QString outPath = tempDir->filePath(QStringLiteral("graph.c"));

    QFile graphFile(graphPath);
    if (!graphFile.open(QIODevice::WriteOnly)) {
        GraphCodegenResult result;
        result.unavailableReason = QStringLiteral("could not write ") + graphPath;
        Q_EMIT finished(result);
        delete tempDir;
        return;
    }
    graphFile.write(Json::stringify(graphDocument));
    graphFile.close();

    auto *process = new QProcess(this);
    process->setProgram(node);
    process->setArguments({ scriptPath_, graphPath, outPath });

    connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this, process, outPath, tempDir](int exitCode, QProcess::ExitStatus status) {
        GraphCodegenResult result;
        result.exitCode = exitCode;
        result.stdOut = QString::fromUtf8(process->readAllStandardOutput());
        result.stdErr = trimmedStderr(process->readAllStandardError());
        if (status == QProcess::CrashExit) {
            result.unavailableReason = QStringLiteral("codegen process crashed.");
        } else if (exitCode == 0 && QFileInfo::exists(outPath)) {
            QFile outFile(outPath);
            if (outFile.open(QIODevice::ReadOnly)) {
                result.source = QString::fromUtf8(outFile.readAll());
                result.ok = true;
            } else {
                result.unavailableReason = QStringLiteral("could not read generated output.");
            }
        }
        process->deleteLater();
        delete tempDir;
        Q_EMIT finished(result);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, tempDir](QProcess::ProcessError error) {
        if (error == QProcess::Crashed) return; // handled by finished()
        if (process->state() != QProcess::NotRunning) return;
        GraphCodegenResult result;
        result.unavailableReason = QStringLiteral("could not start codegen process (%1).").arg(process->errorString());
        process->deleteLater();
        delete tempDir;
        Q_EMIT finished(result);
    });

    process->start();
}

} // namespace Hypr
