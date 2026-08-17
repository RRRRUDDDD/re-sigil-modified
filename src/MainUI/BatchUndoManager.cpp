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

#include "MainUI/BatchUndoManager.h"

#include <QSet>
#include <QWriteLocker>
#include <QCryptographicHash>

#include "ResourceObjects/TextResource.h"

static QByteArray ContentDigest(TextResource *resource)
{
    if (resource == nullptr || !resource->IsLoaded()) {
        return QByteArray();
    }
    return QCryptographicHash::hash(resource->GetText().toUtf8(),
                                    QCryptographicHash::Sha256);
}

void BatchUndoManager::RegisterGroup(const QList<TextResource *> &resources,
                                     const QList<TextResource *> &tracked_resources)
{
    // Any new edit invalidates redo groups, including an edit that does
    // not leave an undoable step to register.
    m_RedoGroups.clear();

    Group group;
    QSet<TextResource *> seen;
    for (TextResource *resource : resources) {
        if (resource == nullptr || seen.contains(resource)) {
            continue;
        }
        seen.insert(resource);

        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        const int undoSteps = document.availableUndoSteps();
        if (undoSteps <= 0) {
            continue;
        }

        Entry entry;
        entry.resource = resource;
        entry.undoSteps = undoSteps;
        entry.redoSteps = document.availableRedoSteps();
        entry.undoResetGeneration = resource->GetUndoResetGeneration();
        entry.wasLoaded = resource->IsLoaded();
        entry.contentDigest = ContentDigest(resource);
        group.entries.append(entry);
    }

    QSet<TextResource *> tracked_seen;
    const QList<TextResource *> resources_to_track = tracked_resources.isEmpty()
        ? resources
        : tracked_resources;
    for (TextResource *resource : resources_to_track) {
        if (resource == nullptr || seen.contains(resource) || tracked_seen.contains(resource)) {
            continue;
        }
        tracked_seen.insert(resource);

        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        ResourceState state;
        state.resource = resource;
        state.undoSteps = document.availableUndoSteps();
        state.redoSteps = document.availableRedoSteps();
        state.undoResetGeneration = resource->GetUndoResetGeneration();
        state.wasLoaded = resource->IsLoaded();
        state.contentDigest = ContentDigest(resource);
        group.tracked.append(state);
    }

    if (!group.entries.isEmpty()) {
        m_UndoGroups.append(group);
    }
}

bool BatchUndoManager::CanUndoGroup() const
{
    return !m_UndoGroups.isEmpty() && CanApplyGroup(m_UndoGroups.constLast(), true);
}

bool BatchUndoManager::CanRedoGroup() const
{
    return !m_RedoGroups.isEmpty() && CanApplyGroup(m_RedoGroups.constLast(), false);
}

QList<TextResource *> BatchUndoManager::UndoGroup()
{
    return ApplyGroup(true);
}

QList<TextResource *> BatchUndoManager::RedoGroup()
{
    return ApplyGroup(false);
}

TextResource *BatchUndoManager::UndoResourceBeforeGroup() const
{
    if (m_UndoGroups.isEmpty()) {
        return nullptr;
    }

    const Group &group = m_UndoGroups.constLast();
    TextResource *latest_resource = nullptr;
    quint64 latest_revision = 0;
    QSet<TextResource *> members;
    for (const Entry &entry : group.entries) {
        members.insert(entry.resource.data());
    }

    for (const ResourceState &state : group.tracked) {
        TextResource *resource = state.resource.data();
        if (resource == nullptr || members.contains(resource)) {
            continue;
        }
        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        const bool content_changed = state.wasLoaded &&
            (!resource->IsLoaded() || ContentDigest(resource) != state.contentDigest);
        if (resource->GetUndoResetGeneration() == state.undoResetGeneration &&
            (document.availableUndoSteps() > state.undoSteps ||
             (document.availableUndoSteps() == state.undoSteps &&
              content_changed && document.isUndoAvailable()))) {
            const quint64 revision = resource->GetEditRevision();
            if (latest_resource == nullptr || revision > latest_revision) {
                latest_resource = resource;
                latest_revision = revision;
            }
        }
    }

    for (const Entry &entry : group.entries) {
        TextResource *resource = entry.resource.data();
        if (resource == nullptr) {
            continue;
        }
        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        const bool content_changed = entry.wasLoaded &&
            (!resource->IsLoaded() || ContentDigest(resource) != entry.contentDigest);
        if (resource->GetUndoResetGeneration() == entry.undoResetGeneration &&
            (document.availableUndoSteps() > entry.undoSteps ||
             (document.availableUndoSteps() == entry.undoSteps &&
              content_changed && document.isUndoAvailable()))) {
            const quint64 revision = resource->GetEditRevision();
            if (latest_resource == nullptr || revision > latest_revision) {
                latest_resource = resource;
                latest_revision = revision;
            }
        }
    }

    return latest_resource;
}

TextResource *BatchUndoManager::RedoResourceBeforeGroup() const
{
    if (m_RedoGroups.isEmpty()) {
        return nullptr;
    }

    const Group &group = m_RedoGroups.constLast();
    TextResource *latest_resource = nullptr;
    quint64 latest_revision = 0;
    for (const ResourceState &state : group.tracked) {
        TextResource *resource = state.resource.data();
        if (resource == nullptr) {
            continue;
        }
        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        if (resource->GetUndoResetGeneration() == state.undoResetGeneration &&
            document.availableUndoSteps() < state.undoSteps &&
            document.availableRedoSteps() > state.redoSteps) {
            const quint64 revision = resource->GetEditRevision();
            if (latest_resource == nullptr || revision > latest_revision) {
                latest_resource = resource;
                latest_revision = revision;
            }
        }
    }
    for (const Entry &entry : group.entries) {
        TextResource *resource = entry.resource.data();
        if (resource == nullptr) {
            continue;
        }
        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        if (resource->GetUndoResetGeneration() == entry.undoResetGeneration &&
            document.availableUndoSteps() < entry.undoSteps &&
            document.availableRedoSteps() > entry.redoSteps) {
            const quint64 revision = resource->GetEditRevision();
            if (latest_resource == nullptr || revision > latest_revision) {
                latest_resource = resource;
                latest_revision = revision;
            }
        }
    }
    return latest_resource;
}

void BatchUndoManager::Clear()
{
    m_UndoGroups.clear();
    m_RedoGroups.clear();
}

bool BatchUndoManager::CanApplyGroup(const Group &group, bool undo) const
{
    if (group.entries.isEmpty()) {
        return false;
    }

    for (const Entry &entry : group.entries) {
        TextResource *resource = entry.resource.data();
        if (resource == nullptr) {
            return false;
        }

        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        if (resource->GetUndoResetGeneration() != entry.undoResetGeneration) {
            return false;
        }
        if (entry.wasLoaded &&
            (!resource->IsLoaded() || ContentDigest(resource) != entry.contentDigest)) {
            return false;
        }
        const int expectedSteps = undo ? entry.undoSteps : entry.redoSteps;
        const int availableSteps = undo ? document.availableUndoSteps()
                                        : document.availableRedoSteps();
        if (availableSteps != expectedSteps) {
            return false;
        }

        if (undo ? !document.isUndoAvailable() : !document.isRedoAvailable()) {
            return false;
        }
    }

    QSet<TextResource *> members;
    for (const Entry &entry : group.entries) {
        members.insert(entry.resource.data());
    }
    for (const ResourceState &state : group.tracked) {
        TextResource *resource = state.resource.data();
        if (resource == nullptr) {
            return false;
        }
        if (members.contains(resource)) {
            continue;
        }

        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        if (resource->GetUndoResetGeneration() != state.undoResetGeneration) {
            return false;
        }
        if (state.wasLoaded &&
            (!resource->IsLoaded() || ContentDigest(resource) != state.contentDigest)) {
            return false;
        }
        if (undo) {
            // A newer ordinary edit may be undone before the batch group is
            // eligible again, so redo depth is intentionally ignored here.
            if (document.availableUndoSteps() != state.undoSteps) {
                return false;
            }
        } else if (document.availableUndoSteps() != state.undoSteps ||
                   document.availableRedoSteps() != state.redoSteps) {
            return false;
        }
    }

    return true;
}

QList<TextResource *> BatchUndoManager::ApplyGroup(bool undo)
{
    QVector<Group> &source = undo ? m_UndoGroups : m_RedoGroups;
    QVector<Group> &destination = undo ? m_RedoGroups : m_UndoGroups;
    if (source.isEmpty() || !CanApplyGroup(source.constLast(), undo)) {
        return QList<TextResource *>();
    }

    // Work on a copy until every operation succeeds.  The stack is only
    // moved after a complete group has been applied. If a later resource
    // fails its recheck or operation, restore every resource already changed
    // before returning failure.
    Group updated = source.constLast();
    QList<TextResource *> processed;

    const auto rollbackProcessed = [this, undo, &processed]() {
        bool restored = true;
        for (auto it = processed.crbegin(); it != processed.crend(); ++it) {
            TextResource *resource = *it;
            if (resource == nullptr) {
                restored = false;
                continue;
            }

            QWriteLocker locker(&resource->GetLock());
            const bool applied = undo ? resource->RedoLastEdit()
                                      : resource->UndoLastEdit();
            restored = applied && restored;
        }

        if (!restored) {
            // The documents no longer match either managed state, so no
            // remaining group can be applied safely.
            m_UndoGroups.clear();
            m_RedoGroups.clear();
        }
    };

    for (int index = 0; index < updated.entries.size(); ++index) {
        Entry &entry = updated.entries[index];
        TextResource *resource = entry.resource.data();
        if (resource == nullptr) {
            rollbackProcessed();
            return QList<TextResource *>();
        }

        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        const int expectedSteps = undo ? entry.undoSteps : entry.redoSteps;
        const int availableSteps = undo ? document.availableUndoSteps()
                                        : document.availableRedoSteps();
        if (availableSteps != expectedSteps) {
            // The preflight check normally catches this.  Recheck while the
            // resource is locked to avoid applying a stale group.
            locker.unlock();
            rollbackProcessed();
            return QList<TextResource *>();
        }

        const bool applied = undo ? resource->UndoLastEdit() : resource->RedoLastEdit();
        if (!applied) {
            locker.unlock();
            rollbackProcessed();
            return QList<TextResource *>();
        }

        entry.undoSteps = document.availableUndoSteps();
        entry.redoSteps = document.availableRedoSteps();
        entry.undoResetGeneration = resource->GetUndoResetGeneration();
        entry.wasLoaded = resource->IsLoaded();
        entry.contentDigest = ContentDigest(resource);
        processed.append(resource);
    }

    for (ResourceState &state : updated.tracked) {
        TextResource *resource = state.resource.data();
        if (resource == nullptr) {
            rollbackProcessed();
            return QList<TextResource *>();
        }
        QWriteLocker locker(&resource->GetLock());
        TextDocument &document = resource->GetTextDocumentForWriting();
        state.undoSteps = document.availableUndoSteps();
        state.redoSteps = document.availableRedoSteps();
        state.undoResetGeneration = resource->GetUndoResetGeneration();
        state.wasLoaded = resource->IsLoaded();
        state.contentDigest = ContentDigest(resource);
    }

    source.removeLast();
    destination.append(updated);
    return processed;
}
