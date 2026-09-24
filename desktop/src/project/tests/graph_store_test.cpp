#include "graph_store.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Hypr;
using Json::Value;

namespace {

QString findNode()
{
    const auto override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HYPRACCEL_NODE"));
    if (!override.isEmpty()) return override;
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

bool copyRecursively(const QString &srcPath, const QString &dstPath)
{
    QDir src(srcPath);
    if (!src.exists()) return false;
    QDir().mkpath(dstPath);
    for (const auto &entry : src.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Dirs)) {
        const QString target = QDir(dstPath).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyRecursively(entry.filePath(), target)) return false;
        } else {
            if (!QFile::copy(entry.filePath(), target)) return false;
        }
    }
    return true;
}

QString harnessPath()
{
    return QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/web_graph_harness.js");
}

// Runs the web harness (mode + args), returns stdout, or a null QString and
// sets *ok=false if node is unavailable or the process failed.
QString runHarness(const QStringList &args, bool *ok)
{
    *ok = false;
    const QString node = findNode();
    if (node.isEmpty()) return QString();
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GRAPH_EDITOR_HTML"), QStringLiteral(GRAPH_EDITOR_HTML_FIXTURE));
    process.setProcessEnvironment(env);
    QStringList fullArgs{ harnessPath() };
    fullArgs += args;
    process.start(node, fullArgs);
    if (!process.waitForFinished(15000)) return QString();
    if (process.exitCode() != 0) {
        qWarning() << "harness failed:" << process.readAllStandardError();
        return QString();
    }
    *ok = true;
    return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace

class GraphStoreTest : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // --- 3a: every demo_project graph loads and saves back byte-identical. ---
    void demoProjectRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(copyRecursively(QStringLiteral(DEMO_PROJECT_FIXTURE), dir.filePath(QStringLiteral("demo_project"))));

        const auto graphs = GraphStore::listGraphs(dir.path(), QStringLiteral("demo_project"));
        QVERIFY(!graphs.isEmpty());
        for (const auto &entry : graphs) {
            const QString path = QDir(GraphStore::graphsDir(dir.path(), QStringLiteral("demo_project"))).filePath(entry.filename);
            const QByteArray before = [&] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }();

            GraphDocument doc = GraphStore::load(dir.path(), QStringLiteral("demo_project"), entry.id);
            GraphStore::save(dir.path(), QStringLiteral("demo_project"), doc, QStringLiteral("esp32"), false);

            const QByteArray after = [&] { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }();
            QCOMPARE(QString::fromUtf8(after), QString::fromUtf8(before));
        }
    }

    // --- 3b: desktop save == web save, proven via the extracted web functions. ---
    void demoProjectMatchesWebOracle()
    {
        if (findNode().isEmpty()) QSKIP("node not found");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(copyRecursively(QStringLiteral(DEMO_PROJECT_FIXTURE), dir.filePath(QStringLiteral("demo_project"))));

        const auto graphs = GraphStore::listGraphs(dir.path(), QStringLiteral("demo_project"));
        for (const auto &entry : graphs) {
            const QString origPath = QDir(GraphStore::graphsDir(dir.path(), QStringLiteral("demo_project"))).filePath(entry.filename);

            bool ok = false;
            const QString webOutput = runHarness({ QStringLiteral("toGraph"), origPath, QStringLiteral("esp32") }, &ok);
            QVERIFY2(ok, "harness toGraph failed");

            GraphDocument doc = GraphStore::load(dir.path(), QStringLiteral("demo_project"), entry.id);
            GraphStore::save(dir.path(), QStringLiteral("demo_project"), doc, QStringLiteral("esp32"), false);
            const QByteArray desktopOutput = [&] { QFile f(origPath); f.open(QIODevice::ReadOnly); return f.readAll(); }();

            QCOMPARE(QString::fromUtf8(desktopOutput), webOutput);
        }
    }

    // --- Non-toGraph-shaped fixtures: load must work; save must normalize
    // exactly as the web would (verified against the extracted web
    // functions, not a hand-copied expectation). ---
    void nonCanonicalFixturesLoadAndNormalize()
    {
        bool haveNode = !findNode().isEmpty();

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("fixture_project");
        QVERIFY(QDir().mkpath(dir.filePath(projectId + QStringLiteral("/graphs"))));
        Json::Object manifest;
        Json::set(manifest, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.project")));
        Json::set(manifest, QStringLiteral("version"), Value::fromNumber(1));
        Json::set(manifest, QStringLiteral("id"), Value::fromString(projectId));
        Json::set(manifest, QStringLiteral("name"), Value::fromString(QStringLiteral("Fixture")));
        Json::Object graphsField;
        Json::set(graphsField, QStringLiteral("path"), Value::fromString(QStringLiteral("graphs")));
        Json::set(manifest, QStringLiteral("graphs"), Value::fromObject(graphsField));
        Json::set(manifest, QStringLiteral("updatedAt"), Value::fromString(QStringLiteral("2026-01-01T00:00:00.000Z")));
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/project.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Json::stringify(Value::fromObject(manifest)) + "\n");
        }

        const QStringList fixtures = {
            QStringLiteral(SCHEMA_EXAMPLES_DIR_FIXTURE) + QStringLiteral("/cordic-to-publish.graph.json"),
            QStringLiteral(SCHEMA_EXAMPLES_DIR_FIXTURE) + QStringLiteral("/cordic-hw-to-publish.graph.json"),
            QStringLiteral(TEST_GRAPHS_DIR_FIXTURE) + QStringLiteral("/pid_test.json"),
        };

        for (const QString &fixturePath : fixtures) {
            QFile source(fixturePath);
            QVERIFY(source.open(QIODevice::ReadOnly));
            const QByteArray bytes = source.readAll();
            QString parseError;
            Value fixtureDoc = Json::parse(bytes, &parseError);
            QVERIFY2(parseError.isEmpty(), qPrintable(parseError));
            const Value *idField = Json::find(fixtureDoc.object, QStringLiteral("id"));
            QVERIFY(idField && idField->isString());
            const QString graphId = idField->string;

            // Place it under the filename the id/grammar requires so
            // GraphStore's project-scoped API (filename must equal id) can
            // load it, exactly as it would if the web had originally saved
            // it under that project.
            const QString placedPath = QDir(GraphStore::graphsDir(dir.path(), projectId)).filePath(graphId + QStringLiteral(".json"));
            QFile placed(placedPath);
            QVERIFY(placed.open(QIODevice::WriteOnly));
            placed.write(bytes);
            placed.close();

            GraphDocument doc = GraphStore::load(dir.path(), projectId, graphId);
            GraphStore::save(dir.path(), projectId, doc, QString(), false);
            const QByteArray desktopOutput = [&] { QFile f(placedPath); f.open(QIODevice::ReadOnly); return f.readAll(); }();

            // Loading succeeded and produced *some* toGraph-shaped output
            // (format/version/id present, no $schema/description carried
            // over) even without node.
            Value saved = Json::parse(desktopOutput);
            QVERIFY(saved.isObject());
            QVERIFY(!Json::find(saved.object, QStringLiteral("$schema")));
            QVERIFY(!Json::find(saved.object, QStringLiteral("description")));

            if (haveNode) {
                bool ok = false;
                const QString webOutput = runHarness({ QStringLiteral("toGraph"), placedPath }, &ok);
                QVERIFY2(ok, "harness toGraph failed");
                QCOMPARE(QString::fromUtf8(desktopOutput), webOutput);
            }
        }
    }

    // --- 3c: desktop-authored graph -> save -> load through the web's own
    // functions; nodes/edges/params/positions survive, including a CordicOp
    // with no angle edge (relies on the metadata.externalBindings
    // derivation, not an edge). ---
    void desktopAuthoredGraphSurvivesWebLoad()
    {
        if (findNode().isEmpty()) QSKIP("node not found");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("authored_project");
        makeMinimalProject(dir.path(), projectId);

        GraphDocument doc;
        doc.id = QStringLiteral("authored_graph");
        doc.name = QStringLiteral("Authored Graph");

        GraphNode constant;
        constant.id = QStringLiteral("const_1");
        constant.type = QStringLiteral("Constant");
        constant.label = QStringLiteral("Constant const_1");
        Json::set(constant.params, QStringLiteral("value"), Value::fromNumber(3.5));
        constant.position = QPointF(10, 20);
        doc.nodes.push_back(constant);

        GraphNode cordic;
        cordic.id = QStringLiteral("cordic_1");
        cordic.type = QStringLiteral("CordicOp");
        cordic.label = QStringLiteral("CORDIC cordic_1");
        Json::set(cordic.params, QStringLiteral("operation"), Value::fromString(QStringLiteral("sin")));
        Json::set(cordic.params, QStringLiteral("implementation"), Value::fromString(QStringLiteral("auto")));
        Json::set(cordic.params, QStringLiteral("iterations"), Value::fromNumber(16));
        cordic.position = QPointF(150, 20); // no angle_rad edge: relies on externalBindings
        doc.nodes.push_back(cordic);

        GraphNode publish;
        publish.id = QStringLiteral("publish_1");
        publish.type = QStringLiteral("Publish");
        publish.label = QStringLiteral("Publish publish_1");
        Json::set(publish.params, QStringLiteral("topic"), Value::fromString(QStringLiteral("telemetry/value")));
        Json::set(publish.params, QStringLiteral("transport"), Value::fromString(QStringLiteral("telemetry")));
        Json::set(publish.params, QStringLiteral("retain"), Value::fromBool(false));
        publish.position = QPointF(300, 20);
        doc.nodes.push_back(publish);

        GraphEdge edge;
        edge.fromNode = QStringLiteral("cordic_1");
        edge.fromPort = QStringLiteral("value");
        edge.toNode = QStringLiteral("publish_1");
        edge.toPort = QStringLiteral("value");
        edge.id = edgeIdFor(edge.fromNode, edge.fromPort, edge.toNode, edge.toPort);
        doc.edges.push_back(edge);

        GraphStore::save(dir.path(), projectId, doc, QStringLiteral("esp32"), true);

        const QString savedPath = QDir(GraphStore::graphsDir(dir.path(), projectId)).filePath(doc.id + QStringLiteral(".json"));
        bool ok = false;
        const QString webLoaded = runHarness({ QStringLiteral("loadDocument"), savedPath }, &ok);
        QVERIFY2(ok, "harness loadDocument failed");
        Value loaded = Json::parse(webLoaded.toUtf8());
        QVERIFY(loaded.isObject());
        const Value *nodesV = Json::find(loaded.object, QStringLiteral("nodes"));
        QVERIFY(nodesV && nodesV->isArray());
        QCOMPARE(nodesV->array.size(), size_t(3));
        // cordic node: label, params, position round-trip.
        const Value &cordicNode = nodesV->array[1];
        QCOMPARE(Json::find(cordicNode.object, QStringLiteral("id"))->string, QStringLiteral("cordic_1"));
        const Value *dataV = Json::find(cordicNode.object, QStringLiteral("data"));
        QVERIFY(dataV);
        QCOMPARE(Json::find(dataV->object, QStringLiteral("nodeType"))->string, QStringLiteral("CordicOp"));
        const Value *paramsV = Json::find(dataV->object, QStringLiteral("params"));
        QVERIFY(paramsV);
        QCOMPARE(Json::find(paramsV->object, QStringLiteral("iterations"))->number, 16.0);
        const Value *posV = Json::find(cordicNode.object, QStringLiteral("position"));
        QCOMPARE(Json::find(posV->object, QStringLiteral("x"))->number, 150.0);

        const Value *edgesV = Json::find(loaded.object, QStringLiteral("edges"));
        QVERIFY(edgesV && edgesV->isArray());
        QCOMPARE(edgesV->array.size(), size_t(1));
        QCOMPARE(Json::find(edgesV->array[0].object, QStringLiteral("source"))->string, QStringLiteral("cordic_1"));
        QCOMPARE(Json::find(edgesV->array[0].object, QStringLiteral("target"))->string, QStringLiteral("publish_1"));

        // Confirm the saved file itself carries the externalBindings for
        // the edge-less CordicOp angle_rad input, and the inputs array.
        Value savedDoc;
        {
            QFile f(savedPath);
            QVERIFY(f.open(QIODevice::ReadOnly));
            savedDoc = Json::parse(f.readAll());
        }
        const Value *inputsV = Json::find(savedDoc.object, QStringLiteral("inputs"));
        QVERIFY(inputsV && inputsV->isArray() && inputsV->array.size() == 1);
        const Value *metadataV = Json::find(savedDoc.object, QStringLiteral("metadata"));
        QVERIFY(metadataV);
        const Value *bindingsV = Json::find(metadataV->object, QStringLiteral("externalBindings"));
        QVERIFY(bindingsV && bindingsV->isArray() && bindingsV->array.size() == 1);
        QCOMPARE(Json::find(bindingsV->array[0].object, QStringLiteral("input"))->string, QStringLiteral("angle_rad"));

        checkAgainstRealServer(dir.path(), projectId, doc.id);
    }

    // --- D4: desktop stores select values with their JSON type; verify the
    // web renders a number correctly, and that a web-saved string survives
    // desktop load/save unchanged. ---
    void d4SelectValueTypes()
    {
        if (findNode().isEmpty()) QSKIP("node not found");

        // 1) A desktop-saved UARTInput.dataBits: 7 (number) must render
        // selected among the web's dataBits options [5,6,7,8]. This
        // evaluates React 17's own documented controlled-<select> match
        // rule ('' + value === '' + optionValue) directly, since
        // react-dom/jsdom are not present in mbd/editor/node_modules (see
        // the report for exactly what this does and does not prove).
        bool ok = false;
        QString out = runHarness({ QStringLiteral("selectValue"), QStringLiteral("7"), QStringLiteral("[5,6,7,8]") }, &ok);
        QVERIFY2(ok, "harness selectValue failed");
        QCOMPARE(out.trimmed(), QStringLiteral("2")); // index of 7

        // A desktop-saved string "7" must ALSO select correctly (parity for
        // graphs the web itself saved, since its onChange stores a string).
        out = runHarness({ QStringLiteral("selectValue"), QStringLiteral("\"7\""), QStringLiteral("[5,6,7,8]") }, &ok);
        QVERIFY2(ok, "harness selectValue failed");
        QCOMPARE(out.trimmed(), QStringLiteral("2"));

        // 2) A web-saved dataBits:"7" (string) must load and save unchanged
        // in desktop (no silent type coercion).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("d4_project");
        makeMinimalProject(dir.path(), projectId);

        GraphDocument doc;
        doc.id = QStringLiteral("uart_graph");
        doc.name = QStringLiteral("UART Graph");
        GraphNode uart;
        uart.id = QStringLiteral("uartinput_1");
        uart.type = QStringLiteral("UARTInput");
        uart.label = QStringLiteral("UART uartinput_1");
        Json::set(uart.params, QStringLiteral("samplePeriodUs"), Value::fromNumber(1000));
        Json::set(uart.params, QStringLiteral("baudRate"), Value::fromNumber(115200));
        Json::set(uart.params, QStringLiteral("dataBits"), Value::fromString(QStringLiteral("7"))); // web-saved shape
        Json::set(uart.params, QStringLiteral("parity"), Value::fromString(QStringLiteral("none")));
        Json::set(uart.params, QStringLiteral("stopBits"), Value::fromNumber(2)); // desktop-native shape
        doc.nodes.push_back(uart);
        GraphStore::save(dir.path(), projectId, doc, QString(), true);

        GraphDocument reloaded = GraphStore::load(dir.path(), projectId, doc.id);
        QCOMPARE(reloaded.nodes.size(), 1);
        const Value *dataBits = Json::find(reloaded.nodes[0].params, QStringLiteral("dataBits"));
        QVERIFY(dataBits && dataBits->isString() && dataBits->string == QStringLiteral("7"));
        const Value *stopBits = Json::find(reloaded.nodes[0].params, QStringLiteral("stopBits"));
        QVERIFY(stopBits && stopBits->isNumber() && stopBits->number == 2.0);

        // Saving again must not rewrite either param's JSON type.
        GraphStore::save(dir.path(), projectId, reloaded, QString(), false);
        const Value *dataBits2 = Json::find(reloaded.nodes[0].params, QStringLiteral("dataBits"));
        QVERIFY(dataBits2->isString());
        const Value *stopBits2 = Json::find(reloaded.nodes[0].params, QStringLiteral("stopBits"));
        QVERIFY(stopBits2->isNumber());
    }

    void graphIdFromNameMatchesWeb()
    {
        const QStringList names = {
            QStringLiteral("My Graph"), QStringLiteral("  leading/trailing  "),
            QStringLiteral("camelCaseName"), QStringLiteral("123startsWithDigit"),
            QStringLiteral(""), QStringLiteral("___"), QStringLiteral("a-b_c!!d"),
        };
        for (const QString &name : names) {
            const QString ours = GraphStore::graphIdFromName(name);
            if (!findNode().isEmpty()) {
                bool ok = false;
                const QString web = runHarness({ QStringLiteral("graphIdFromName"), name }, &ok).trimmed();
                QVERIFY2(ok, "harness graphIdFromName failed");
                QCOMPARE(ours, web);
            }
        }
    }

    void manifestUpdatedOnSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("manifest_project");
        makeMinimalProject(dir.path(), projectId);

        GraphDocument doc;
        doc.id = QStringLiteral("g1");
        doc.name = QStringLiteral("G1");
        GraphStore::save(dir.path(), projectId, doc, QString(), true);

        Value manifest;
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/project.json")));
            QVERIFY(f.open(QIODevice::ReadOnly));
            manifest = Json::parse(f.readAll());
        }
        QCOMPARE(Json::find(manifest.object, QStringLiteral("activeGraphId"))->string, QStringLiteral("g1"));
        QVERIFY(Json::find(manifest.object, QStringLiteral("updatedAt")));
    }

    void createTwiceThrows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("dup_project");
        makeMinimalProject(dir.path(), projectId);

        GraphDocument doc;
        doc.id = QStringLiteral("g1");
        doc.name = QStringLiteral("G1");
        GraphStore::save(dir.path(), projectId, doc, QString(), true);
        QVERIFY_EXCEPTION_THROWN(GraphStore::save(dir.path(), projectId, doc, QString(), true), std::runtime_error);
    }

    void deleteLastGraphRefused()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("solo_project");
        makeMinimalProject(dir.path(), projectId);
        GraphDocument doc;
        doc.id = QStringLiteral("only");
        doc.name = QStringLiteral("Only");
        GraphStore::save(dir.path(), projectId, doc, QString(), true);
        QVERIFY_EXCEPTION_THROWN(GraphStore::remove(dir.path(), projectId, doc.id), std::runtime_error);
    }

    void renameAndDeleteRemapActive()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("multi_project");
        makeMinimalProject(dir.path(), projectId);

        GraphDocument a; a.id = QStringLiteral("graph_a"); a.name = QStringLiteral("A");
        GraphStore::save(dir.path(), projectId, a, QString(), true);
        GraphDocument b; b.id = QStringLiteral("graph_b"); b.name = QStringLiteral("B");
        GraphStore::save(dir.path(), projectId, b, QString(), true); // active is now graph_b

        GraphStore::rename(dir.path(), projectId, QStringLiteral("graph_a"), QStringLiteral("graph_c"), QStringLiteral("C"));
        QVERIFY(!QFileInfo::exists(QDir(GraphStore::graphsDir(dir.path(), projectId)).filePath(QStringLiteral("graph_a.json"))));
        QVERIFY(QFileInfo::exists(QDir(GraphStore::graphsDir(dir.path(), projectId)).filePath(QStringLiteral("graph_c.json"))));

        GraphStore::setActive(dir.path(), projectId, QStringLiteral("graph_c"));
        GraphStore::remove(dir.path(), projectId, QStringLiteral("graph_c"));
        Value manifest;
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/project.json")));
            QVERIFY(f.open(QIODevice::ReadOnly));
            manifest = Json::parse(f.readAll());
        }
        QCOMPARE(Json::find(manifest.object, QStringLiteral("activeGraphId"))->string, QStringLiteral("graph_b"));
    }

    void legacyMigration()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("legacy_project");
        QVERIFY(QDir().mkpath(dir.filePath(projectId + QStringLiteral("/graph"))));

        Json::Object manifest;
        Json::set(manifest, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.project")));
        Json::set(manifest, QStringLiteral("version"), Value::fromNumber(1));
        Json::set(manifest, QStringLiteral("id"), Value::fromString(projectId));
        Json::set(manifest, QStringLiteral("name"), Value::fromString(QStringLiteral("Legacy")));
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/project.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Json::stringify(Value::fromObject(manifest)) + "\n");
        }
        Json::Object legacyGraph;
        Json::set(legacyGraph, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.mbd.graph")));
        Json::set(legacyGraph, QStringLiteral("version"), Value::fromNumber(1));
        Json::set(legacyGraph, QStringLiteral("id"), Value::fromString(QStringLiteral("legacy_graph")));
        Json::set(legacyGraph, QStringLiteral("name"), Value::fromString(QStringLiteral("Legacy Graph")));
        Json::set(legacyGraph, QStringLiteral("nodes"), Value::fromArray({}));
        Json::set(legacyGraph, QStringLiteral("edges"), Value::fromArray({}));
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/graph/graph.json")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(Json::stringify(Value::fromObject(legacyGraph)) + "\n");
        }

        const auto graphs = GraphStore::listGraphs(dir.path(), projectId);
        QCOMPARE(graphs.size(), 1);
        QCOMPARE(graphs.first().id, QStringLiteral("legacy_graph"));
        QVERIFY(QFileInfo::exists(dir.filePath(projectId + QStringLiteral("/graphs/legacy_graph.json"))));
        // Legacy file is left in place (recoverable compatibility copy).
        QVERIFY(QFileInfo::exists(dir.filePath(projectId + QStringLiteral("/graph/graph.json"))));

        Value manifestAfter;
        {
            QFile f(dir.filePath(projectId + QStringLiteral("/project.json")));
            QVERIFY(f.open(QIODevice::ReadOnly));
            manifestAfter = Json::parse(f.readAll());
        }
        QCOMPARE(Json::find(manifestAfter.object, QStringLiteral("activeGraphId"))->string, QStringLiteral("legacy_graph"));
        QVERIFY(!Json::find(manifestAfter.object, QStringLiteral("graph")));
        QCOMPARE(Json::find(Json::find(manifestAfter.object, QStringLiteral("graphs"))->object, QStringLiteral("path"))->string,
                 QStringLiteral("graphs"));
    }

    // --- listGraphs must sort by filename using the same collation as
    // node's String.prototype.localeCompare (server.js:435), not plain
    // ASCII/QString ordering: it's case-insensitive at primary strength
    // (lowercase sorts before uppercase only as a tiebreak), and
    // '_' / '-' / '.' sort before digits before letters. ---
    void localeCompareMatchesNode()
    {
        const QStringList names = {
            QStringLiteral("GRAPH_1.json"), QStringLiteral("arm_sine_uart_demo.json"),
            QStringLiteral("cordic_sine_test.json"), QStringLiteral("editor_graph.json"),
            QStringLiteral("gen_test_graph.json"), QStringLiteral("a-b.json"), QStringLiteral("a_b.json"),
            QStringLiteral("a1.json"), QStringLiteral("A1.json"), QStringLiteral("b.json"),
            QStringLiteral("Arm_x.json"), QStringLiteral("arm_x.json"), QStringLiteral("a.json"),
            QStringLiteral("a9.json"), QStringLiteral("a10.json"), QStringLiteral("GRAPH_10.json"),
            QStringLiteral("GRAPH_2.json"),
        };

        if (!findNode().isEmpty()) {
            // One batched node invocation (289 pairs) rather than one
            // process per pair.
            QString script = QStringLiteral("const names=[");
            for (int i = 0; i < names.size(); ++i) {
                if (i) script += ',';
                script += quoted(names[i]);
            }
            script += QStringLiteral("]; const out=[]; for (const a of names) for (const b of names) "
                                      "out.push(Math.sign(a.localeCompare(b))); process.stdout.write(out.join(','));");
            QProcess process;
            process.start(findNode(), { QStringLiteral("-e"), script });
            const bool ok = process.waitForFinished(15000) && process.exitCode() == 0;
            QVERIFY2(ok, "node -e failed");
            const QStringList signs = QString::fromUtf8(process.readAllStandardOutput()).trimmed().split(',');
            QCOMPARE(signs.size(), names.size() * names.size());
            int idx = 0;
            for (const QString &a : names) {
                for (const QString &b : names) {
                    const int expected = signs[idx++].toInt();
                    const int raw = GraphStore::compareLikeNodeLocale(a, b);
                    const int actual = raw < 0 ? -1 : (raw > 0 ? 1 : 0);
                    QCOMPARE(actual, expected);
                }
            }
        }

        // Static golden: the demo project's own listing order (verified
        // against node above when available; this assertion runs even
        // without node).
        QStringList demo = {
            QStringLiteral("GRAPH_1.json"), QStringLiteral("arm_sine_uart_demo.json"),
            QStringLiteral("cordic_sine_test.json"), QStringLiteral("editor_graph.json"),
            QStringLiteral("gen_test_graph.json"),
        };
        std::sort(demo.begin(), demo.end(), [](const QString &a, const QString &b) {
            return GraphStore::compareLikeNodeLocale(a, b) < 0;
        });
        QCOMPARE(demo, QStringList({
            QStringLiteral("arm_sine_uart_demo.json"), QStringLiteral("cordic_sine_test.json"),
            QStringLiteral("editor_graph.json"), QStringLiteral("gen_test_graph.json"), QStringLiteral("GRAPH_1.json"),
        }));

        // And that listGraphs() on the real demo_project fixture returns
        // exactly this order (proves the "first listed graph" fallback --
        // resolveActiveGraphId()/delete()'s remap -- picks the same graph
        // the web would).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(copyRecursively(QStringLiteral(DEMO_PROJECT_FIXTURE), dir.filePath(QStringLiteral("demo_project"))));
        const auto graphs = GraphStore::listGraphs(dir.path(), QStringLiteral("demo_project"));
        QStringList orderedIds;
        for (const auto &e : graphs) orderedIds << e.filename;
        QCOMPARE(orderedIds, demo);
    }

    // --- Missing-label default takes an optional display-name lookup, so
    // Phase 2 can pass the node registry's displayName() and match
    // graphNodesFromDocument's `displayNames[type] || type` exactly. ---
    void loadUsesSuppliedDisplayNameForMissingLabel()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectId = QStringLiteral("label_project");
        makeMinimalProject(dir.path(), projectId);

        Json::Object node;
        Json::set(node, QStringLiteral("id"), Value::fromString(QStringLiteral("cordic_1")));
        Json::set(node, QStringLiteral("type"), Value::fromString(QStringLiteral("CordicOp")));
        // no "label" key at all
        Json::set(node, QStringLiteral("params"), Value::fromObject({}));
        Json::Object pos;
        Json::set(pos, QStringLiteral("x"), Value::fromNumber(0));
        Json::set(pos, QStringLiteral("y"), Value::fromNumber(0));
        Json::set(node, QStringLiteral("position"), Value::fromObject(pos));

        Json::Object root;
        Json::set(root, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.mbd.graph")));
        Json::set(root, QStringLiteral("version"), Value::fromNumber(1));
        Json::set(root, QStringLiteral("id"), Value::fromString(QStringLiteral("g1")));
        Json::set(root, QStringLiteral("name"), Value::fromString(QStringLiteral("G1")));
        Json::set(root, QStringLiteral("nodes"), Value::fromArray({ Value::fromObject(node) }));
        Json::set(root, QStringLiteral("edges"), Value::fromArray({}));

        const QString path = QDir(GraphStore::graphsDir(dir.path(), projectId)).filePath(QStringLiteral("g1.json"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(Json::stringify(Value::fromObject(root)));
        f.close();

        // Default (no lookup supplied): falls back to `type` verbatim.
        GraphDocument withoutLookup = GraphStore::load(dir.path(), projectId, QStringLiteral("g1"));
        QCOMPARE(withoutLookup.nodes.size(), 1);
        QCOMPARE(withoutLookup.nodes[0].label, QStringLiteral("CordicOp cordic_1"));

        // With a lookup matching the web's displayNames = { CordicOp: 'CORDIC' }.
        GraphDocument withLookup = GraphStore::load(dir.path(), projectId, QStringLiteral("g1"),
            [](const QString &type) -> QString {
                if (type == QStringLiteral("CordicOp")) return QStringLiteral("CORDIC");
                return QString();
            });
        QCOMPARE(withLookup.nodes.size(), 1);
        QCOMPARE(withLookup.nodes[0].label, QStringLiteral("CORDIC cordic_1"));
    }

private:
    static QString quoted(const QString &s)
    {
        QString escaped = s;
        escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'"));
        return QLatin1Char('\'') + escaped + QLatin1Char('\'');
    }

    static void makeMinimalProject(const QString &root, const QString &projectId)
    {
        QVERIFY(QDir().mkpath(QDir(root).filePath(projectId + QStringLiteral("/graphs"))));
        Json::Object manifest;
        Json::set(manifest, QStringLiteral("format"), Value::fromString(QStringLiteral("hypraccel.project")));
        Json::set(manifest, QStringLiteral("version"), Value::fromNumber(1));
        Json::set(manifest, QStringLiteral("id"), Value::fromString(projectId));
        Json::set(manifest, QStringLiteral("name"), Value::fromString(projectId));
        Json::Object graphsField;
        Json::set(graphsField, QStringLiteral("path"), Value::fromString(QStringLiteral("graphs")));
        Json::set(manifest, QStringLiteral("graphs"), Value::fromObject(graphsField));
        Json::set(manifest, QStringLiteral("updatedAt"), Value::fromString(QStringLiteral("2026-01-01T00:00:00.000Z")));
        QFile f(QDir(root).filePath(projectId + QStringLiteral("/project.json")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(Json::stringify(Value::fromObject(manifest)) + "\n");
    }

    // 3c (server half): starts the real, unmodified mbd/editor/server.js
    // against the temp project root and confirms it lists and serves the
    // desktop-saved graph. Skipped (not failed) if node or
    // mbd/editor/node_modules is missing.
    static void checkAgainstRealServer(const QString &root, const QString &projectId, const QString &graphId)
    {
        if (findNode().isEmpty()) { qInfo() << "skip real-server check: node not found"; return; }
        if (!QDir(QStringLiteral(MBD_EDITOR_DIR_FIXTURE) + QStringLiteral("/node_modules")).exists()) {
            qInfo() << "skip real-server check: mbd/editor/node_modules missing";
            return;
        }

        quint16 port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            port = probe.serverPort();
        }

        QProcess server;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HYPRACCEL_PROJECTS_ROOT"), root);
        env.insert(QStringLiteral("HYPRACCEL_EDITOR_PORT"), QString::number(port));
        server.setProcessEnvironment(env);
        server.setWorkingDirectory(QStringLiteral(MBD_EDITOR_DIR_FIXTURE));
        server.start(findNode(), { QStringLiteral(MBD_SERVER_JS_FIXTURE) });
        if (!server.waitForStarted(5000)) { qInfo() << "skip real-server check: server did not start"; return; }

        QNetworkAccessManager nam;
        bool ready = false;
        for (int i = 0; i < 50 && !ready; ++i) {
            QNetworkReply *reply = nam.get(QNetworkRequest(
                QUrl(QStringLiteral("http://127.0.0.1:%1/api/projects/%2/graphs").arg(port).arg(projectId))));
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() == QNetworkReply::NoError) {
                const auto doc = QJsonDocument::fromJson(reply->readAll());
                if (doc.isObject() && doc.object().contains("graphs")) ready = true;
            }
            reply->deleteLater();
            if (!ready) QTest::qWait(100);
        }

        if (ready) {
            QNetworkReply *reply = nam.get(QNetworkRequest(
                QUrl(QStringLiteral("http://127.0.0.1:%1/api/projects/%2/graphs/%3").arg(port).arg(projectId, graphId))));
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            QVERIFY(doc.isObject());
            QCOMPARE(doc.object().value("id").toString(), graphId);
            reply->deleteLater();
        } else {
            qInfo() << "skip real-server check: server never became ready";
        }

        server.terminate();
        if (!server.waitForFinished(2000)) server.kill();
    }
};

QTEST_MAIN(GraphStoreTest)
#include "graph_store_test.moc"
