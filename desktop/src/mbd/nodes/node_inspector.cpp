#include "node_inspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSet>
#include <QVBoxLayout>

namespace Hypr {

NodeInspector::Source::~Source() = default;

namespace {

// Approximates JS `'' + value` / `String(value)` for the values our node
// params ever hold (numbers, strings, booleans) — enough to match a loaded
// string "7" against the numeric option 7 (contract D4).
QString jsStringify(const Json::Value &v)
{
    switch (v.kind) {
    case Json::Value::Kind::Null: return QString();
    case Json::Value::Kind::Bool: return v.boolean ? QStringLiteral("true") : QStringLiteral("false");
    case Json::Value::Kind::Number: {
        const double n = v.number;
        if (n == static_cast<qint64>(n)) return QString::number(static_cast<qint64>(n));
        return QString::number(n, 'g', 17);
    }
    case Json::Value::Kind::String: return v.string;
    default: return QString();
    }
}

// Web `field()` number semantics (graph_editor.html:1208): ignore these
// partial inputs while typing; on blur, invalid/empty becomes 0.
bool isIgnoredNumberPartial(const QString &raw)
{
    return raw.isEmpty() || raw == QLatin1String("-") || raw == QLatin1String(".")
        || raw == QLatin1String("-.") || raw.endsWith(QLatin1Char('.'));
}

Json::Value paramOrNull(const GraphNode &node, const QString &key)
{
    if (const Json::Value *v = Json::find(node.params, key)) return *v;
    return Json::Value::null();
}

} // namespace

NodeInspector::NodeInspector(const NodeTypeRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , registry_(registry)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    bodyLayout_ = new QVBoxLayout();
    outer->addLayout(bodyLayout_);
    outer->addStretch(1);
    showEmptyState();
}

void NodeInspector::setSource(Source *source)
{
    source_ = source;
    rebuild();
}

void NodeInspector::setHardware(const QJsonObject &hardware)
{
    hardware_ = hardware;
    rebuild();
}

void NodeInspector::showNode(const QString &nodeId)
{
    currentNodeId_ = nodeId;
    rebuild();
}

void NodeInspector::refresh()
{
    rebuild();
}

void NodeInspector::clearBody()
{
    QLayoutItem *item = nullptr;
    while ((item = bodyLayout_->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
}

void NodeInspector::showEmptyState()
{
    clearBody();
    auto *label = new QLabel(QStringLiteral("Select a node on the canvas to configure its parameters."), this);
    label->setObjectName(QStringLiteral("emptyStateLabel"));
    label->setWordWrap(true);
    bodyLayout_->addWidget(label);
}

void NodeInspector::rebuild()
{
    if (!registry_ || !source_ || currentNodeId_.isEmpty()) {
        showEmptyState();
        return;
    }
    const GraphNode *node = source_->node(currentNodeId_);
    if (!node) {
        showEmptyState();
        return;
    }
    clearBody();

    auto *header = new QLabel(registry_->displayName(node->type) + QStringLiteral(" Properties — ") + node->id, this);
    header->setObjectName(QStringLiteral("inspectorHeader"));
    bodyLayout_->addWidget(header);

    for (const auto &group : registry_->inspectorGroups(node->type))
        buildGroup(group, *node);

    if (registry_->isCustomCode(node->type))
        buildCustomCodeGroup(*node);

    if (!registry_->isAccelerator(node->type) && !registry_->hardwareResourceTypes(node->type).isEmpty())
        buildHardwareGroup(*node);

    if (registry_->isAccelerator(node->type))
        buildAcceleratorGroup(*node);
}

void NodeInspector::buildGroup(const InspectorGroup &group, const GraphNode &node)
{
    auto *title = new QLabel(group.name, this);
    title->setObjectName(QStringLiteral("groupTitle_") + group.name);
    bodyLayout_->addWidget(title);

    for (const auto &field : group.fields) {
        const QString nodeId = node.id;
        const QString key = field.key;
        // Web `field()` (graph_editor.html:1189): the group label is "Retain"
        // for `retain`, else the raw key; a checkbox additionally carries its
        // own text, "Enable retained message" for `retain`, else "Enabled".
        const QString label = (field.kind == QLatin1String("check") && key == QLatin1String("retain"))
            ? QStringLiteral("Retain")
            : key;
        auto *labelWidget = new QLabel(label, this);
        bodyLayout_->addWidget(labelWidget);

        const Json::Value current = paramOrNull(node, key);

        if (field.kind == QLatin1String("check")) {
            auto *box = new QCheckBox(key == QLatin1String("retain") ? QStringLiteral("Enable retained message") : QStringLiteral("Enabled"), this);
            box->setObjectName(QStringLiteral("field_") + key);
            box->setChecked(current.isBool() && current.boolean);
            connect(box, &QCheckBox::toggled, this, [this, nodeId, key](bool checked) {
                if (source_) source_->setParam(nodeId, key, Json::Value::fromBool(checked));
            });
            bodyLayout_->addWidget(box);
        } else if (field.kind == QLatin1String("select")) {
            auto *combo = new QComboBox(this);
            combo->setObjectName(QStringLiteral("field_") + key);
            const QString currentStr = jsStringify(current);
            int selectIndex = -1;
            for (int i = 0; i < field.options.size(); ++i) {
                const auto &opt = field.options[i];
                combo->addItem(opt.label, QVariant::fromValue(opt.value));
                if (selectIndex < 0 && jsStringify(opt.value) == currentStr) selectIndex = i;
            }
            if (selectIndex >= 0) combo->setCurrentIndex(selectIndex);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, nodeId, key, combo](int index) {
                if (!source_ || index < 0) return;
                const Json::Value value = combo->itemData(index).value<Json::Value>();
                source_->setParam(nodeId, key, value);
            });
            bodyLayout_->addWidget(combo);
        } else if (field.kind == QLatin1String("number")) {
            auto *edit = new QLineEdit(this);
            edit->setObjectName(QStringLiteral("field_") + key);
            edit->setText(jsStringify(current.isNumber() ? current : Json::Value::fromNumber(0)));
            connect(edit, &QLineEdit::textEdited, this, [this, nodeId, key, edit](const QString &text) {
                const QString raw = text.trimmed();
                if (isIgnoredNumberPartial(raw)) return;
                bool ok = false;
                const double num = raw.toDouble(&ok);
                if (ok && source_) source_->setParam(nodeId, key, Json::Value::fromNumber(num));
            });
            connect(edit, &QLineEdit::editingFinished, this, [this, nodeId, key, edit]() {
                const QString raw = edit->text().trimmed();
                bool ok = false;
                const double num = raw.toDouble(&ok);
                if (raw.isEmpty() || !ok) {
                    edit->setText(QStringLiteral("0"));
                    if (source_) source_->setParam(nodeId, key, Json::Value::fromNumber(0));
                } else if (source_) {
                    source_->setParam(nodeId, key, Json::Value::fromNumber(num));
                }
            });
            bodyLayout_->addWidget(edit);
        } else { // text
            auto *edit = new QLineEdit(this);
            edit->setObjectName(QStringLiteral("field_") + key);
            edit->setText(current.isString() ? current.string : QString());
            connect(edit, &QLineEdit::textChanged, this, [this, nodeId, key](const QString &text) {
                if (source_) source_->setParam(nodeId, key, Json::Value::fromString(text));
            });
            bodyLayout_->addWidget(edit);
        }
    }
}

void NodeInspector::buildCustomCodeGroup(const GraphNode &node)
{
    const QString nodeId = node.id;
    auto *title = new QLabel(QStringLiteral("Custom C Body"), this);
    bodyLayout_->addWidget(title);

    QStringList currentInputs;
    if (const Json::Value *inputsVal = Json::find(node.params, QStringLiteral("inputs"))) {
        if (inputsVal->isArray())
            for (const auto &item : inputsVal->array)
                if (item.isString()) currentInputs.push_back(item.string);
    }

    auto *inputsLabel = new QLabel(QStringLiteral("Input identifiers"), this);
    bodyLayout_->addWidget(inputsLabel);
    auto *inputsEdit = new QLineEdit(this);
    inputsEdit->setObjectName(QStringLiteral("customCodeInputsEdit"));
    inputsEdit->setText(currentInputs.join(QStringLiteral(", ")));
    connect(inputsEdit, &QLineEdit::editingFinished, this, [this, nodeId, inputsEdit]() {
        if (!source_) return;
        const auto parts = inputsEdit->text().split(QLatin1Char(','));
        Json::Array arr;
        for (const auto &part : parts) {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty()) arr.push_back(Json::Value::fromString(trimmed));
        }
        source_->setParam(nodeId, QStringLiteral("inputs"), Json::Value::fromArray(arr));
    });
    bodyLayout_->addWidget(inputsEdit);

    QString code;
    if (const Json::Value *codeVal = Json::find(node.params, QStringLiteral("code")))
        if (codeVal->isString()) code = codeVal->string;

    auto *codeLabel = new QLabel(QStringLiteral("Generated C body"), this);
    bodyLayout_->addWidget(codeLabel);
    auto *codeEdit = new QPlainTextEdit(this);
    codeEdit->setObjectName(QStringLiteral("customCodeEdit"));
    codeEdit->setPlainText(code);
    connect(codeEdit, &QPlainTextEdit::textChanged, this, [this, nodeId, codeEdit]() {
        if (source_) source_->setParam(nodeId, QStringLiteral("code"), Json::Value::fromString(codeEdit->toPlainText()));
    });
    bodyLayout_->addWidget(codeEdit);
}

void NodeInspector::buildHardwareGroup(const GraphNode &node)
{
    const QString nodeId = node.id;
    const QString type = node.type;
    auto *title = new QLabel(QStringLiteral("Hardware Setup"), this);
    bodyLayout_->addWidget(title);

    auto *combo = new QComboBox(this);
    combo->setObjectName(QStringLiteral("hardwareResourceCombo"));
    auto *summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("hardwareResourceSummary"));
    summary->setWordWrap(true);

    const QStringList expectedTypes = registry_->hardwareResourceTypes(type);
    const QJsonObject resources = hardware_.value(QStringLiteral("resources")).toObject();
    QSet<QString> configured;
    for (const auto &a : hardware_.value(QStringLiteral("assignments")).toArray())
        configured.insert(a.toObject().value(QStringLiteral("resource")).toString());

    const Json::Value current = paramOrNull(node, QStringLiteral("hardwareResource"));
    const QString currentResource = current.isString() ? current.string : QString();

    combo->addItem(QStringLiteral("— no hardware resource —"), QString());
    QJsonObject matchedResource;
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        const QJsonObject resource = it.value().toObject();
        if (!resource.value(QStringLiteral("available")).toBool()) continue;
        const QString resType = resource.value(QStringLiteral("type")).toString();
        if (!expectedTypes.isEmpty() && !expectedTypes.contains(resType)) continue;
        const QString id = resource.value(QStringLiteral("id")).toString();
        QString label = id;
        if (!configured.contains(id)) label += QStringLiteral(" (unassigned pin)");
        combo->addItem(label, id);
        if (id == currentResource) matchedResource = resource;
    }
    const int idx = combo->findData(currentResource);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);

    if (currentResource.isEmpty()) {
        summary->setText(QStringLiteral("Select a hardware resource configured in Hardware Setup."));
    } else if (matchedResource.isEmpty()) {
        summary->setText(QStringLiteral("Resource '%1' is not configured in Hardware Setup.").arg(currentResource));
    } else {
        QStringList lines;
        for (const auto &a : matchedResource.value(QStringLiteral("assignments")).toArray()) {
            const QJsonObject aObj = a.toObject();
            lines.push_back(aObj.value(QStringLiteral("role")).toString().toUpper() + QStringLiteral(" → ") + aObj.value(QStringLiteral("pin")).toString());
        }
        summary->setText(lines.isEmpty()
            ? matchedResource.value(QStringLiteral("id")).toString() + QStringLiteral(" (no pins assigned)")
            : matchedResource.value(QStringLiteral("id")).toString() + QStringLiteral("\n") + lines.join(QStringLiteral("\n")));
    }

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, nodeId, combo](int index) {
        if (!source_) return;
        const QString value = combo->itemData(index).toString();
        if (value.isEmpty()) source_->removeParam(nodeId, QStringLiteral("hardwareResource"));
        else source_->setParam(nodeId, QStringLiteral("hardwareResource"), Json::Value::fromString(value));
    });

    bodyLayout_->addWidget(combo);
    bodyLayout_->addWidget(summary);
}

void NodeInspector::buildAcceleratorGroup(const GraphNode &node)
{
    const QString nodeId = node.id;
    auto *title = new QLabel(QStringLiteral("Accelerator / CORDIC"), this);
    bodyLayout_->addWidget(title);

    auto *combo = new QComboBox(this);
    combo->setObjectName(QStringLiteral("acceleratorResourceCombo"));

    const QJsonObject resources = hardware_.value(QStringLiteral("resources")).toObject();
    const Json::Value current = paramOrNull(node, QStringLiteral("hardwareResource"));
    const QString currentResource = current.isString() ? current.string : QString();

    combo->addItem(QStringLiteral("— no accelerator resource —"), QString());
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        const QJsonObject resource = it.value().toObject();
        if (resource.value(QStringLiteral("type")).toString() != QLatin1String("accelerator")) continue;
        if (!resource.value(QStringLiteral("available")).toBool()) continue;
        if (resource.value(QStringLiteral("configuration")).toObject().isEmpty()) continue;
        const QString id = resource.value(QStringLiteral("id")).toString();
        combo->addItem(id, id);
    }
    const int idx = combo->findData(currentResource);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, nodeId, combo](int index) {
        if (!source_) return;
        const QString value = combo->itemData(index).toString();
        if (value.isEmpty()) source_->removeParam(nodeId, QStringLiteral("hardwareResource"));
        else source_->setParam(nodeId, QStringLiteral("hardwareResource"), Json::Value::fromString(value));
    });

    bodyLayout_->addWidget(combo);
}

} // namespace Hypr
