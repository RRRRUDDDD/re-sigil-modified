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

#include <QApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

#include "BookBrowserTreeView.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "Misc/ImagePreviewService.h"
#include "Misc/Utility.h"
#include "ResourceObjects/Resource.h"

static const int IMAGE_PREVIEW_DELAY_MS = 150;

BookBrowserTreeView::BookBrowserTreeView(QWidget* parent)
    :
    QTreeView(parent),
    imagePreviewIndex(QModelIndex()),
    imagePreviewPopup(new QLabel(nullptr, Qt::ToolTip)),
    imagePreviewTimer(new QTimer(this))
{
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    imagePreviewPopup->setAttribute(Qt::WA_ShowWithoutActivating);
    imagePreviewPopup->setAlignment(Qt::AlignCenter);
    imagePreviewPopup->setStyleSheet("QLabel { background: palette(base); border: 1px solid palette(mid); padding: 4px; }");
    imagePreviewPopup->hide();
    imagePreviewTimer->setSingleShot(true);
    connect(imagePreviewTimer, &QTimer::timeout, this, [this]() { showImagePreview(); });
}

BookBrowserTreeView::~BookBrowserTreeView()
{
    delete imagePreviewPopup;
}

Resource* BookBrowserTreeView::resourceForIndex(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return nullptr;
    }

    const QString identifier = index.data(Qt::UserRole + 1).toString();
    if (identifier.isEmpty()) {
        return nullptr;
    }

    MainWindow* mainwin = qobject_cast<MainWindow*>(Utility::GetMainWindow());
    if (!mainwin || mainwin->GetCurrentBook().isNull()) {
        return nullptr;
    }

    return mainwin->GetCurrentBook()->GetFolderKeeper()->GetResourceByIdentifier(identifier);
}

static QString formatPreviewFileSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + QStringLiteral(" B");
    }

    constexpr qint64 KB = 1024;
    constexpr qint64 MB = KB * 1024;
    constexpr qint64 GB = MB * 1024;

    if (bytes < MB) {
        const double size = bytes / static_cast<double>(KB);
        return QString::number(size, 'f', size < 10.0 ? 1 : 0) + QStringLiteral(" KB");
    }
    if (bytes < GB) {
        const double size = bytes / static_cast<double>(MB);
        return QString::number(size, 'f', size < 10.0 ? 1 : 0) + QStringLiteral(" MB");
    }
    const double size = bytes / static_cast<double>(GB);
    return QString::number(size, 'f', size < 10.0 ? 1 : 0) + QStringLiteral(" GB");
}

static QString formatPreviewInfo(const QSize& pixel_size, qint64 file_size)
{
    QString dimensions = pixel_size.isEmpty() ?
                         QString("Unknown px") :
                         QString("%1 x %2 px").arg(pixel_size.width()).arg(pixel_size.height());
    return QString("%1 | %2").arg(dimensions, formatPreviewFileSize(file_size));
}

static QPixmap imagePreviewWithInfoBar(const ImagePreviewData& preview,
                                       const QFont& font,
                                       const QPalette& palette)
{
    const QString info = formatPreviewInfo(preview.pixelSize, preview.fileSize);
    const QFontMetrics fm(font);
    const int horizontal_padding = 12;
    const int info_height = fm.height() + 10;
    const int width = qMax(preview.image.width(), fm.horizontalAdvance(info) + horizontal_padding * 2);
    const int height = preview.image.height() + info_height;

    QPixmap pixmap(width, height);
    pixmap.fill(palette.base().color());

    QPainter painter(&pixmap);
    const int image_x = (width - preview.image.width()) / 2;
    painter.drawImage(image_x, 0, preview.image);

    const QRect info_rect(0, preview.image.height(), width, info_height);
    painter.fillRect(info_rect, palette.window().color());
    painter.setPen(palette.mid().color());
    painter.drawLine(info_rect.topLeft(), info_rect.topRight());
    painter.setFont(font);
    painter.setPen(palette.text().color());
    painter.drawText(info_rect.adjusted(horizontal_padding, 0, -horizontal_padding, 0),
                     Qt::AlignCenter,
                     info);
    return pixmap;
}

static QPoint imagePreviewPopupPosition(const QRect& anchor_global,
                                        const QSize& popup_size)
{
    constexpr int kGap = 12;
    constexpr int kMargin = 8;

    QScreen* screen = QGuiApplication::screenAt(anchor_global.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return QPoint(anchor_global.right() + kGap, anchor_global.top());
    }

    const QRect available = screen->availableGeometry().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    const int available_right = available.x() + available.width();
    const int available_bottom = available.y() + available.height();
    const int max_x = available.x() + qMax(0, available.width() - popup_size.width());
    const int max_y = available.y() + qMax(0, available.height() - popup_size.height());

    int x = anchor_global.right() + kGap;
    if (x + popup_size.width() > available_right) {
        const int left_x = anchor_global.left() - kGap - popup_size.width();
        x = (left_x >= available.x()) ? left_x : max_x;
    }
    x = qBound(available.x(), x, max_x);

    int y = anchor_global.top();
    if (y + popup_size.height() > available_bottom) {
        y = max_y;
    }
    y = qBound(available.y(), y, max_y);

    return QPoint(x, y);
}

void BookBrowserTreeView::scheduleImagePreview(const QModelIndex& index)
{
    Resource* resource = resourceForIndex(index);
    if (!resource ||
        (resource->Type() != Resource::ImageResourceType &&
         resource->Type() != Resource::SVGResourceType)) {
        hideImagePreview();
        return;
    }

    if (imagePreviewIndex == index &&
        (imagePreviewPopup->isVisible() || imagePreviewTimer->isActive())) {
        return;
    }

    imagePreviewIndex = index;
    imagePreviewPopup->hide();
    imagePreviewTimer->start(IMAGE_PREVIEW_DELAY_MS);
}

void BookBrowserTreeView::showImagePreview()
{
    Resource* resource = resourceForIndex(imagePreviewIndex);
    if (!resource ||
        (resource->Type() != Resource::ImageResourceType &&
         resource->Type() != Resource::SVGResourceType)) {
        hideImagePreview();
        return;
    }

    QString imagePath = resource->GetFullPath();
    if (imagePath.isEmpty()) {
        hideImagePreview();
        return;
    }

    // Load image synchronously
    QImage image;
    if (resource->Type() == Resource::SVGResourceType) {
        // For SVG, we need to render it
        QByteArray svg_data = Utility::ReadUnicodeTextFile(imagePath).toUtf8();
        if (svg_data.isEmpty()) {
            hideImagePreview();
            return;
        }
        // Simple SVG rendering - could be enhanced
        QPixmap pixmap;
        pixmap.loadFromData(svg_data, "SVG");
        image = pixmap.toImage();
    } else {
        // For bitmap images
        image.load(imagePath);
    }

    if (image.isNull()) {
        hideImagePreview();
        return;
    }

    // Scale image to preview size (max 300px on longest side)
    QSize originalSize = image.size();
    const int maxSide = 300;
    if (image.width() > maxSide || image.height() > maxSide) {
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Create preview data structure
    ImagePreviewData preview;
    preview.image = image;
    preview.pixelSize = originalSize;  // Store original size, not scaled size
    preview.fileSize = QFileInfo(imagePath).size();  // Get actual file size

    QPixmap pixmap = imagePreviewWithInfoBar(preview,
                                             imagePreviewPopup->font(),
                                             imagePreviewPopup->palette());
    imagePreviewPopup->setPixmap(pixmap);
    imagePreviewPopup->adjustSize();

    const QRect item_rect = visualRect(imagePreviewIndex);
    const QRect anchor_global(viewport()->mapToGlobal(item_rect.topLeft()), item_rect.size());
    const QPoint pos = imagePreviewPopupPosition(anchor_global, imagePreviewPopup->size());
    imagePreviewPopup->move(pos);
    imagePreviewPopup->show();
}

void BookBrowserTreeView::hideImagePreview()
{
    imagePreviewTimer->stop();
    imagePreviewIndex = QPersistentModelIndex();
    if (imagePreviewPopup) {
        imagePreviewPopup->hide();
    }
}

void BookBrowserTreeView::mousePressEvent(QMouseEvent* event)
{
    hideImagePreview();
    QTreeView::mousePressEvent(event);
}

void BookBrowserTreeView::mouseMoveEvent(QMouseEvent* event)
{
    QTreeView::mouseMoveEvent(event);

    if (event->buttons() == Qt::NoButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        scheduleImagePreview(indexAt(event->position().toPoint()));
#else
        scheduleImagePreview(indexAt(event->pos()));
#endif
    }
}

void BookBrowserTreeView::leaveEvent(QEvent* event)
{
    hideImagePreview();
    QTreeView::leaveEvent(event);
}

void BookBrowserTreeView::scrollContentsBy(int dx, int dy)
{
    hideImagePreview();
    QTreeView::scrollContentsBy(dx, dy);
}
