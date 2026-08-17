/************************************************************************
**
**  Copyright (C) 2026
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#pragma once

#include <QList>
#include <QByteArray>
#include <QPointer>
#include <QVector>
#include <QtGlobal>

class TextResource;

/**
 * Keeps undo and redo groups that span more than one text resource.
 *
 * A group stores the expected document undo/redo stack sizes for every
 * participating resource.  This lets the manager reject a group if a
 * resource was changed independently after it was registered.
 */
class BatchUndoManager final
{
public:
    void RegisterGroup(const QList<TextResource *> &resources,
                       const QList<TextResource *> &tracked_resources = QList<TextResource *>());

    bool CanUndoGroup() const;
    bool CanRedoGroup() const;

    QList<TextResource *> UndoGroup();
    QList<TextResource *> RedoGroup();

    /**
     * Returns a resource whose ordinary undo should be applied before the
     * pending batch group.  This lets a global Undo action preserve edits in
     * a different tab instead of undoing a batch member prematurely.
     */
    TextResource *UndoResourceBeforeGroup() const;

    /**
     * Returns a resource whose ordinary redo should be applied before the
     * pending batch redo group, when such a recoverable edit exists.
     */
    TextResource *RedoResourceBeforeGroup() const;

    void Clear();

private:
    struct Entry {
        QPointer<TextResource> resource;
        int undoSteps = 0;
        int redoSteps = 0;
        quint64 undoResetGeneration = 0;
        QByteArray contentDigest;
        bool wasLoaded = false;
    };

    struct ResourceState {
        QPointer<TextResource> resource;
        int undoSteps = 0;
        int redoSteps = 0;
        quint64 undoResetGeneration = 0;
        QByteArray contentDigest;
        bool wasLoaded = false;
    };

    struct Group {
        QVector<Entry> entries;
        QVector<ResourceState> tracked;
    };

    bool CanApplyGroup(const Group &group, bool undo) const;
    QList<TextResource *> ApplyGroup(bool undo);

    QVector<Group> m_UndoGroups;
    QVector<Group> m_RedoGroups;
};
