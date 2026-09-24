#include "ordered_json.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
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

// Runs `node -e <script>` and returns trimmed stdout, or a null QString if
// node could not be found/run.
QString runNode(const QString &script, bool *ok = nullptr)
{
    if (ok) *ok = false;
    const QString node = findNode();
    if (node.isEmpty()) return QString();
    QProcess process;
    process.start(node, { QStringLiteral("-e"), script });
    if (!process.waitForFinished(10000)) return QString();
    if (process.exitCode() != 0) return QString();
    if (ok) *ok = true;
    return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace

class OrderedJsonTest : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // --- Static golden subset: runs with no node dependency. ---

    void stringifyIntegers()
    {
        QCOMPARE(Json::stringify(Value::fromNumber(0)), QByteArray("0"));
        QCOMPARE(Json::stringify(Value::fromNumber(-0.0)), QByteArray("0"));
        QCOMPARE(Json::stringify(Value::fromNumber(1)), QByteArray("1"));
        QCOMPARE(Json::stringify(Value::fromNumber(-1)), QByteArray("-1"));
        QCOMPARE(Json::stringify(Value::fromNumber(120)), QByteArray("120"));
        QCOMPARE(Json::stringify(Value::fromNumber(1e20)), QByteArray("100000000000000000000"));
    }

    void stringifyFractional()
    {
        QCOMPARE(Json::stringify(Value::fromNumber(0.1)), QByteArray("0.1"));
        QCOMPARE(Json::stringify(Value::fromNumber(3.3)), QByteArray("3.3"));
        QCOMPARE(Json::stringify(Value::fromNumber(101.5)), QByteArray("101.5"));
        QCOMPARE(Json::stringify(Value::fromNumber(-2.5)), QByteArray("-2.5"));
    }

    void stringifyExponentForm()
    {
        QCOMPARE(Json::stringify(Value::fromNumber(1e21)), QByteArray("1e+21"));
        QCOMPARE(Json::stringify(Value::fromNumber(1e-7)), QByteArray("1e-7"));
        QCOMPARE(Json::stringify(Value::fromNumber(5e-324)), QByteArray("5e-324"));
        QCOMPARE(Json::stringify(Value::fromNumber(123456789012345680000.0)), QByteArray("123456789012345680000"));
    }

    void stringifyStrings()
    {
        QCOMPARE(Json::stringify(Value::fromString(QStringLiteral("a/b"))), QByteArray("\"a/b\""));
        QCOMPARE(Json::stringify(Value::fromString(QStringLiteral("a\"b\\c"))), QByteArray("\"a\\\"b\\\\c\""));
        QCOMPARE(Json::stringify(Value::fromString(QStringLiteral("a\nb\tb"))), QByteArray("\"a\\nb\\tb\""));
        QCOMPARE(Json::stringify(Value::fromString(QString(QChar(0x01)))), QByteArray("\"\\u0001\""));
        QCOMPARE(Json::stringify(Value::fromString(QStringLiteral("héllo"))), QByteArray("\"h\xC3\xA9llo\""));
        // Lone high surrogate (no matching low surrogate).
        QString lone;
        lone += QChar(0xD800);
        QCOMPARE(Json::stringify(Value::fromString(lone)), QByteArray("\"\\ud800\""));
        // A valid surrogate pair (an emoji) round-trips as raw UTF-8.
        QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80"); // U+1F600
        QCOMPARE(Json::stringify(Value::fromString(emoji)), QByteArray("\"\xF0\x9F\x98\x80\""));
    }

    void stringifyContainers()
    {
        QCOMPARE(Json::stringify(Value::fromObject({})), QByteArray("{}"));
        QCOMPARE(Json::stringify(Value::fromArray({})), QByteArray("[]"));

        Json::Object obj;
        Json::set(obj, QStringLiteral("b"), Value::fromNumber(2));
        Json::set(obj, QStringLiteral("a"), Value::fromNumber(1));
        const QByteArray expected = "{\n  \"b\": 2,\n  \"a\": 1\n}";
        QCOMPARE(Json::stringify(Value::fromObject(obj)), expected);

        Json::Array arr{ Value::fromNumber(1), Value::fromString(QStringLiteral("x")) };
        QCOMPARE(Json::stringify(Value::fromArray(arr)), QByteArray("[\n  1,\n  \"x\"\n]"));
    }

    void parseRoundTrip()
    {
        const QByteArray input = "{\n  \"z\": 1,\n  \"a\": [1, 2.5, \"s\\u00e9\", true, false, null]\n}";
        QString error;
        Value v = Json::parse(input, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(v.isObject());
        QCOMPARE(v.object.size(), size_t(2));
        QCOMPARE(v.object[0].first, QStringLiteral("z"));
        QCOMPARE(v.object[1].first, QStringLiteral("a"));
        const Value &arr = v.object[1].second;
        QVERIFY(arr.isArray());
        QCOMPARE(arr.array.size(), size_t(6));
        QCOMPARE(arr.array[2].string, QStringLiteral("s\u00e9"));
    }

    void parsePreservesKeyOrderAndDuplicates()
    {
        // JS object semantics: a repeated key overwrites the value but keeps
        // its original position.
        const QByteArray input = "{\"a\":1,\"b\":2,\"a\":3}";
        Value v = Json::parse(input);
        QVERIFY(v.isObject());
        QCOMPARE(v.object.size(), size_t(2));
        QCOMPARE(v.object[0].first, QStringLiteral("a"));
        QCOMPARE(v.object[0].second.number, 3.0);
        QCOMPARE(v.object[1].first, QStringLiteral("b"));
    }

    void parseInvalidReportsError()
    {
        QString error;
        Value v = Json::parse(QByteArray("{not json"), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(v.isNull());
    }

    // --- Node-backed exhaustive corpus comparison (skipped if node absent). ---

    void numbersMatchNodeCorpus()
    {
        const QString node = findNode();
        if (node.isEmpty())
            QSKIP("node not found; set HYPRACCEL_NODE or add node to PATH");

        const QVector<double> corpus = {
            0.0, -0.0, 1.0, -1.0, 3.3, 101.5, 120.0, 0.1, 1e21, 1e-7, 5e-324,
            123456789012345680000.0, 1e20, 1e-6, 1e-5, -1e21, 999999999999999999999.0,
            0.0001, 1234.5678, 2.0, -2.5, 1e100, 1e-100, 4294967296.0, 9007199254740993.0
        };
        QStringList jsScriptParts;
        for (double d : corpus) {
            jsScriptParts << QStringLiteral("JSON.stringify(%1)").arg(QString::number(d, 'r', 17));
        }
        const QString script = QStringLiteral("const vals=[%1]; console.log(vals.join('\\n'));")
            .arg([&]{
                QStringList literalParts;
                for (double d : corpus) literalParts << QString::number(d, 'g', 17);
                return literalParts.join(',');
            }());
        bool ok = false;
        const QString out = runNode(script, &ok);
        QVERIFY2(ok, "node -e failed to run");
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), corpus.size());
        for (int i = 0; i < corpus.size(); ++i) {
            const QByteArray ours = Json::stringify(Value::fromNumber(corpus[i]));
            QCOMPARE(QString::fromUtf8(ours), lines[i].trimmed());
        }
    }

    void stringsMatchNodeCorpus()
    {
        const QString node = findNode();
        if (node.isEmpty())
            QSKIP("node not found; set HYPRACCEL_NODE or add node to PATH");

        const QStringList corpus = {
            QStringLiteral("plain"),
            QStringLiteral("a/b/c"),
            QStringLiteral("quote\"here"),
            QStringLiteral("back\\slash"),
            QStringLiteral("line\nbreak"),
            QStringLiteral("tab\ttab"),
            QStringLiteral("cr\rreturn"),
            QStringLiteral("bell\x07here"),
            QStringLiteral("unicode\u00e9\u00fc"),
            QStringLiteral("emoji\U0001F600end"),
            QStringLiteral(""),
        };

        // Write each corpus string, base64-encoded, to a file that a small
        // node script reads back and stringifies, to avoid any shell/QProcess
        // argument-encoding pitfalls with control characters.
        for (const QString &s : corpus) {
            const QByteArray b64 = s.toUtf8().toBase64();
            const QString script = QStringLiteral(
                "console.log(JSON.stringify(Buffer.from('%1','base64').toString('utf8')));").arg(QString::fromLatin1(b64));
            bool ok = false;
            const QString out = runNode(script, &ok).trimmed();
            QVERIFY2(ok, "node -e failed to run");
            const QByteArray ours = Json::stringify(Value::fromString(s));
            QCOMPARE(QString::fromUtf8(ours), out);
        }
    }
};

QTEST_MAIN(OrderedJsonTest)
#include "ordered_json_test.moc"
