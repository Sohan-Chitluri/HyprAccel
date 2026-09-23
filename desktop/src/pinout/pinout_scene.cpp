#include "pinout_scene.h"
#include "theme.h"

#include <QFont>
#include <QGraphicsRectItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsTextItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

namespace {

// Selectable, hoverable pin lead. The base fill colour lives in brush() (set
// via setBrush by PinoutScene::recolor / rebuild); hover/selection are purely
// paint-time overlays so recolouring never disturbs either state.
class PinLead : public QGraphicsRectItem {
public:
    using QGraphicsRectItem::QGraphicsRectItem;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        hovered = true;
        update();
        QGraphicsRectItem::hoverEnterEvent(event);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        hovered = false;
        update();
        QGraphicsRectItem::hoverLeaveEvent(event);
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        Q_UNUSED(widget);
        QColor fill = brush().color();
        if (hovered) fill = fill.lighter(130);
        painter->setBrush(fill);
        QPen pen(StudioTheme::token("mutedFG"));
        if (option->state & QStyle::State_Selected) {
            pen = QPen(StudioTheme::token("primary"));
            pen.setWidthF(2);
        } else if (hovered) {
            pen = QPen(StudioTheme::token("foreground"));
        }
        painter->setPen(pen);
        painter->drawRect(rect());
    }

private:
    bool hovered = false;
};

QString tooltipFor(const Hypr::Pin &pin, Hypr::PinAssignmentModel *model)
{
    const QString function = model->functionFor(pin.name);
    return pin.name + "\nAssigned: " + (function.isEmpty() ? QStringLiteral("none") : function)
        + "\n" + pin.functions.join("\n");
}

QColor labelColorFor(Hypr::FunctionKind kind)
{
    return kind == Hypr::FunctionKind::Unassigned ? StudioTheme::token("mutedFG") : PinoutStyle::colorFor(kind);
}

} // namespace

namespace PinoutStyle {

QColor colorFor(Hypr::FunctionKind kind)
{
    // Okabe-Ito colour-blind-safe palette, chosen for readability on the dark
    // StudioTheme canvas (yellow and black omitted as too low-contrast there).
    switch (kind) {
    case Hypr::FunctionKind::Gpio: return QColor(0x56, 0xB4, 0xE9); // sky blue
    case Hypr::FunctionKind::Uart: return QColor(0xE6, 0x9F, 0x00); // orange
    case Hypr::FunctionKind::Spi:  return QColor(0x00, 0x9E, 0x73); // bluish green
    case Hypr::FunctionKind::I2c:  return QColor(0x00, 0x72, 0xB2); // blue
    case Hypr::FunctionKind::Pwm:  return QColor(0xCC, 0x79, 0xA7); // reddish purple
    case Hypr::FunctionKind::Adc:  return QColor(0xD5, 0x5E, 0x00); // vermillion
    case Hypr::FunctionKind::Unassigned: break;
    }
    return StudioTheme::token("surface3");
}

QString labelFor(Hypr::FunctionKind kind)
{
    switch (kind) {
    case Hypr::FunctionKind::Gpio: return QStringLiteral("GPIO");
    case Hypr::FunctionKind::Uart: return QStringLiteral("UART");
    case Hypr::FunctionKind::Spi:  return QStringLiteral("SPI");
    case Hypr::FunctionKind::I2c:  return QStringLiteral("I²C");
    case Hypr::FunctionKind::Pwm:  return QStringLiteral("PWM");
    case Hypr::FunctionKind::Adc:  return QStringLiteral("ADC");
    case Hypr::FunctionKind::Unassigned: break;
    }
    return QStringLiteral("Unassigned");
}

QWidget *createLegend(QWidget *parent)
{
    auto *legend = new QWidget(parent);
    legend->setObjectName("pinLegend");
    auto *layout = new QHBoxLayout(legend);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(14);

    static const Hypr::FunctionKind kinds[] = {
        Hypr::FunctionKind::Unassigned, Hypr::FunctionKind::Gpio, Hypr::FunctionKind::Uart,
        Hypr::FunctionKind::Spi,        Hypr::FunctionKind::I2c, Hypr::FunctionKind::Pwm,
        Hypr::FunctionKind::Adc,
    };
    for (auto kind : kinds) {
        auto *row = new QWidget(legend);
        row->setProperty("kind", Hypr::kindName(kind));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(5);

        auto *swatch = new QLabel(row);
        swatch->setFixedSize(12, 12);
        const QColor color = colorFor(kind);
        swatch->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:2px;")
                                   .arg(color.name(), StudioTheme::token("border").name()));

        auto *caption = new QLabel(labelFor(kind), row);
        caption->setStyleSheet(QStringLiteral("color:%1;").arg(StudioTheme::token("mutedFG").name()));

        rowLayout->addWidget(swatch);
        rowLayout->addWidget(caption);
        layout->addWidget(row);
    }
    layout->addStretch();
    return legend;
}

} // namespace PinoutStyle

PinoutScene::PinoutScene(Hypr::PinAssignmentModel *model, QObject *parent)
    : QGraphicsScene(parent), model(model)
{
    connect(model, &Hypr::PinAssignmentModel::assignmentChanged, this,
            [this](const QString &pin, const QString &) { recolor(pin); });
    connect(model, &Hypr::PinAssignmentModel::reset, this, &PinoutScene::rebuild);
    rebuild();
}

QGraphicsItem *PinoutScene::pinItem(const QString &pin) const { return leads.value(pin, nullptr); }

void PinoutScene::rebuild()
{
    clear();
    leads.clear();

    const Hypr::Board *board = model->board();
    if (!board) {
        auto *text = addText("Select a target board above");
        text->setDefaultTextColor(StudioTheme::token("mutedFG"));
        setSceneRect(0, 0, 760, 760);
        return;
    }

    for (int i = 0; i <= 760; i += 16) {
        addLine(i, 0, i, 760, QPen(StudioTheme::token("border"), .25));
        addLine(0, i, 760, i, QPen(StudioTheme::token("border"), .25));
    }
    const qreal body = 372, origin = 194;
    addRect(origin, origin, body, body, QPen(StudioTheme::token("border")), StudioTheme::token("surface2"));
    auto *name = addText(board->id.toUpper(), QFont("monospace", 18));
    name->setDefaultTextColor(StudioTheme::token("foreground"));
    name->setPos(380 - name->boundingRect().width() / 2, 335);
    auto *subtitle = addText(board->architecture + "\nLOGICAL VIEW", QFont("monospace", 10));
    subtitle->setDefaultTextColor(StudioTheme::token("mutedFG"));
    subtitle->setPos(380 - subtitle->boundingRect().width() / 2, 385);

    const int count = board->pins.size();
    const int sideCount = (count + 3) / 4;
    const qreal pitch = body / (sideCount + 1);
    for (int i = 0; i < count; ++i) {
        const int side = i / sideCount, offset = i % sideCount;
        const qreal p = origin + pitch * (offset + 1);
        QRectF rect;
        if (side == 0) rect = {origin - 22, p - 5, 22, 10};
        else if (side == 1) rect = {p - 5, origin + body, 10, 22};
        else if (side == 2) rect = {origin + body, origin + body - pitch * (offset + 1) - 5, 22, 10};
        else rect = {origin + body - pitch * (offset + 1) - 5, origin - 22, 10, 22};

        const auto &pin = board->pins[i];
        const auto kind = model->kindFor(pin.name);

        auto *lead = new PinLead(rect);
        lead->setPen(QPen(StudioTheme::token("mutedFG")));
        lead->setBrush(PinoutStyle::colorFor(kind));
        lead->setFlag(QGraphicsItem::ItemIsSelectable);
        lead->setAcceptHoverEvents(true);
        lead->setData(0, pin.name);
        lead->setData(1, QStringLiteral("pin"));
        lead->setToolTip(tooltipFor(pin, model));
        addItem(lead);

        auto *text = new QGraphicsTextItem(pin.name, lead);
        text->setFont(QFont("monospace", 9));
        text->setDefaultTextColor(labelColorFor(kind));
        if (side == 0) text->setPos(rect.left() - text->boundingRect().width() - 4, rect.center().y() - text->boundingRect().height() / 2);
        else if (side == 2) text->setPos(rect.right() + 4, rect.center().y() - text->boundingRect().height() / 2);
        else {
            text->setRotation(-90);
            text->setPos(rect.center().x() - 10, side == 1 ? rect.bottom() + text->boundingRect().width() + 4 : rect.top() - 4);
        }

        leads.insert(pin.name, lead);
    }
    setSceneRect(0, 0, 760, 760);
}

void PinoutScene::recolor(const QString &pin)
{
    auto it = leads.find(pin);
    if (it == leads.end()) return;
    auto *lead = static_cast<PinLead *>(it.value());
    const auto kind = model->kindFor(pin);
    lead->setBrush(PinoutStyle::colorFor(kind));

    const Hypr::Board *board = model->board();
    if (board) {
        for (const auto &candidate : board->pins) {
            if (candidate.name == pin) {
                lead->setToolTip(tooltipFor(candidate, model));
                break;
            }
        }
    }
    for (auto *child : lead->childItems()) {
        if (auto *text = qgraphicsitem_cast<QGraphicsTextItem *>(child))
            text->setDefaultTextColor(labelColorFor(kind));
    }
    lead->update();
}
