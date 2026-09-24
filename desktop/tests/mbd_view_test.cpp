// Phase 2 integration test for the MBD tab (desktop/src/mbd_view.*):
//  1. A graph built on desktop (palette drop + canvas model + inspector-path
//     params) is saved and generated; the C must equal, byte for byte,
//     (a) graph_to_c.js run directly on the saved file, and
//     (b) the real web server's POST /api/build for the same project graph.
//     The saved file must also be a fixed point of the web's own save path.
//  2. Web-authored graphs (the demo project, plus one created through the real
//     server's POST /api/projects/:id/graphs) open in desktop exactly as the
//     web editor loads them, render every connected port, and save back
//     byte-identical.
//  3. For every demo graph, desktop Generate and server /api/build agree:
//     same C when both succeed, same error text when codegen rejects it.
// Oracles are the frozen web code itself (server.js, graph_to_c.js, and
// functions extracted from graph_editor.html by web_graph_harness.js).

#include "graph_canvas.h"
#include "graph_store.h"
#include "mbd_view.h"
#include "node_palette.h"

#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

using namespace Hypr;

namespace {

const QString kRepo = QStringLiteral(HYPRACCEL_REPO_FIXTURE);

QString findNode()
{
    const auto override = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HYPRACCEL_NODE"));
    return override.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("node")) : override;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString target = to + path.mid(from.size());
        if (it.fileInfo().isDir()) { QDir().mkpath(target); continue; }
        if (!QFile::copy(path, target)) return false;
    }
    return true;
}

struct NodeRun { int exitCode = -1; QByteArray out; QByteArray err; };

NodeRun runNode(const QStringList &args)
{
    QProcess process;
    process.start(findNode(), args);
    NodeRun run;
    if (!process.waitForFinished(20000)) return run;
    run.exitCode = process.exitCode();
    run.out = process.readAllStandardOutput();
    run.err = process.readAllStandardError();
    return run;
}

// graph_to_c.js on a copy named graph.json — exactly how server.js invokes it.
NodeRun directCodegen(const QByteArray &graphBytes, QByteArray *source)
{
    QTemporaryDir dir;
    writeFile(dir.filePath("graph.json"), graphBytes);
    NodeRun run = runNode({ kRepo + "/mbd/codegen/graph_to_c.js", dir.filePath("graph.json"), dir.filePath("graph.c") });
    if (source) *source = readFile(dir.filePath("graph.c"));
    return run;
}

// The real (frozen) web editor server on a scratch projects root.
class WebServer {
public:
    bool start(const QString &projectsRoot)
    {
        if (findNode().isEmpty() || !QDir(kRepo + "/mbd/editor/node_modules").exists()) return false;
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) return false;
        port_ = probe.serverPort();
        probe.close();
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("HYPRACCEL_PROJECTS_ROOT", projectsRoot);
        env.insert("HYPRACCEL_EDITOR_PORT", QString::number(port_));
        process_.setProcessEnvironment(env);
        process_.setWorkingDirectory(kRepo + "/mbd/editor");
        process_.start(findNode(), { kRepo + "/mbd/editor/server.js" });
        if (!process_.waitForStarted(5000)) return false;
        for (int i = 0; i < 50; ++i) {           // wait until express answers
            QTest::qWait(100);
            if (request("GET", "/api/boards").status == 200) return true;
        }
        return false;
    }
    ~WebServer()
    {
        if (process_.state() == QProcess::NotRunning) return;
        process_.terminate();
        if (!process_.waitForFinished(2000)) process_.kill();
    }
    struct Reply { int status = 0; QJsonObject body; };
    Reply request(const QByteArray &verb, const QString &path, const QByteArray &json = QByteArray())
    {
        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port_).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = nam_.sendCustomRequest(req, verb, json);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        Reply result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return result;
    }
private:
    QProcess process_;
    QNetworkAccessManager nam_;
    quint16 port_ = 0;
};

QJsonObject readHardware(const QString &projectDir)
{
    return QJsonDocument::fromJson(readFile(projectDir + "/hardware/hardware.json")).object();
}

GraphCodegenResult generateAndWait(MbdView &view)
{
    QSignalSpy spy(&view, &MbdView::generated);
    view.generate();
    if (!spy.wait(20000)) return GraphCodegenResult();
    return view.lastResult();
}

} // namespace

class MbdViewTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        if (findNode().isEmpty()) QSKIP("node not found; set HYPRACCEL_NODE to run the MBD integration test.");
        QVERIFY(root_.isValid());
        projectDir_ = root_.filePath("demo_project");
        QDir(projectDir_).removeRecursively();
        QVERIFY(copyTree(kRepo + "/.hypraccel/projects/demo_project", projectDir_));
        // Artifacts are irrelevant here and may be large.
        QDir(projectDir_ + "/generated").removeRecursively();
        QDir(projectDir_ + "/build").removeRecursively();
        QDir(projectDir_ + "/.pio").removeRecursively();
    }

    // 1. Desktop-authored graph → Generate == graph_to_c.js == server /api/build.
    void desktopGraphGeneratesSameCAsWeb()
    {
        MbdView view;
        view.setHardware(readHardware(projectDir_));
        view.setProject(root_.path(), "demo_project");
        view.newGraph("Desktop Parity");
        QCOMPARE(view.graphId(), QStringLiteral("Desktop_Parity"));

        // A real palette → canvas drop for the first node.
        auto *canvas = view.findChild<GraphCanvas*>("mbdCanvas");
        QVERIFY(canvas);
        view.resize(1400, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        std::unique_ptr<QMimeData> mime(NodePalette::mimeDataForType("Constant"));
        const QPoint dropAt = canvas->viewport()->rect().center();
        QDragEnterEvent enter(dropAt, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &enter);
        QDropEvent drop(QPointF(dropAt), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas->viewport(), &drop);
        GraphModel *model = view.model();
        QCOMPARE(model->document().nodes.size(), 1);
        const QString constant = model->document().nodes.first().id;
        QCOMPARE(constant, QStringLiteral("constant_1"));
        QCOMPARE(model->document().nodes.first().label, QStringLiteral("Constant constant_1"));

        const auto &reg = view.registry();
        auto add = [&](const QString &type, QPointF pos) {
            return model->addNode(type, reg.defaultParams(type), pos, reg.displayName(type));
        };
        const QString gain = add("Gain", { 300, 0 });
        const QString cordic = add("CordicOp", { 300, 200 });   // angle via externalBinding only
        const QString sat = add("Saturation", { 600, 0 });
        const QString pub = add("Publish", { 900, 0 });
        const QString pub2 = add("Publish", { 900, 200 });
        model->setParam(constant, "value", Json::Value::fromNumber(0.25));
        model->setParam(gain, "gain", Json::Value::fromNumber(-2.5));
        model->setParam(pub2, "topic", Json::Value::fromString("telemetry/sine"));
        auto connectPorts = [&](const QString &a, const QString &ap, const QString &b, const QString &bp) {
            const auto check = reg.checkConnection(model->document(), a, ap, b, bp);
            QVERIFY2(check.verdict != ConnectionVerdict::Block, qPrintable(check.reason));
            model->addEdge(a, ap, b, bp);
        };
        connectPorts(constant, "value", gain, "input");
        connectPorts(gain, "value", sat, "input");
        connectPorts(sat, "value", pub, "value");
        connectPorts(cordic, "value", pub2, "value");
        // D5: a second edge into a connected input is refused by the registry.
        QCOMPARE(reg.checkConnection(model->document(), constant, "value", pub, "value").verdict, ConnectionVerdict::Block);

        const GraphCodegenResult desktop = generateAndWait(view);
        QVERIFY2(desktop.ok, qPrintable(desktop.stdErr + desktop.unavailableReason));
        QVERIFY(view.graphPersisted());
        QVERIFY(!model->isDirty());

        const QString savedPath = projectDir_ + "/graphs/Desktop_Parity.json";
        const QByteArray saved = readFile(savedPath);
        QVERIFY(!saved.isEmpty());
        QVERIFY(saved.contains("\"externalBindings\""));
        QVERIFY(saved.contains("\"targetBoard\": \"" + readHardware(projectDir_).value("board").toString().toUtf8() + "\""));

        // (a) graph_to_c.js directly on the saved file.
        QByteArray direct;
        const NodeRun run = directCodegen(saved, &direct);
        QCOMPARE(run.exitCode, 0);
        QCOMPARE(desktop.source.toUtf8(), direct);

        // Web's own save path over the desktop file is a fixed point.
        const NodeRun reSave = runNode({ QStringLiteral(WEB_GRAPH_HARNESS), "toGraph", savedPath,
                                         readHardware(projectDir_).value("board").toString() });
        QCOMPARE(reSave.exitCode, 0);
        QCOMPARE(reSave.out, saved);

        // (b) the real server's /api/build for the same project graph.
        WebServer server;
        if (!server.start(root_.path())) QSKIP("web server unavailable (node or mbd/editor/node_modules missing)");
        const auto reply = server.request("POST", "/api/build?projectId=demo_project&graphId=Desktop_Parity", "{}");
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.body.value("source").toString(), desktop.source);
        // …and the web lists/serves it.
        const auto listed = server.request("GET", "/api/projects/demo_project/graphs");
        bool found = false;
        for (const auto &entry : listed.body.value("graphs").toArray())
            found = found || entry.toObject().value("id").toString() == "Desktop_Parity";
        QVERIFY(found);
    }

    // 2. Web-authored graphs open in the desktop canvas as the web loads them.
    void webGraphsOpenInDesktop()
    {
        // One more graph authored through the real web server API.
        WebServer server;
        const bool serverUp = server.start(root_.path());
        if (serverUp) {
            const QByteArray body = R"({"graphId":"web_made","name":"  Web Made  ","graph":{"format":"hypraccel.mbd.graph","version":1,"id":"web_made","name":"x",
              "nodes":[{"id":"constant_1","type":"Constant","label":"Constant constant_1","params":{"value":2},"position":{"x":10.5,"y":20}},
                       {"id":"cordicop_2","type":"CordicOp","params":{"operation":"sincos","implementation":"hardware","iterations":16},"position":{"x":300,"y":40}},
                       {"id":"uartinput_3","type":"UARTInput","label":"UART in","params":{"samplePeriodUs":1000,"baudRate":115200,"dataBits":"7","parity":"none","stopBits":1},"position":{"x":0,"y":300}},
                       {"id":"publish_4","type":"Publish","label":"Publish publish_4","params":{"topic":"t","transport":"telemetry","retain":false},"position":{"x":600,"y":40}}],
              "edges":[{"id":"e1","from":{"node":"constant_1","port":"value"},"to":{"node":"cordicop_2","port":"angle_rad"}},
                       {"from":{"node":"cordicop_2","port":"cos"},"to":{"node":"publish_4","port":"value"}}],
              "inputs":[],"metadata":{"targetBoard":"esp32"}}})";
            const auto created = server.request("POST", "/api/projects/demo_project/graphs", body);
            QCOMPARE(created.status, 201);
        } else {
            qInfo() << "web server unavailable; checking the demo project's web-authored graphs only";
        }

        MbdView view;
        view.setHardware(readHardware(projectDir_));
        view.setProject(root_.path(), "demo_project");
        auto *canvas = view.findChild<GraphCanvas*>("mbdCanvas");
        QVERIFY(canvas);

        const auto graphs = GraphStore::listGraphs(root_.path(), "demo_project");
        QVERIFY(graphs.size() >= 5);
        for (const auto &entry : graphs) {
            const QString path = projectDir_ + "/graphs/" + entry.filename;
            const QByteArray original = readFile(path);
            view.openGraph(entry.id);
            const GraphDocument &doc = view.model()->document();
            if (entry.id == "arm_sine_uart_demo" && qEnvironmentVariableIsSet("MBD_SCREENSHOT")) {
                view.resize(1600, 1000);
                view.show();
                QTest::qWaitForWindowExposed(&view);
                view.model()->setSelectedNode(doc.nodes.first().id);
                QTest::qWait(50);
                view.grab().save(qEnvironmentVariable("MBD_SCREENSHOT"));
            }

            // Same nodes/edges the web editor builds on load.
            const NodeRun web = runNode({ QStringLiteral(WEB_GRAPH_HARNESS), "loadDocument", path });
            QCOMPARE(web.exitCode, 0);
            QString error;
            const Json::Value loaded = Json::parse(web.out, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            const Json::Array &webNodes = Json::find(loaded.object, "nodes")->array;
            const Json::Array &webEdges = Json::find(loaded.object, "edges")->array;
            QCOMPARE(doc.nodes.size(), int(webNodes.size()));
            QCOMPARE(doc.edges.size(), int(webEdges.size()));
            for (int i = 0; i < doc.nodes.size(); ++i) {
                const auto &node = doc.nodes[i];
                const Json::Object &webNode = webNodes[size_t(i)].object;
                const Json::Object &data = Json::find(webNode, "data")->object;
                const Json::Object &position = Json::find(webNode, "position")->object;
                QCOMPARE(node.id, Json::find(webNode, "id")->string);
                QCOMPARE(node.type, Json::find(data, "nodeType")->string);
                QCOMPARE(node.label, Json::find(data, "label")->string);
                QVERIFY2(Json::Value::fromObject(node.params) == *Json::find(data, "params"), qPrintable(entry.id + "/" + node.id));
                QCOMPARE(node.position.x(), Json::find(position, "x")->number);
                QCOMPARE(node.position.y(), Json::find(position, "y")->number);
            }
            for (int i = 0; i < doc.edges.size(); ++i) {
                const auto &edge = doc.edges[i];
                const Json::Object &webEdge = webEdges[size_t(i)].object;
                QCOMPARE(edge.id, Json::find(webEdge, "id")->string);
                QCOMPARE(edge.fromNode, Json::find(webEdge, "source")->string);
                QCOMPARE(edge.fromPort, Json::find(webEdge, "sourceHandle")->string);
                QCOMPARE(edge.toNode, Json::find(webEdge, "target")->string);
                QCOMPARE(edge.toPort, Json::find(webEdge, "targetHandle")->string);
                // Every connected port is rendered on the canvas.
                QVERIFY2(canvas->nodeHasRenderedPort(edge.fromNode, edge.fromPort, true), qPrintable(entry.id + ": " + edge.id));
                QVERIFY2(canvas->nodeHasRenderedPort(edge.toNode, edge.toPort, false), qPrintable(entry.id + ": " + edge.id));
            }

            // Save back from desktop: identical to what the web would write.
            view.saveGraph();
            const NodeRun webSave = runNode({ QStringLiteral(WEB_GRAPH_HARNESS), "toGraph", path,
                                              readHardware(projectDir_).value("board").toString() });
            QCOMPARE(webSave.exitCode, 0);
            QCOMPARE(readFile(path), webSave.out);
            if (entry.id != "web_made")   // server-written canonical files are already fixed points
                QCOMPARE(readFile(path), original);
        }
        if (serverUp) {
            // D4 in practice: the web-saved string stays a string after a desktop save.
            QVERIFY(readFile(projectDir_ + "/graphs/web_made.json").contains("\"dataBits\": \"7\""));
            // Label-less CordicOp got the web's default label on load.
            QVERIFY(readFile(projectDir_ + "/graphs/web_made.json").contains("\"label\": \"CORDIC cordicop_2\""));
        }
    }

    // 3. Desktop Generate vs server /api/build over every demo graph.
    void generateMatchesServerForDemoGraphs()
    {
        WebServer server;
        if (!server.start(root_.path())) QSKIP("web server unavailable (node or mbd/editor/node_modules missing)");
        MbdView view;
        view.setHardware(readHardware(projectDir_));
        view.setProject(root_.path(), "demo_project");
        int same = 0, sameError = 0;
        for (const auto &entry : GraphStore::listGraphs(root_.path(), "demo_project")) {
            // Ask the server first: desktop's Generate re-saves the file.
            const auto reply = server.request("POST", "/api/build?projectId=demo_project&graphId=" + entry.id, "{}");
            view.openGraph(entry.id);
            const GraphCodegenResult desktop = generateAndWait(view);
            if (reply.status == 200) {
                QVERIFY2(desktop.ok, qPrintable(entry.id + ": " + desktop.stdErr));
                QCOMPARE(desktop.source, reply.body.value("source").toString());
                ++same;
            } else {
                const QString serverError = reply.body.value("error").toString();
                QVERIFY2(!serverError.startsWith("Node '") && !serverError.startsWith("Graph target"),
                         qPrintable(entry.id + " hit the server-only resource check (D3): " + serverError));
                QVERIFY2(!desktop.ok, qPrintable(entry.id));
                QCOMPARE(desktop.stdErr, serverError);
                ++sameError;
            }
        }
        qInfo() << "demo graphs: identical C" << same << "identical codegen error" << sameError;
        QVERIFY(same >= 1);
    }

private:
    QTemporaryDir root_;
    QString projectDir_;
};

QTEST_MAIN(MbdViewTest)
#include "mbd_view_test.moc"
