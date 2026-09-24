#pragma once

// Drag source for creating graph nodes (contract §5 B / §7 "Palette to
// canvas drag"). Categories in web order (graph_editor.html renderPalette,
// ~line 2003), empty categories showing their placeholder text, and a search
// filter over type key + description.

#include "node_type_registry.h"

#include <QLineEdit>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QWidget>

class QMimeData;

namespace Hypr {

// The Qt mime type used to drop a node onto the canvas (contract §7): the
// payload is the UTF-8 node type key.
inline const char *kNodeDragMimeType = "application/x-hypraccel-node";

class NodePalette : public QWidget {
    Q_OBJECT
public:
    explicit NodePalette(const NodeTypeRegistry *registry, QWidget *parent = nullptr);

    // Test/introspection helpers: what the tree currently shows.
    QStringList visibleCategoryNames() const;
    QStringList visibleTypesInCategory(const QString &categoryName) const;
    QString placeholderFor(const QString &categoryName) const; // empty if not a placeholder row

    // Single source of truth for the drag payload (contract §7): a QMimeData
    // with kNodeDragMimeType set to the UTF-8 type key. Exposed statically so
    // tests can verify the exact payload without driving a real QDrag.
    static QMimeData *mimeDataForType(const QString &type);

public Q_SLOTS:
    void setFilterText(const QString &text);

Q_SIGNALS:
    void nodeActivated(const QString &type); // double-click; Phase 2 may wire this to add-at-center

private:
    class Tree : public QTreeWidget {
    public:
        explicit Tree(QWidget *parent = nullptr) : QTreeWidget(parent) {}
    protected:
        QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
        void startDrag(Qt::DropActions supportedActions) override;
    };

    const NodeTypeRegistry *registry_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    Tree *tree_ = nullptr;
    QString filterText_;

    void rebuild();
};

} // namespace Hypr
