#include "project_store.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Hypr;

namespace {

ProjectSnapshot makeSnapshot(const QString &id, const QString &name)
{
    ProjectSnapshot snapshot;
    snapshot.id = id;
    snapshot.name = name;
    snapshot.board = QStringLiteral("esp32");
    snapshot.pinAssignments.insert(QStringLiteral("GPIO1"), QStringLiteral("uart.uart0.tx"));
    snapshot.pinAssignments.insert(QStringLiteral("GPIO25"), QStringLiteral("gpio"));
    snapshot.clock.selections.insert(QStringLiteral("cpu_mux"), QStringLiteral("pll"));
    snapshot.clockResult.freqMHz.insert(QStringLiteral("cpu_div"), 240);
    snapshot.clockResult.errors.append(QStringLiteral("demo warning"));
    return snapshot;
}

} // namespace

class ProjectStoreTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void saveLoadRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto snapshot = makeSnapshot(QStringLiteral("proj1"), QStringLiteral("My Project"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        const auto loaded = ProjectStore::load(dir.path(), QStringLiteral("proj1"));
        QCOMPARE(loaded.id, snapshot.id);
        QCOMPARE(loaded.name, snapshot.name);
        QCOMPARE(loaded.board, snapshot.board);
        QCOMPARE(loaded.pinAssignments, snapshot.pinAssignments);
        QCOMPARE(loaded.clock.selections, snapshot.clock.selections);
        QFile clockFile(QDir(dir.path()).filePath(QStringLiteral("proj1/hardware/clock.json")));
        QVERIFY(clockFile.open(QIODevice::ReadOnly));
        const auto clock = QJsonDocument::fromJson(clockFile.readAll()).object();
        QCOMPARE(clock["resolved"].toObject()["cpu_div"].toDouble(), 240.0);
        QCOMPARE(clock["errors"].toArray().size(), 1);
    }

    void newProjectCreatesManifestAndGraphsDir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto snapshot = makeSnapshot(QStringLiteral("proj2"), QStringLiteral("Second"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        const QString projectDir = QDir(dir.path()).filePath(QStringLiteral("proj2"));
        QVERIFY(QFile::exists(QDir(projectDir).filePath(QStringLiteral("project.json"))));
        QVERIFY(QDir(QDir(projectDir).filePath(QStringLiteral("graphs"))).exists());

        QFile manifestFile(QDir(projectDir).filePath(QStringLiteral("project.json")));
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        QCOMPARE(manifest.value("format").toString(), QStringLiteral("hypraccel.project"));
        QCOMPARE(manifest.value("version").toInt(), 1);
        QCOMPARE(manifest.value("id").toString(), QStringLiteral("proj2"));
        QVERIFY(manifest.contains("createdAt"));
        QVERIFY(manifest.contains("updatedAt"));
    }

    void resavePreservesWebEditorHardwareState()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto snapshot = makeSnapshot(QStringLiteral("proj_web"), QStringLiteral("Web"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        // Simulate the web editor adding configuration, a device and a node binding.
        const QString hardwarePath = QDir(dir.path()).filePath(QStringLiteral("proj_web/hardware/hardware.json"));
        QFile in(hardwarePath);
        QVERIFY(in.open(QIODevice::ReadOnly));
        auto hardware = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        auto resources = hardware["resources"].toObject();
        auto uart = resources["uart.uart0"].toObject();
        uart["configuration"] = QJsonObject{{"baud", 115200}};
        resources["uart.uart0"] = uart;
        hardware["resources"] = resources;
        hardware["devices"] = QJsonArray{QJsonObject{{"id", "imu"}, {"name", "IMU"}}};
        auto assignments = hardware["assignments"].toArray();
        for (int i = 0; i < assignments.size(); ++i) {
            auto entry = assignments[i].toObject();
            if (entry["pin"].toString() == "GPIO1") entry["node"] = QStringLiteral("SerialOut[0]");
            assignments[i] = entry;
        }
        hardware["assignments"] = assignments;
        QFile out(hardwarePath);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(hardware).toJson());
        out.close();

        snapshot.pinAssignments.insert(QStringLiteral("GPIO32"), QStringLiteral("pwm"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        QFile reread(hardwarePath);
        QVERIFY(reread.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(reread.readAll()).object();
        QCOMPARE(saved["resources"].toObject()["uart.uart0"].toObject()["configuration"].toObject()["baud"].toInt(), 115200);
        QCOMPARE(saved["devices"].toArray().size(), 1);
        QString node;
        int count = 0;
        for (const auto &value : saved["assignments"].toArray()) {
            ++count;
            if (value.toObject()["pin"].toString() == "GPIO1") node = value.toObject()["node"].toString();
        }
        QCOMPARE(node, QStringLiteral("SerialOut[0]"));
        QCOMPARE(count, 3);

        // Changing board discards board-specific state.
        snapshot.board = QStringLiteral("thejas32");
        snapshot.pinAssignments.clear();
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));
        QFile rereadThejas(hardwarePath);
        QVERIFY(rereadThejas.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(rereadThejas.readAll()).object()["devices"].toArray().size(), 0);
    }

    void resaveKeepsCreatedAtAndDoesNotDeleteGraphs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto snapshot = makeSnapshot(QStringLiteral("proj3"), QStringLiteral("Third"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        const QString projectDir = QDir(dir.path()).filePath(QStringLiteral("proj3"));
        const QString graphsDir = QDir(projectDir).filePath(QStringLiteral("graphs"));
        const QString extraGraph = QDir(graphsDir).filePath(QStringLiteral("foo.json"));
        QFile graphFile(extraGraph);
        QVERIFY(graphFile.open(QIODevice::WriteOnly));
        graphFile.write("{\"format\":\"hypraccel.mbd.graph\"}");
        graphFile.close();

        QFile manifestFile(QDir(projectDir).filePath(QStringLiteral("project.json")));
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const auto firstManifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
        manifestFile.close();
        const auto createdAt = firstManifest.value("createdAt").toString();

        QTest::qSleep(20);
        snapshot.name = QStringLiteral("Third (renamed)");
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        QVERIFY(QFile::exists(extraGraph));

        QFile manifestFile2(QDir(projectDir).filePath(QStringLiteral("project.json")));
        QVERIFY(manifestFile2.open(QIODevice::ReadOnly));
        const auto secondManifest = QJsonDocument::fromJson(manifestFile2.readAll()).object();
        QCOMPARE(secondManifest.value("createdAt").toString(), createdAt);
        QCOMPARE(secondManifest.value("name").toString(), QStringLiteral("Third (renamed)"));

        const auto reloaded = ProjectStore::load(dir.path(), QStringLiteral("proj3"));
        QCOMPARE(reloaded.name, QStringLiteral("Third (renamed)"));
    }

    void invalidIdThrowsAndWritesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto snapshot = makeSnapshot(QStringLiteral("1bad"), QStringLiteral("Bad id"));
        bool threw = false;
        try {
            ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));
        } catch (const std::exception &) {
            threw = true;
        }
        QVERIFY(threw);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("1bad"))));
    }

    void invalidBoardThrowsAndWritesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto snapshot = makeSnapshot(QStringLiteral("projbad"), QStringLiteral("Bad board"));
        snapshot.board = QStringLiteral("not-a-real-board");
        bool threw = false;
        try {
            ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));
        } catch (const std::exception &) {
            threw = true;
        }
        QVERIFY(threw);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("projbad"))));
    }

    void listOnlyReturnsValidManifestDirs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto snapshot = makeSnapshot(QStringLiteral("goodproj"), QStringLiteral("Good"));
        ProjectStore::save(dir.path(), snapshot, QStringLiteral(BOARD_CATALOG_FIXTURE));

        QDir(dir.path()).mkpath(QStringLiteral("emptydir"));
        QDir(dir.path()).mkpath(QStringLiteral("badmanifest"));
        QFile bad(QDir(dir.path()).filePath(QStringLiteral("badmanifest/project.json")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("{\"not\":\"valid\"}");
        bad.close();

        const auto ids = ProjectStore::list(dir.path());
        QCOMPARE(ids.size(), 1);
        QVERIFY(ids.contains(QStringLiteral("goodproj")));
    }
};

QTEST_APPLESS_MAIN(ProjectStoreTest)
#include "project_store_test.moc"
