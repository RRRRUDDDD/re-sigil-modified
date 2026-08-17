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

#include <QHash>
#include <QString>
#include <QStringList>

#include "Misc/SearchBatchRunner.h"

class MainWindow;
class TextResource;

class SearchBatchCoordinator final
{
public:
    struct Snapshot {
        QStringList resourcePaths;
        QHash<QString, QString> originalTexts;
        QHash<QString, QString> mediaTypes;
    };

    // GUI-thread boundary: flush open tabs and copy all worker input.
    static bool CaptureSnapshot(MainWindow* main_window,
                                const QStringList& ordered_paths,
                                const QHash<QString, TextResource*>& resources,
                                Snapshot& snapshot,
                                QString* error = nullptr);

    // GUI-thread boundary: conflict check, then one undoable write per changed
    // resource. The staged result is never recomputed here.
    static SearchBatch::Result CommitStagedResult(
        MainWindow* main_window,
        const QHash<QString, TextResource*>& resources,
        const Snapshot& snapshot,
        const SearchBatch::Result& staged_result);

    // Verify that a worker result still describes the current resource set.
    static bool ResourcesMatchSnapshot(const QHash<QString, TextResource*>& resources,
                                       const Snapshot& snapshot,
                                       QString* conflict_path = nullptr);

    // Compatibility wrapper for saved-search batches.
    static SearchBatch::Result Run(MainWindow* main_window,
                                   const QList<SearchBatch::Rule>& rules,
                                   const QHash<QString, TextResource*>& resources,
                                   const SearchBatch::ApplyFunction& apply);

};
