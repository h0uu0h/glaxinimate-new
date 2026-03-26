/*
 * SPDX-FileCopyrightText: 2019-2023 Mattia Basaglia <dev@dragon.best>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "timeline_treeview.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "item_models/property_model_full.hpp"
#include "item_models/comp_filter_model.hpp"

// Hierarchy colors for different nesting depths
static const QColor depth_colors[] = {
    QColor(108, 92, 231),   // purple — depth 0 (top-level layers)
    QColor(0, 206, 209),    // teal — depth 1
    QColor(253, 121, 168),  // pink — depth 2
    QColor(85, 239, 196),   // mint — depth 3
    QColor(255, 159, 67),   // orange — depth 4
    QColor(116, 185, 255),  // sky blue — depth 5+
};
static const int depth_color_count = sizeof(depth_colors) / sizeof(depth_colors[0]);

static QColor color_for_depth(int depth)
{
    return depth_colors[qMin(depth, depth_color_count - 1)];
}

static int index_depth(const QModelIndex& index)
{
    int depth = 0;
    QModelIndex parent = index.parent();
    while ( parent.isValid() )
    {
        depth++;
        parent = parent.parent();
    }
    return depth;
}


class glaxnimate::gui::TimelineTreeview::Private
{
public:
    QModelIndex source_index(const QModelIndex& index)
    {
        return proxy_model()->mapToSource(index);
    }

    item_models::CompFilterModel* proxy_model()
    {
        return static_cast<item_models::CompFilterModel*>(parent->model());
    }

    item_models::PropertyModelFull* source_model()
    {
        return static_cast<item_models::PropertyModelFull*>(proxy_model()->sourceModel());
    }

    model::Layer* layer(const QModelIndex& source_index)
    {
        return qobject_cast<model::Layer*>(source_model()->node(source_index));
    }

    void on_drag()
    {
        auto index = parent->indexAt(drag_to_local);
        auto source_index = this->source_index(index);

        auto layer = this->layer(source_index);

        if ( layer && drag_from_layer->is_valid_parent(layer) )
        {
            drag_to_layer = layer;
            drag_to_index = index;
        }
        else
        {
            drag_to_layer = nullptr;
            drag_to_index = {};
        }

        parent->viewport()->update();
    }

    QPoint relpoint()
    {
        return parent->visualRect(parent->model()->index(0, 0)).topLeft();
    }

    TimelineTreeview* parent;
    QPoint drag_from;
    QPoint drag_to;
    QPoint drag_to_local;
    QModelIndex drag_from_index;
    QModelIndex drag_to_index;
    model::Layer* drag_from_layer = nullptr;
    model::Layer* drag_to_layer = nullptr;
    bool dragging = false;
};

glaxnimate::gui::TimelineTreeview::TimelineTreeview(QWidget* parent)
    : QTreeView(parent), d(std::make_unique<Private>())
{
    d->parent = this;
    setIndentation(16);
}

glaxnimate::gui::TimelineTreeview::~TimelineTreeview() = default;

void glaxnimate::gui::TimelineTreeview::mousePressEvent(QMouseEvent* event)
{
    if ( event->button() == Qt::LeftButton )
    {
        auto index = indexAt(event->pos());
        auto source_index = d->source_index(index);

        if ( source_index.column() == item_models::PropertyModelFull::ColumnValue )
        {
            auto layer = d->layer(source_index);
            if ( layer && !layer->docnode_locked_recursive()  )
            {
                d->drag_from_layer = layer;
                d->drag_from = d->drag_to = event->pos() - d->relpoint();
                d->drag_to_local = event->pos();
                d->drag_from_index = index;
                d->drag_to_layer = nullptr;
                d->drag_to_layer = nullptr;
                d->drag_to_index = {};
                d->dragging = true;

                event->accept();
                viewport()->update();
                return;
            }
        }
    }

    QTreeView::mousePressEvent(event);
}

void glaxnimate::gui::TimelineTreeview::mouseMoveEvent(QMouseEvent* event)
{
    if ( d->dragging )
    {
        d->drag_to = event->pos() - d->relpoint();
        d->drag_to_local = event->pos();
        d->on_drag();
    }
    else
    {
        QTreeView::mouseMoveEvent(event);
    }
}

void glaxnimate::gui::TimelineTreeview::mouseReleaseEvent(QMouseEvent* event)
{
    if ( d->dragging && event->button() == Qt::LeftButton )
    {
        d->dragging = false;

        // if moved less than 3 pixels, treat as a click
        if ( math::length_squared<QPointF>(d->drag_to - d->drag_from) < 9 )
        {
            QTreeView::mousePressEvent(event);
            QTreeView::mouseReleaseEvent(event);
        }
        else
        {
            d->drag_from_layer->parent.set_undoable(QVariant::fromValue(d->drag_to_layer));
        }

        d->drag_from_layer = d->drag_to_layer = nullptr;
        d->drag_from_index = d->drag_to_index = {};
        viewport()->update();
    }
    else
    {
        QTreeView::mouseReleaseEvent(event);
    }
}

void glaxnimate::gui::TimelineTreeview::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    int depth = index_depth(index);
    int indent = indentation();
    bool has_children = model()->hasChildren(index);
    bool expanded = isExpanded(index);

    // Draw vertical depth bars for each ancestor level
    for ( int i = 0; i < depth; i++ )
    {
        QColor bar_color = color_for_depth(i);
        bar_color.setAlpha(50);
        int x = rect.left() + i * indent + indent / 2;
        painter->setPen(QPen(bar_color, 1.0));
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }

    // Draw expand/collapse indicator
    if ( has_children )
    {
        QColor arrow_color = color_for_depth(depth);
        int cx = rect.left() + depth * indent + indent / 2;
        int cy = rect.top() + rect.height() / 2;

        painter->setPen(Qt::NoPen);
        painter->setBrush(arrow_color);

        if ( expanded )
        {
            // Down-pointing triangle
            QPolygonF tri;
            tri << QPointF(cx - 4, cy - 2)
                << QPointF(cx + 4, cy - 2)
                << QPointF(cx, cy + 3);
            painter->drawPolygon(tri);
        }
        else
        {
            // Right-pointing triangle
            QPolygonF tri;
            tri << QPointF(cx - 2, cy - 4)
                << QPointF(cx + 3, cy)
                << QPointF(cx - 2, cy + 4);
            painter->drawPolygon(tri);
        }
    }

    painter->restore();
}


void glaxnimate::gui::TimelineTreeview::drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    int depth = index_depth(index);
    QColor depth_col = color_for_depth(depth);
    QRect row_rect = option.rect;
    bool selected = selectionModel()->isSelected(index);
    bool has_children = model()->hasChildren(index);

    // Selected row highlight
    if ( selected )
    {
        QColor sel_color = depth_col;
        sel_color.setAlpha(35);
        painter->fillRect(row_rect, sel_color);
    }

    // Hover highlight (when not selected)
    if ( !selected && option.state & QStyle::State_MouseOver )
    {
        QColor hover_color = palette().color(QPalette::Midlight);
        hover_color.setAlpha(80);
        painter->fillRect(row_rect, hover_color);
    }

    // Left color accent bar — 3px wide, colored by depth
    QRect accent_rect(row_rect.left(), row_rect.top() + 1, 3, row_rect.height() - 2);
    if ( has_children )
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(depth_col);
        painter->drawRoundedRect(accent_rect, 1.5, 1.5);
    }

    // Bottom separator line
    QColor sep_color = palette().color(QPalette::Dark);
    sep_color.setAlpha(40);
    painter->setPen(QPen(sep_color, 0.5));
    painter->drawLine(row_rect.bottomLeft(), row_rect.bottomRight());

    painter->restore();

    // Draw the default content (text, icons, etc.)
    QTreeView::drawRow(painter, option, index);
}


void glaxnimate::gui::TimelineTreeview::paintEvent(QPaintEvent* event)
{
    QTreeView::paintEvent(event);

    if ( d->dragging )
    {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);

        if ( d->drag_to_index.isValid() )
        {
            // Highlight drop target with depth-colored border
            auto rect = visualRect(d->drag_to_index);
            rect.setX(0);
            rect.setWidth(width());

            QColor highlight = color_for_depth(index_depth(d->drag_to_index));
            highlight.setAlpha(100);
            painter.setPen(QPen(highlight, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect.adjusted(1, 1, -1, -1), 4, 4);
        }

        // Drag connection line
        QColor line_col = palette().color(QPalette::Highlight);
        line_col.setAlpha(180);
        painter.setPen(QPen(line_col, 2, Qt::DashLine));
        QPoint off = d->relpoint();
        painter.drawLine(
            d->drag_from + off,
            d->drag_to + off
        );
    }
}

void glaxnimate::gui::TimelineTreeview::scrollContentsBy(int dx, int dy)
{
    QTreeView::scrollContentsBy(dx, dy);

    if ( d->dragging )
    {
        d->drag_to = d->drag_to_local - d->relpoint();
        d->on_drag();
    }
}
