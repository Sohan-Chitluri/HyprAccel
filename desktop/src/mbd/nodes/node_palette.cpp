#include "node_palette.h"

#include <QByteArray>
#include <QDrag>
#include <QMimeData>
#include <QVBoxLayout>

namespace Hypr {

namespace {
constexpr int kTypeRole = Qt::UserRole + 1;
constexpr int kIsPlaceholderRole = Qt::UserRole + 2;
constexpr int kIsCategoryRole = Qt::UserRole + 3;
}

QMimeData *NodePalette::mimeDataForType(const QString &type)
{
    if (type.isEmpty()) return nullptr;
    auto *mime = new QMimeData();
    mime->setData(QString::fromLatin1(kNodeDragMimeType), type.toUtf8());
    return mime;
}

QMimeData *NodePalette::Tree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    if (items.isEmpty()) return nullptr;
    return NodePalette::mimeDataForType(items.first()->data(0, kTypeRole).toString());
}

void NodePalette::Tree::startDrag(Qt::DropActions supportedActions)
{
    QTreeWidgetItem *item = currentItem();
    if (!item) return;
    const QString type = item->data(0, kTypeRole).toString();
    QMimeData *mime = NodePalette::mimeDataForType(type);
    if (!mime) return; // category header / placeholder row: not draggable

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(supportedActions, Qt::CopyAction);
}

NodePalette::NodePalette(const NodeTypeRegistry *registry, QWidget *parent)
    : QWidget(parent)
    , registry_(registry)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("paletteSearchEdit"));
    searchEdit_->setPlaceholderText(QStringLiteral("Search node types…"));
    connect(searchEdit_, &QLineEdit::textChanged, this, &NodePalette::setFilterText);
    layout->addWidget(searchEdit_);

    tree_ = new Tree(this);
    tree_->setObjectName(QStringLiteral("paletteTree"));
    tree_->setHeaderHidden(true);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const QString type = item->data(0, kTypeRole).toString();
        if (!type.isEmpty()) Q_EMIT nodeActivated(type);
    });
    layout->addWidget(tree_);

    rebuild();
}

void NodePalette::setFilterText(const QString &text)
{
    filterText_ = text;
    rebuild();
}

void NodePalette::rebuild()
{
    tree_->clear();
    if (!registry_) return;

    const QString filter = filterText_.toLower().trimmed();

    for (const auto &cat : registry_->categories()) {
        QStringList matchingItems;
        for (const auto &type : cat.items) {
            const bool typeMatches = type.toLower().contains(filter);
            const bool descMatches = registry_->description(type).toLower().contains(filter);
            if (typeMatches || descMatches) matchingItems.push_back(type);
        }
        const bool categoryNameMatches = cat.name.toLower().contains(filter);
        if (!filter.isEmpty() && matchingItems.isEmpty() && !categoryNameMatches) continue;

        auto *catItem = new QTreeWidgetItem(tree_);
        catItem->setText(0, cat.name + QStringLiteral(" (%1)").arg(cat.items.size()));
        catItem->setData(0, kIsCategoryRole, true);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
        catItem->setExpanded(true);

        if (cat.items.isEmpty()) {
            auto *placeholderItem = new QTreeWidgetItem(catItem);
            placeholderItem->setText(0, cat.placeholder.isEmpty() ? QStringLiteral("No nodes available") : cat.placeholder);
            placeholderItem->setData(0, kIsPlaceholderRole, true);
            placeholderItem->setFlags(placeholderItem->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsDragEnabled);
            placeholderItem->setDisabled(true);
            continue;
        }

        // Note: when a filter is active but this category's items are all
        // filtered out while the category name itself matches (web
        // behaviour), matchingItems is empty here but the category still
        // shows (with no children), matching graph_editor.html renderPalette.
        for (const auto &type : matchingItems) {
            auto *typeItem = new QTreeWidgetItem(catItem);
            typeItem->setText(0, registry_->displayName(type));
            typeItem->setToolTip(0, registry_->description(type));
            typeItem->setData(0, kTypeRole, type);
            typeItem->setFlags(typeItem->flags() | Qt::ItemIsDragEnabled);
        }
    }
}

QStringList NodePalette::visibleCategoryNames() const
{
    QStringList result;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QString name = tree_->topLevelItem(i)->text(0);
        const int parenIdx = name.lastIndexOf(QStringLiteral(" ("));
        if (parenIdx >= 0) name = name.left(parenIdx);
        result.push_back(name);
    }
    return result;
}

QStringList NodePalette::visibleTypesInCategory(const QString &categoryName) const
{
    QStringList result;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *catItem = tree_->topLevelItem(i);
        QString name = catItem->text(0);
        const int parenIdx = name.lastIndexOf(QStringLiteral(" ("));
        if (parenIdx >= 0) name = name.left(parenIdx);
        if (name != categoryName) continue;
        for (int c = 0; c < catItem->childCount(); ++c) {
            const QString type = catItem->child(c)->data(0, kTypeRole).toString();
            if (!type.isEmpty()) result.push_back(type);
        }
        break;
    }
    return result;
}

QString NodePalette::placeholderFor(const QString &categoryName) const
{
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *catItem = tree_->topLevelItem(i);
        QString name = catItem->text(0);
        const int parenIdx = name.lastIndexOf(QStringLiteral(" ("));
        if (parenIdx >= 0) name = name.left(parenIdx);
        if (name != categoryName) continue;
        if (catItem->childCount() == 1 && catItem->child(0)->data(0, kIsPlaceholderRole).toBool())
            return catItem->child(0)->text(0);
        return QString();
    }
    return QString();
}

} // namespace Hypr
