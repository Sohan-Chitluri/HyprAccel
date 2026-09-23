#include "clock_view.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString formatMHz(double value)
{
    return QString::number(value, 'g', 6) + " MHz";
}

QString kindName(Hypr::ClockNode::Kind kind)
{
    switch (kind) {
    case Hypr::ClockNode::Kind::Source: return QStringLiteral("Source");
    case Hypr::ClockNode::Kind::Mux: return QStringLiteral("Mux");
    case Hypr::ClockNode::Kind::Pll: return QStringLiteral("PLL");
    case Hypr::ClockNode::Kind::Divider: return QStringLiteral("Divider");
    case Hypr::ClockNode::Kind::Gate: return QStringLiteral("Gate");
    case Hypr::ClockNode::Kind::Output: return QStringLiteral("Output");
    }
    return {};
}

QString tooltipFor(const Hypr::ClockNode &node)
{
    QStringList parts;
    if (!node.reference.isEmpty())
        parts << node.reference;
    if (!node.note.isEmpty())
        parts << node.note;
    return parts.join("\n\n");
}

const Hypr::ClockNode *findNode(const Hypr::ClockTree &tree, const QString &id)
{
    for (const auto &node : tree.nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}

// ClockView::Private is a private nested type (per the owned clock_view.h
// contract), so free helper functions in this file cannot name it directly.
// Impl holds the actual state; ClockView::Private (defined below) is a thin
// wrapper so ClockView's own members can still say "d->...".
struct Impl {
    QString catalogPath;
    QMap<QString, Hypr::ClockTree> catalog;
    QString catalogError;

    QString boardId;
    Hypr::ClockTree tree;
    Hypr::ClockConfig config;
    Hypr::ClockResult result;

    QStackedWidget *stack = nullptr;
    QLabel *placeholder = nullptr;
    QWidget *content = nullptr;
    QWidget *nodesHost = nullptr;
    QHBoxLayout *nodesLayout = nullptr;
    QLabel *errorsLabel = nullptr;
    QTableWidget *outputsTable = nullptr;
    QMap<QString, QComboBox *> combos;
    QMap<QString, QLabel *> valueLabels;
    bool updating = false;
};

} // namespace

struct ClockView::Private {
    Impl impl;
};

namespace {

void applyResult(ClockView *view, Impl *d)
{
    d->result = Hypr::computeClocks(d->tree, d->config);

    d->updating = true;
    for (const auto &node : d->tree.nodes) {
        if (auto *label = d->valueLabels.value(node.id)) {
            const auto freq = d->result.freqMHz.value(node.id, 0);
            QString text = formatMHz(freq);
            if (node.maxMHz > 0 && freq > node.maxMHz + 1e-9)
                text += QStringLiteral(" (over limit)");
            label->setText(text);
        }
        if (auto *combo = d->combos.value(node.id)) {
            const auto selected = d->config.selections.value(node.id, node.defaultValue);
            const auto index = combo->findData(selected);
            if (index >= 0 && combo->currentIndex() != index)
                combo->setCurrentIndex(index);
        }
    }
    d->updating = false;

    d->errorsLabel->setStyleSheet(QString("color:%1;").arg(
        StudioTheme::token(d->result.errors.isEmpty() ? "ok" : "err").name()));
    d->errorsLabel->setText(d->result.errors.isEmpty() ? QStringLiteral("No clock errors.")
                                                         : d->result.errors.join("\n"));

    d->outputsTable->setRowCount(d->tree.peripheralOutputs.size());
    for (int row = 0; row < d->tree.peripheralOutputs.size(); ++row) {
        const auto id = d->tree.peripheralOutputs.at(row);
        const auto *node = findNode(d->tree, id);
        const auto freq = d->result.freqMHz.value(id, 0);
        const auto label = node ? node->label : id;
        const auto limit = (node && node->maxMHz > 0) ? formatMHz(node->maxMHz) : QStringLiteral("—");
        const bool overLimit = node && node->maxMHz > 0 && freq > node->maxMHz + 1e-9;
        const auto status = overLimit ? QStringLiteral("Exceeds limit") : QStringLiteral("OK");

        auto *labelItem = new QTableWidgetItem(label);
        labelItem->setToolTip(node ? tooltipFor(*node) : QString());
        d->outputsTable->setItem(row, 0, labelItem);
        d->outputsTable->setItem(row, 1, new QTableWidgetItem(formatMHz(freq)));
        d->outputsTable->setItem(row, 2, new QTableWidgetItem(limit));
        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setForeground(StudioTheme::token(overLimit ? "err" : "ok"));
        d->outputsTable->setItem(row, 3, statusItem);
    }
}

void clearNodes(Impl *d)
{
    d->combos.clear();
    d->valueLabels.clear();
    QLayoutItem *item = nullptr;
    while ((item = d->nodesLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
}

void buildNodes(ClockView *view, Impl *d)
{
    clearNodes(d);
    const auto cardStyle = QString("QFrame{background:%1;border:1px solid %2;border-radius:6px;}")
                                .arg(StudioTheme::token("card").name(), StudioTheme::token("border").name());
    const auto fgStyle = QString("color:%1;").arg(StudioTheme::token("foreground").name());
    const auto mutedStyle = QString("color:%1;font-size:11px;").arg(StudioTheme::token("mutedFG").name());

    for (const auto &node : d->tree.nodes) {
        auto *card = new QFrame(d->nodesHost);
        card->setObjectName("clockNode:" + node.id);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(cardStyle);
        card->setMinimumWidth(150);
        card->setToolTip(tooltipFor(node));

        auto *cardLayout = new QVBoxLayout(card);
        auto *titleLabel = new QLabel(node.label, card);
        titleLabel->setStyleSheet(fgStyle + "font-weight:600;");
        cardLayout->addWidget(titleLabel);

        auto *kindLabel = new QLabel(kindName(node.kind), card);
        kindLabel->setStyleSheet(mutedStyle);
        cardLayout->addWidget(kindLabel);

        auto *valueLabel = new QLabel(card);
        valueLabel->setObjectName("clockValue:" + node.id);
        valueLabel->setStyleSheet(fgStyle);
        cardLayout->addWidget(valueLabel);
        d->valueLabels.insert(node.id, valueLabel);

        if (node.editable) {
            auto *combo = new QComboBox(card);
            combo->setObjectName("clock:" + node.id);
            combo->setToolTip(card->toolTip());
            if (node.kind == Hypr::ClockNode::Kind::Mux) {
                for (const auto &inputId : node.inputs) {
                    const auto *inputNode = findNode(d->tree, inputId);
                    combo->addItem(inputNode ? inputNode->label : inputId, inputId);
                }
            } else {
                for (const double option : node.options)
                    combo->addItem(QString::number(option, 'g', 10), QString::number(option, 'g', 15));
            }
            cardLayout->addWidget(combo);
            d->combos.insert(node.id, combo);

            const QString id = node.id;
            QObject::connect(combo, &QComboBox::currentIndexChanged, view, [view, d, id](int) {
                if (d->updating)
                    return;
                auto *box = d->combos.value(id);
                if (!box)
                    return;
                d->config.selections.insert(id, box->currentData().toString());
                applyResult(view, d);
                Q_EMIT view->configChanged();
            });
        } else if (!node.note.isEmpty()) {
            auto *noteLabel = new QLabel(node.note, card);
            noteLabel->setWordWrap(true);
            noteLabel->setStyleSheet(mutedStyle);
            noteLabel->setMaximumWidth(200);
            cardLayout->addWidget(noteLabel);
        }

        cardLayout->addStretch();
        d->nodesLayout->addWidget(card);
    }
    d->nodesLayout->addStretch();
}

} // namespace

ClockView::~ClockView() { delete d; }

ClockView::ClockView(const QString &catalogPath, QWidget *parent) : QWidget(parent), d(new Private)
{
    auto &impl = d->impl;
    impl.catalogPath = catalogPath;
    try {
        impl.catalog = Hypr::ClockCatalog::load(catalogPath);
    } catch (const std::exception &error) {
        impl.catalogError = QString::fromUtf8(error.what());
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    impl.stack = new QStackedWidget(this);
    root->addWidget(impl.stack);

    impl.placeholder = new QLabel(this);
    impl.placeholder->setObjectName("clockPlaceholder");
    impl.placeholder->setAlignment(Qt::AlignCenter);
    impl.placeholder->setWordWrap(true);
    impl.placeholder->setStyleSheet(QString("color:%1;padding:24px;").arg(StudioTheme::token("mutedFG").name()));
    impl.placeholder->setText(QStringLiteral("Select a board to view its clock configuration."));
    impl.stack->addWidget(impl.placeholder);

    impl.content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(impl.content);
    contentLayout->setContentsMargins(12, 12, 12, 12);

    auto *scroll = new QScrollArea(impl.content);
    scroll->setObjectName("clockTreeScroll");
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    impl.nodesHost = new QWidget;
    impl.nodesLayout = new QHBoxLayout(impl.nodesHost);
    impl.nodesLayout->setContentsMargins(4, 4, 4, 4);
    impl.nodesLayout->setSpacing(16);
    impl.nodesLayout->addStretch();
    scroll->setWidget(impl.nodesHost);
    contentLayout->addWidget(scroll, 1);

    impl.errorsLabel = new QLabel(impl.content);
    impl.errorsLabel->setObjectName("clockErrors");
    impl.errorsLabel->setWordWrap(true);
    impl.errorsLabel->setStyleSheet(QString("color:%1;").arg(StudioTheme::token("err").name()));
    contentLayout->addWidget(impl.errorsLabel);

    auto *outputsHeading = new QLabel(QStringLiteral("Peripheral clocks"), impl.content);
    outputsHeading->setStyleSheet(QString("color:%1;font-weight:600;").arg(StudioTheme::token("foreground").name()));
    contentLayout->addWidget(outputsHeading);

    impl.outputsTable = new QTableWidget(0, 4, impl.content);
    impl.outputsTable->setObjectName("clockOutputs");
    impl.outputsTable->setHorizontalHeaderLabels({"Peripheral", "MHz", "Limit", "Status"});
    impl.outputsTable->horizontalHeader()->setStretchLastSection(true);
    impl.outputsTable->verticalHeader()->setVisible(false);
    impl.outputsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    impl.outputsTable->setSelectionMode(QAbstractItemView::NoSelection);
    impl.outputsTable->setMaximumHeight(160);
    contentLayout->addWidget(impl.outputsTable);

    impl.stack->addWidget(impl.content);
    impl.stack->setCurrentWidget(impl.placeholder);
}

void ClockView::setBoard(const QString &boardId)
{
    auto &impl = d->impl;
    impl.boardId = boardId;

    if (!impl.catalog.contains(boardId)) {
        clearNodes(&impl);
        impl.tree = Hypr::ClockTree{};
        impl.config = Hypr::ClockConfig{};
        impl.result = Hypr::ClockResult{};
        impl.errorsLabel->clear();
        impl.outputsTable->setRowCount(0);
        const auto message = !impl.catalogError.isEmpty()
            ? QStringLiteral("Clock data unavailable: %1").arg(impl.catalogError)
            : boardId.isEmpty()
                ? QStringLiteral("Select a board to view its clock configuration.")
                : QStringLiteral("No clock configuration data for board '%1'.").arg(boardId);
        impl.placeholder->setText(message);
        impl.stack->setCurrentWidget(impl.placeholder);
        return;
    }

    impl.tree = impl.catalog.value(boardId);
    impl.config = Hypr::defaultClockConfig(impl.tree);
    buildNodes(this, &impl);
    applyResult(this, &impl);
    impl.stack->setCurrentWidget(impl.content);
}

QString ClockView::boardId() const
{
    return d->impl.boardId;
}

Hypr::ClockConfig ClockView::config() const
{
    return d->impl.config;
}

void ClockView::setConfig(const Hypr::ClockConfig &config)
{
    auto &impl = d->impl;
    Hypr::ClockConfig filtered;
    for (auto it = config.selections.constBegin(); it != config.selections.constEnd(); ++it) {
        const auto *node = findNode(impl.tree, it.key());
        if (!node || !node->editable)
            continue;
        if (node->kind == Hypr::ClockNode::Kind::Mux) {
            if (!node->inputs.contains(it.value()))
                continue;
        } else {
            bool ok = false;
            const auto value = it.value().toDouble(&ok);
            if (!ok || value <= 0)
                continue;
            if (!node->options.isEmpty()) {
                bool found = false;
                for (const double option : node->options)
                    found = found || qFuzzyCompare(option, value);
                if (!found)
                    continue;
            }
        }
        filtered.selections.insert(it.key(), it.value());
    }
    impl.config = filtered;
    applyResult(this, &impl);
}

Hypr::ClockResult ClockView::result() const
{
    return d->impl.result;
}
