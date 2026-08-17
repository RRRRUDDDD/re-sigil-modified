/************************************************************************
**
**  Copyright (C) 2026 rinne1998, RUD
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#pragma once
#ifndef BOOKBROWSERTREEVIEW_H
#define BOOKBROWSERTREEVIEW_H

#include <QTreeView>
#include <QMouseEvent>
#include <QPersistentModelIndex>

class QLabel;
class QTimer;
class Resource;

class BookBrowserTreeView : public QTreeView
{
    Q_OBJECT

public:
    BookBrowserTreeView(QWidget* parent = nullptr);
    ~BookBrowserTreeView();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    QPersistentModelIndex imagePreviewIndex;
    QLabel* imagePreviewPopup;
    QTimer* imagePreviewTimer;

    void scheduleImagePreview(const QModelIndex& index);
    void showImagePreview();
    void hideImagePreview();
    Resource* resourceForIndex(const QModelIndex& index) const;
};

#endif // BOOKBROWSERTREEVIEW_H
