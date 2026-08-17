/************************************************************************
**
**  Copyright (C) 2016-2021 Kevin B. Hendricks, Stratford, Ontario, Canada
**  Copyright (C) 2013      Dave Heiland
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

#include <algorithm>
#include <functional>

#include <QtCore/QStringList>
#include <QtGui/QStandardItem>
#include <QItemSelection>
#include <QItemSelectionRange>
#include <QKeyEvent>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QtAlgorithms>

#include "BookManipulation/Book.h"
#include "Dialogs/EditTOC.h"
#include "Dialogs/SelectHyperlink.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NavProcessor.h"
#include "ResourceObjects/Resource.h"
#include "sigil_constants.h"

static const QString SETTINGS_GROUP   = "edit_toc";
static const int COLUMN_INDENTATION = 20;

EditTOC::EditTOC(QSharedPointer<Book> book, QList<Resource *> resources, QWidget *parent)
    :
    QDialog(parent),
    m_Book(book),
    m_Resources(resources),
    m_TableOfContents(new QStandardItemModel(this)),
    m_ContextMenu(new QMenu(this)),
    m_TOCModel(new TOCModel(this)),
    m_BaseResource(NULL)
{
    // first determine the base resource pointer we will be working with
    //  is it the ncx or the nav
    QString version = m_Book->GetConstOPF()->GetEpubVersion();
    if (version.startsWith("3")) {
        m_BaseResource = m_Book->GetConstOPF()->GetNavResource();
    } else {
        m_BaseResource = m_Book->GetNCX();
    }

    // Remove the Nav resource from list of HTMLResources if it exists (EPUB3)
    HTMLResource* nav_resource = m_Book->GetConstOPF()->GetNavResource();
    if (nav_resource) {
        m_Resources.removeOne(nav_resource);
    }

    ui.setupUi(this);
    ui.TOCTree->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.TOCTree->installEventFilter(this);
    ui.TOCTree->setModel(m_TableOfContents);
    ui.TOCTree->setIndentation(COLUMN_INDENTATION);
    ui.TOCTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui.TOCTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    CreateContextMenuActions();
    ConnectSignalsToSlots();

    CreateTOCModel();
    UpdateTreeViewDisplay();
    ReadSettings();
    QTimer::singleShot(0, this, SLOT(MakeDefaultFirstSelection()));
}

EditTOC::~EditTOC()
{
    WriteSettings();
}

void EditTOC::UpdateTreeViewDisplay()
{
    ui.TOCTree->expandAll();
}

void EditTOC::CreateTOCModel()
{
    m_TOCModel->SetBook(m_Book);

    TOCModel::TOCEntry toc_entry = m_TOCModel->GetRootTOCEntry();

    m_TableOfContents->clear();
    QStringList header;
    header.append(tr("TOC Entry"));
    header.append(tr("Target"));
    m_TableOfContents->setHorizontalHeaderLabels(header);

    BuildModel(toc_entry);
}

void EditTOC::Save()
{
    QString version = m_Book->GetConstOPF()->GetEpubVersion();
    if (version.startsWith('3')) {
        NavProcessor navproc(m_Book->GetConstOPF()->GetNavResource());
        navproc.GenerateNavTOCFromTOCEntries(ConvertTableToEntries());
    } else {
        // this is safe as all epub2's must hve an ncx (if not we made one for them)
        m_Book->GetNCX()->GenerateNCXFromTOCEntries(m_Book.data(), ConvertTableToEntries());
    }
}

TOCModel::TOCEntry EditTOC::ConvertTableToEntries()
{
    return ConvertItemToEntry(m_TableOfContents->invisibleRootItem());
}

TOCModel::TOCEntry EditTOC::ConvertItemToEntry(QStandardItem *item)
{
    TOCModel::TOCEntry entry;

    if (item != m_TableOfContents->invisibleRootItem()) {
        entry.text = item->text();
        QStandardItem *parent_item = item->parent();
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }
        entry.target = parent_item->child(item->row(), 1)->text();
    } else {
        entry.is_root = true;
    }

    if (!item->hasChildren()) {
        return entry;
    }

    for (int row = 0; row < item->rowCount(); row++) {
        entry.children.append(ConvertItemToEntry(item->child(row, 0)));
    }
    return entry;
}

void EditTOC::ExpandChildren(QStandardItem *item)
{
    QModelIndexList indexes;

    if (item->hasChildren()) {
        for (int i = 0; i < item->rowCount(); i++) {
            ExpandChildren(item->child(i, 0));
        }
    }

    ui.TOCTree->expand(item->index());
}

void EditTOC::ReselectAndExpandItems(const QList<QStandardItem *> &items)
{
    ui.TOCTree->selectionModel()->clear();
    QItemSelection selection;
    QList<QStandardItem *> valid_items;

    foreach(QStandardItem *item, items) {
        QModelIndex index = item->index();
        if (!index.isValid()) {
            continue;
        }
        valid_items << item;

        int row = item->row();
        QModelIndex parent_index = index.parent();
        QModelIndex top_left = m_TableOfContents->index(row, 0, parent_index);
        QModelIndex bottom_right = m_TableOfContents->index(row, 1, parent_index);
        selection.select(top_left, bottom_right);
    }

    if (!selection.isEmpty()) {
        ui.TOCTree->selectionModel()->select(selection,
                                             QItemSelectionModel::Select | QItemSelectionModel::Current);
    }

    if (!valid_items.isEmpty() && valid_items.first()->index().isValid()) {
        ui.TOCTree->selectionModel()->setCurrentIndex(valid_items.first()->index(),
                                                      QItemSelectionModel::NoUpdate);
    }

    foreach(QStandardItem *item, valid_items) {
        ExpandChildren(item);
    }
}

void EditTOC::MoveLeft()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }

    // Build selected_items once for later reselection
    QList<QStandardItem *> selected_items;
    QItemSelection selection = ui.TOCTree->selectionModel()->selection();

    struct SelectedRange {
        QStandardItem *parent;
        QList<QStandardItem *> items;
    };
    QList<SelectedRange> selected_ranges;

    foreach(const QItemSelectionRange &range, selection) {
        QModelIndex parent = range.parent();
        if (!parent.isValid()) {
            continue;
        }

        QStandardItem *parent_item = m_TableOfContents->itemFromIndex(parent);
        if (!parent_item) {
            continue;
        }

        QList<QStandardItem *> range_items;
        for (int row = range.top(); row <= range.bottom(); row++) {
            QStandardItem *item = parent_item->child(row, 0);
            range_items << item;
            selected_items << item;  // Accumulate all items for reselection
        }
        selected_ranges << SelectedRange { parent_item, range_items };
    }

    auto item_depth = [](QStandardItem *item) {
        int depth = 0;
        while (item) {
            depth++;
            item = item->parent();
        }
        return depth;
    };
    std::stable_sort(selected_ranges.begin(), selected_ranges.end(),
                     [&item_depth](const SelectedRange &left, const SelectedRange &right) {
        int left_depth = item_depth(left.parent);
        int right_depth = item_depth(right.parent);
        if (left_depth != right_depth) {
            return left_depth > right_depth;
        }
        if (left.parent == right.parent) {
            return left.items.first()->row() > right.items.first()->row();
        }
        return std::less<QStandardItem *>()(left.parent, right.parent);
    });

    foreach(const SelectedRange &range, selected_ranges) {
        QStandardItem *parent_item = range.parent;

        QStandardItem *grandparent_item = parent_item->parent();
        if (!grandparent_item) {
            grandparent_item = m_TableOfContents->invisibleRootItem();
        }

        int row_to_put = parent_item->row() + 1;
        foreach(QStandardItem *item, range.items) {
            QList<QStandardItem *> row_items = parent_item->takeRow(item->row());
            grandparent_item->insertRow(row_to_put, row_items);
            row_to_put++;
        }
    }

    ReselectAndExpandItems(selected_items);
}

void EditTOC::MoveRight()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }

    // Build selected_items once for later reselection
    QList<QStandardItem *> selected_items;
    QItemSelection selection = ui.TOCTree->selectionModel()->selection();
    QList<QStandardItem *> parent_items;
    QList<QStandardItem *> new_parent_items;
    QList<QList<QStandardItem *> > selected_ranges;

    foreach(const QItemSelectionRange &range, selection) {
        QModelIndex parent = range.parent();
        QStandardItem *parent_item = m_TableOfContents->itemFromIndex(parent);
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }

        int top_row = range.top();
        int bottom_row = range.bottom();
        if (top_row == 0) {
            continue;
        }

        QList<QStandardItem *> range_items;
        for (int row = top_row; row <= bottom_row; row++) {
            QStandardItem *item = parent_item->child(row, 0);
            range_items << item;
            selected_items << item;  // Accumulate all items for reselection
        }
        parent_items << parent_item;
        new_parent_items << parent_item->child(top_row - 1, 0);
        selected_ranges << range_items;
    }

    for (int i = 0; i < selected_ranges.count(); i++) {
        QStandardItem *parent_item = parent_items.at(i);
        QStandardItem *new_parent = new_parent_items.at(i);
        foreach(QStandardItem *item, selected_ranges.at(i)) {
            QList<QStandardItem *> row_items = parent_item->takeRow(item->row());
            new_parent->insertRow(new_parent->rowCount(), row_items);
        }
    }

    ReselectAndExpandItems(selected_items);
}

void EditTOC::MoveUp()
{
    QModelIndexList chosen_indexes = CheckSelections();
    if (chosen_indexes.isEmpty()) {
        return;
    }
    QList<QStandardItem *> chosen_items;
    foreach(const QModelIndex &index, chosen_indexes) {
        chosen_items << m_TableOfContents->itemFromIndex(index);
    }
    std::stable_sort(chosen_items.begin(), chosen_items.end(),
                     [](QStandardItem *left, QStandardItem *right) {
        QStandardItem *left_parent = left->parent();
        QStandardItem *right_parent = right->parent();
        if (left_parent == right_parent) {
            return left->row() < right->row();
        }
        return std::less<QStandardItem *>()(left_parent, right_parent);
    });

    foreach(QStandardItem *item, chosen_items) {
        QStandardItem *parent_item = item->parent();
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }

        int item_row = item->row();
        if (item_row == 0) {
            continue;
        }
        if (chosen_items.contains(parent_item->child(item_row - 1, 0))) {
            continue;
        }

        QList<QStandardItem *> row_items = parent_item->takeRow(item_row);
        parent_item->insertRow(item_row - 1, row_items);
    }

    ReselectAndExpandItems(chosen_items);
}

void EditTOC::MoveDown()
{
    QModelIndexList chosen_indexes = CheckSelections();
    if (chosen_indexes.isEmpty()) {
        return;
    }
    QList<QStandardItem *> chosen_items;
    foreach(const QModelIndex &index, chosen_indexes) {
        chosen_items << m_TableOfContents->itemFromIndex(index);
    }
    std::stable_sort(chosen_items.begin(), chosen_items.end(),
                     [](QStandardItem *left, QStandardItem *right) {
        QStandardItem *left_parent = left->parent();
        QStandardItem *right_parent = right->parent();
        if (left_parent == right_parent) {
            return left->row() < right->row();
        }
        return std::less<QStandardItem *>()(left_parent, right_parent);
    });

    for (int i = chosen_items.count() - 1; i >= 0; i--) {
        QStandardItem *item = chosen_items.at(i);
        QStandardItem *parent_item = item->parent();
        if (!parent_item) {
            parent_item = m_TableOfContents->invisibleRootItem();
        }

        int item_row = item->row();
        if (item_row == parent_item->rowCount() - 1) {
            continue;
        }
        if (chosen_items.contains(parent_item->child(item_row + 1, 0))) {
            continue;
        }

        QList<QStandardItem *> row_items = parent_item->takeRow(item_row);
        parent_item->insertRow(item_row + 1, row_items);
    }

    ReselectAndExpandItems(chosen_items);
}

void EditTOC::AddEntryAbove()
{
    AddEntry(true);
}

void EditTOC::AddEntryBelow()
{
    AddEntry(false);
}

void EditTOC::AddEntry(bool above)
{
    QModelIndex index = CheckSelection(0);
    if (!index.isValid()) {
        return;
    }

    QStandardItem *item = m_TableOfContents->itemFromIndex(index);

    QStandardItem *parent_item = item->parent();
    if (!parent_item) {
        parent_item = m_TableOfContents->invisibleRootItem();
    }

    // Add a new empty row of items
    QStandardItem *entry_item = new QStandardItem();
    QStandardItem *target_item = new QStandardItem();
    QList<QStandardItem *> row_items;
    row_items << entry_item << target_item ;
    int location = 1;
    if (above) {
        location = 0;
    }
    parent_item->insertRow(item->row() + location,row_items);

    // Select the new row
    ui.TOCTree->selectionModel()->clear();
    ui.TOCTree->setCurrentIndex(entry_item->index());
    ui.TOCTree->selectionModel()->select(entry_item->index(), QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
}

void EditTOC::MakeDefaultFirstSelection()
{
    QStandardItem *root_item = m_TableOfContents->invisibleRootItem();
    if (root_item->rowCount() > 0) {
        QModelIndex first_index = m_TableOfContents->index(0, 0, QModelIndex());
        ui.TOCTree->selectionModel()->clear();
        ui.TOCTree->setCurrentIndex(first_index);
        ui.TOCTree->selectionModel()->select(first_index,
                                             QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    }
}

QModelIndexList EditTOC::CheckSelections()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return QModelIndexList();
    }

    return ui.TOCTree->selectionModel()->selectedRows();
}

QModelIndex EditTOC::CheckSelection(int row)
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return QModelIndex();
    }

    QModelIndexList selected_indexes = ui.TOCTree->selectionModel()->selectedRows(row);

    if (selected_indexes.count() != 1) {
        return QModelIndex();
    }

    return selected_indexes.first();
}

void EditTOC::DeleteEntry()
{
    QModelIndexList selected_indexes = CheckSelections();
    if (selected_indexes.isEmpty()) {
        return;
    }

    // 1. Filter out nodes whose ancestors are also selected
    QList<QPersistentModelIndex> pending_indexes;
    foreach(const QModelIndex &index, selected_indexes) {
        bool selected_ancestor = false;
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
            if (selected_indexes.contains(parent.sibling(parent.row(), 0))) {
                selected_ancestor = true;
                break;
            }
        }

        if (!selected_ancestor) {
            pending_indexes << QPersistentModelIndex(index);
        }
    }

    // 2. Group by parent
    QHash<QPersistentModelIndex, QList<QPersistentModelIndex>> groups;
    foreach(const QPersistentModelIndex &idx, pending_indexes) {
        groups[idx.parent()].append(idx);
    }

    // 3. Sort each group by row in descending order and delete
    foreach(const QPersistentModelIndex &parent, groups.keys()) {
        QList<QPersistentModelIndex> &items = groups[parent];
        std::sort(items.begin(), items.end(),
                  [](const QPersistentModelIndex &a, const QPersistentModelIndex &b) {
            return a.row() > b.row();  // Descending: delete from back to front
        });

        // 4. Delete in reverse order
        foreach(const QPersistentModelIndex &idx, items) {
            if (!idx.isValid()) {
                continue;
            }
            QStandardItem *item = m_TableOfContents->itemFromIndex(idx);
            QStandardItem *parent_item = item->parent();
            if (!parent_item) {
                parent_item = m_TableOfContents->invisibleRootItem();
            }
            qDeleteAll(parent_item->takeRow(item->row()));
        }
    }

    // 5. Handle empty tree
    if (m_TableOfContents->rowCount() == 0) {
        QStandardItem *entry_item = new QStandardItem(tr("[placeholder]"));
        QStandardItem *target_item = new QStandardItem();
        QList<QStandardItem *> row_items;
        row_items << entry_item << target_item;
        m_TableOfContents->invisibleRootItem()->appendRow(row_items);

        ui.TOCTree->selectionModel()->clear();
        ui.TOCTree->setCurrentIndex(entry_item->index());
        ui.TOCTree->selectionModel()->select(entry_item->index(),
                                             QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    }
}

void EditTOC::SelectTarget()
{
    QModelIndex index = CheckSelection(1);
    if (!index.isValid()) {
        return;
    }

    QStandardItem *item = m_TableOfContents->itemFromIndex(index);
    // convert bookpath (epub root relative href) to relative to ncx or nav as appropriate
    QString ahref = item->text();
    if (ahref.indexOf(':') == -1) {
        std::pair<QString,QString> parts = Utility::parseRelativeHREF(ahref);
        ahref = Utility::buildRelativeHREF(Utility::buildRelativePath(m_BaseResource->GetRelativePath(),
                                                                      parts.first), parts.second);
    }

    SelectHyperlink select_target(ahref, m_BaseResource, "toc", m_Resources, m_Book, this);

    if (select_target.exec() == QDialog::Accepted) {
        QString href = select_target.GetTarget();
        // now convert ncx or nav relative path back to bookpath epub root relative)
        std::pair<QString,QString> parts = Utility::parseRelativeHREF(href);
        QString bookpath = Utility::buildBookPath(parts.first, m_BaseResource->GetFolder());
        item->setText(Utility::buildRelativeHREF(bookpath, parts.second));
    }
}

void EditTOC::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    QByteArray geometry = settings.value("geometry").toByteArray();

    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    }

    // Column widths
    int size = settings.beginReadArray("column_data");

    for (int column = 0; column < size && column < ui.TOCTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        int column_width = settings.value("width").toInt();

        if (column_width) {
            ui.TOCTree->setColumnWidth(column, column_width);
        }
    }
    settings.endArray();

    settings.endGroup();
}

void EditTOC::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    // The size of the window and it's full screen status
    settings.setValue("geometry", saveGeometry());

    // Column widths
    settings.beginWriteArray("column_data");

    for (int column = 0; column < ui.TOCTree->header()->count(); column++) {
        settings.setArrayIndex(column);
        settings.setValue("width", ui.TOCTree->columnWidth(column));
    }

    settings.endArray();

    settings.endGroup();
}

void EditTOC::Rename()
{
    if (!ui.TOCTree->selectionModel()->hasSelection()) {
        return;
    }

    if (ui.TOCTree->selectionModel()->selectedRows(0).count() != 1) {
        return;
    }

    ui.TOCTree->edit(ui.TOCTree->currentIndex());
}

void EditTOC::CollapseAll()
{
    ui.TOCTree->collapseAll();
}

void EditTOC::ExpandAll()
{
    ui.TOCTree->expandAll();
}

void EditTOC::CreateContextMenuActions()
{
    m_Rename = new QAction(tr("Rename"),     this);
    m_Delete = new QAction(tr("Delete"),     this);
    m_Rename->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_R));
    m_Delete->setShortcut(QKeySequence::Delete);
    // Has to be added to the dialog itself for the keyboard shortcut to work.
    addAction(m_Rename);
    addAction(m_Delete);

    m_MoveUp = new QAction(tr("Move Up"),       this);
    m_MoveDown = new QAction(tr("Move Down"),   this);
    m_MoveUp->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Up));
    m_MoveDown->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_Down));
    addAction(m_MoveUp);
    addAction(m_MoveDown);

    m_ExpandAll= new QAction(tr("Expand All"),     this);
    m_CollapseAll = new QAction(tr("Collapse All"),  this);
}

void EditTOC::OpenContextMenu(const QPoint &point)
{
    SetupContextMenu(point);
    m_ContextMenu->exec(ui.TOCTree->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
    }
}

void EditTOC::SetupContextMenu(const QPoint &point)
{
    m_ContextMenu->addAction(m_Rename);
    m_ContextMenu->addAction(m_Delete);
    m_ContextMenu->addSeparator();
    m_ContextMenu->addAction(m_CollapseAll);
    m_ContextMenu->addAction(m_ExpandAll);
}

bool EditTOC::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui.TOCTree) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            int key = keyEvent->key();

            if (key == Qt::Key_Left) {
                MoveLeft();
                return true;
            } else if (key == Qt::Key_Right) {
                MoveRight();
                return true;
            }
        }
    }

    // pass the event on to the parent class
    return QDialog::eventFilter(obj, event);
}

void EditTOC::BuildModel(const TOCModel::TOCEntry &root_entry)
{
    foreach(const TOCModel::TOCEntry& child_entry, root_entry.children) {
        AddEntryToParentItem(child_entry, m_TableOfContents->invisibleRootItem(), 1);
    }
}

void EditTOC::AddEntryToParentItem(const TOCModel::TOCEntry &entry, QStandardItem *parent, int level)
{
    Q_ASSERT(parent);
    QStandardItem *entry_item = new QStandardItem(entry.text);
    QStandardItem *target_item = new QStandardItem(entry.target);

    QList<QStandardItem *> row_items;
    row_items << entry_item << target_item ;
    parent->appendRow(row_items);

    foreach(const TOCModel::TOCEntry &child_entry, entry.children) {
        AddEntryToParentItem(child_entry, entry_item, level + 1);
    }
}

void EditTOC::ConnectSignalsToSlots()
{
    connect(this,               SIGNAL(accepted()),           this, SLOT(Save()));
    connect(ui.AddEntryAbove,   SIGNAL(clicked()),            this, SLOT(AddEntryAbove()));
    connect(ui.AddEntryBelow,   SIGNAL(clicked()),            this, SLOT(AddEntryBelow()));
    connect(ui.DeleteEntry,     SIGNAL(clicked()),            this, SLOT(DeleteEntry()));
    connect(ui.MoveLeft,        SIGNAL(clicked()),            this, SLOT(MoveLeft()));
    connect(ui.MoveRight,       SIGNAL(clicked()),            this, SLOT(MoveRight()));
    connect(ui.MoveUp,          SIGNAL(clicked()),            this, SLOT(MoveUp()));
    connect(ui.MoveDown,        SIGNAL(clicked()),            this, SLOT(MoveDown()));
    connect(m_MoveUp,           SIGNAL(triggered()),          this, SLOT(MoveUp()));
    connect(m_MoveDown,         SIGNAL(triggered()),          this, SLOT(MoveDown()));
    connect(ui.SelectTarget,    SIGNAL(clicked()),            this, SLOT(SelectTarget()));
    connect(ui.TOCTree,         SIGNAL(customContextMenuRequested(const QPoint &)),
            this,               SLOT(OpenContextMenu(const QPoint &)));
    connect(m_Rename,           SIGNAL(triggered()), this, SLOT(Rename()));
    connect(m_Delete,           SIGNAL(triggered()), this, SLOT(DeleteEntry()));
    connect(m_CollapseAll,      SIGNAL(triggered()), this, SLOT(CollapseAll()));
    connect(m_ExpandAll,        SIGNAL(triggered()), this, SLOT(ExpandAll()));
}
