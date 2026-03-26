/*
 * SPDX-FileCopyrightText: 2019-2023 Mattia Basaglia <dev@dragon.best>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <memory>
#include <QTreeView>

namespace glaxnimate::gui {

class TimelineTreeview : public QTreeView
{
public:
    explicit TimelineTreeview(QWidget* parent = nullptr);
    ~TimelineTreeview();

protected:
    void mousePressEvent(QMouseEvent * event) override;
    void mouseMoveEvent(QMouseEvent * event) override;
    void mouseReleaseEvent(QMouseEvent * event) override;
    void paintEvent(QPaintEvent * event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace glaxnimate::gui
