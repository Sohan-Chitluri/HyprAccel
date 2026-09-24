// ctest `node_types_drift`: runs extract_node_types.js --check against the
// committed node_types.json (fails as soon as node_types.json is out of
// date with the frozen web source), and separately proves the check
// actually bites by running the node-side self-test against deliberately
// mutated fixtures. See desktop/docs/mbd_graph_contract.md §6 D1.

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTest>

namespace {

QString findNodeExecutable()
{
    const auto override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HYPRACCEL_NODE"));
    if (!override.isEmpty()) return override;
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

} // namespace

class NodeTypesDriftTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void driftCheckPasses()
    {
        const QString node = findNodeExecutable();
        if (node.isEmpty()) QSKIP("node executable not found; set HYPRACCEL_NODE to run the node_types.json drift check.");

        QProcess process;
        process.start(node, {
            QStringLiteral(EXTRACT_SCRIPT_PATH), QStringLiteral("--check"),
            QStringLiteral("--html"), QStringLiteral(GRAPH_EDITOR_HTML_PATH),
            QStringLiteral("--codegen"), QStringLiteral(GRAPH_TO_C_JS_PATH),
            QStringLiteral("--out"), QStringLiteral(NODE_TYPES_JSON_PATH),
        });
        QVERIFY(process.waitForFinished(15000));
        const QByteArray stderrOut = process.readAllStandardError();
        const QByteArray stdoutOut = process.readAllStandardOutput();
        QVERIFY2(process.exitCode() == 0,
                  ("node_types.json is out of date with the web source:\n" + stderrOut + stdoutOut).constData());
    }

    void selfTestBites()
    {
        const QString node = findNodeExecutable();
        if (node.isEmpty()) QSKIP("node executable not found; set HYPRACCEL_NODE to run the drift self-test.");

        QProcess process;
        process.start(node, {
            QStringLiteral(SELFTEST_SCRIPT_PATH),
            QStringLiteral(EXTRACT_SCRIPT_PATH),
            QStringLiteral(GRAPH_TO_C_JS_PATH),
            QStringLiteral(NODE_TYPES_JSON_PATH),
        });
        QVERIFY(process.waitForFinished(15000));
        const QByteArray stderrOut = process.readAllStandardError();
        const QByteArray stdoutOut = process.readAllStandardOutput();
        QVERIFY2(process.exitCode() == 0, ("drift self-test failed to prove the check bites:\n" + stderrOut + stdoutOut).constData());
    }
};

QTEST_MAIN(NodeTypesDriftTest)
#include "node_types_drift_test.moc"
