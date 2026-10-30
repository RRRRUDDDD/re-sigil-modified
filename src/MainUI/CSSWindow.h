/************************************************************************
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
#ifndef CSSWINDOW_H
#define CSSWINDOW_H

#include <QtCore/QList>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtWidgets/QDockWidget>

class QComboBox;
class QHideEvent;
class QLabel;
class QTimer;
class Book;
class CSSResource;
class CodeViewEditor;
class TextDocument;

/**
 * A dockable editor for the book's CSS files. It edits the resource's own
 * TextDocument and writes pending changes back to disk after a short delay.
 */
class CSSWindow : public QDockWidget
{
    Q_OBJECT

public:

    explicit CSSWindow(QWidget *parent = nullptr);
    ~CSSWindow();

    /**
     * Rebinds the window to a new book and reloads its CSS list.
     */
    void SetBook(Book *book);

    /**
     * Writes any pending edit to disk immediately.
     */
    void FlushPendingChanges();

public slots:

    /**
     * Rebuilds the stylesheet list, keeping the current selection when it
     * still exists.
     */
    void RefreshCSSList();

    void LoadSelectedCSS();

signals:

    /**
     * Emitted after the current stylesheet has been written to disk so the
     * preview can be refreshed.
     */
    void CSSSaved();

protected:

    virtual void showEvent(QShowEvent *event);
    virtual void hideEvent(QHideEvent *event);

private slots:

    void OnDocumentContentsChanged();
    void FlushPendingSave();
    void UpdateStatusLabel();

private:

    void BindResource(CSSResource *resource);
    void DetachDocument();

    QComboBox *m_CSSSelector;
    CodeViewEditor *m_Editor;
    TextDocument *m_EmptyDocument;
    QPointer<TextDocument> m_BoundDocument;
    QLabel *m_StatusLabel;
    QTimer *m_SaveTimer;
    QList<QPointer<CSSResource>> m_CSSResources;
    QPointer<Book> m_Book;
    QPointer<CSSResource> m_CurrentResource;
    QString m_SyncedText;
    bool m_Loading;
    bool m_ChangePending;
};

#endif // CSSWINDOW_H
