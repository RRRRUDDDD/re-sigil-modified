/************************************************************************
**
**  Copyright (C) 2019 Kevin B. Hendricks, Stratford, Ontario Canada
**  Copyright (C) 2009, 2010, 2011  Strahinja Markovic  <strahinja.markovic@gmail.com>
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
#ifndef TEXTRESOURCE_H
#define TEXTRESOURCE_H

#include <QtCore/QMutex>
#include <QtCore/QtGlobal>
#include "Widgets/TextDocument.h"
#include "ResourceObjects/Resource.h"


class TextDocument;

/**
 * A parent class for textual resources like CSS and SVG images.
 * Takes care of loading and caching content etc.
 */
class TextResource : public Resource
{
    Q_OBJECT

public:

    /**
     * Constructor.
     *
     * @param fullfilepath The full path to the file that this
     *                     resource is representing.
     * @param parent The object's parent.
     */
    TextResource(const QString &mainfolder, const QString &fullfilepath, QObject *parent = NULL);

    /**
     * Returns the text stored in the resource.
     *
     * @return The resource text.
     */
    virtual QString GetText() const;

    /**
     * Sets the text of the resource, replacing the stored content.
     */
    virtual void SetText(const QString &text);

    /**
     * Replaces the text as one QTextDocument undo command. This must be
     * called on the GUI thread and is intended for user-visible edits.
     */
    virtual bool SetTextAsUndoableEdit(const QString &text);

    /**
     * Undoes the most recent text edit, if one is available.
     *
     * @return True when an undo command was applied.
     */
    virtual bool UndoLastEdit();

    /**
     * Redoes the most recently undone text edit, if one is available.
     *
     * @return True when a redo command was applied.
     */
    virtual bool RedoLastEdit();

    /**
     * Returns a reference to the QTextDocument that can be read and written to
     * in consumers. If you need just read access, use GetTextDocumentForReading().
     *
     * @warning Make sure to get a write lock externally before calling this function!
     *
     * @return A reference to the QTextDocument cache.
     */
    TextDocument &GetTextDocumentForWriting();

    // inherited
    void SaveToDisk(bool book_wide_save = false);

    /**
     * Loads the text content into the QTextDocument cache if
     * nothing has been loaded so far. This is not done automatically
     * because we want to do loading on demand (for performance reasons).
     */
    virtual void InitialLoad();

    bool IsLoaded();

    /**
     * Returns the generation of non-undoable document resets.  A reset
     * invalidates a batch edit because QTextDocument cannot restore it via
     * its undo stack.
     */
    quint64 GetUndoResetGeneration() const;

    /**
     * Returns the process-wide sequence assigned to this resource's most
     * recent document change.
     */
    quint64 GetEditRevision() const;

    // inherited
    virtual ResourceType Type() const;

protected:
    virtual bool LoadFromDisk();

private slots:

    /**
     * Performs the delayed update of m_TextDocument with the text
     * stored in m_Cache.
     */
    void DelayedUpdateToTextDocument();

private:

    /**
     * Actually sets the text to m_TextDocument.
     *
     * @param text The text to set.
     */
    void SetTextInternal(const QString &text);


    ///////////////////////////////
    // PRIVATE MEMBER VARIABLES
    ///////////////////////////////

    /**
     * If \c true, then the m_Cache var is holding cached text.
     */
    bool m_CacheInUse;

    /**
     * The cached text used when threads are in use. @see SetText() internals.
     */
    QString m_Cache;

    /**
     * The access mutex for the cache.
     */
    mutable QMutex m_CacheAccessMutex;

    /**
     * The syntax colored cache of the TextResource text content.
     */
    TextDocument *m_TextDocument;

    bool m_IsLoaded;

    quint64 m_UndoResetGeneration;

    quint64 m_EditRevision;
};

#endif // TEXTRESOURCE_H

