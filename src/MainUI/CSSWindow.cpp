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

#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QHideEvent>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextDocumentLayout>
#include <QtWidgets/QVBoxLayout>

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/CSSWindow.h"
#include "ResourceObjects/CSSResource.h"
#include "ViewEditors/CodeViewEditor.h"
#include "Widgets/TextDocument.h"

static const float CSS_DOCK_FONT_DELTA = -1.0f;
static const int CSS_DOCK_LINE_NUMBER_MARGIN = 3;

CSSWindow::CSSWindow(QWidget *parent)
    :
    QDockWidget(tr("CSS Editor"), parent),
    m_CSSSelector(new QComboBox(this)),
    m_Editor(new CodeViewEditor(CodeViewEditor::Highlight_CSS, false, this)),
    m_EmptyDocument(new TextDocument(this)),
    m_StatusLabel(new QLabel(this)),
    m_SaveTimer(new QTimer(this)),
    m_Loading(false),
    m_ChangePending(false)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    QWidget *main_widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(main_widget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    QHBoxLayout *controls = new QHBoxLayout();
    controls->setContentsMargins(0, 0, 0, 0);
    m_CSSSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_CSSSelector->setMinimumContentsLength(18);
    m_CSSSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    controls->addWidget(m_CSSSelector, 1);
    layout->addLayout(controls);
    layout->addWidget(m_Editor, 1);
    layout->addWidget(m_StatusLabel);
    setWidget(main_widget);

    m_EmptyDocument->setDocumentLayout(new QPlainTextDocumentLayout(m_EmptyDocument));
    m_Editor->SetRouteUndoToMainWindow(false);
    m_Editor->SetReformatCSSEnabled(true);
    m_Editor->SetFontPointSizeAdjustment(CSS_DOCK_FONT_DELTA);
    m_Editor->SetLineNumberMargin(CSS_DOCK_LINE_NUMBER_MARGIN);
    m_Editor->CustomSetDocument(*m_EmptyDocument, false);
    m_Editor->Zoom();
    m_Editor->setEnabled(false);

    m_SaveTimer->setSingleShot(true);
    m_SaveTimer->setInterval(300);

    connect(m_CSSSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(LoadSelectedCSS()));
    connect(m_SaveTimer, SIGNAL(timeout()), this, SLOT(FlushPendingSave()));
    UpdateStatusLabel();
}


CSSWindow::~CSSWindow()
{
    m_SaveTimer->stop();
    if (m_ChangePending && m_CurrentResource && m_Book) {
        // Emitting CSSSaved during shutdown would reach an already
        // dismantled MainWindow, so only write the pending text to disk.
        m_CurrentResource->SaveToDisk(true);
    }
    m_ChangePending = false;
    DetachDocument();
}


void CSSWindow::FlushPendingChanges()
{
    FlushPendingSave();
}


void CSSWindow::SetBook(Book *book)
{
    FlushPendingSave();
    DetachDocument();

    m_CurrentResource = nullptr;
    m_Book = book;
    m_ChangePending = false;

    m_Loading = true;
    m_EmptyDocument->clear();
    m_EmptyDocument->clearUndoRedoStacks();
    m_EmptyDocument->setModified(false);
    m_Loading = false;

    m_Editor->setEnabled(false);
    {
        QSignalBlocker blocker(m_CSSSelector);
        m_CSSSelector->clear();
    }

    RefreshCSSList();
    UpdateStatusLabel();
}


void CSSWindow::RefreshCSSList()
{
    m_CSSResources.clear();

    QList<CSSResource *> resources;
    if (m_Book) {
        resources = m_Book->GetFolderKeeper()->GetResourceTypeList<CSSResource>(true);
    }

    CSSResource *current = m_CurrentResource.data();
    int current_index = -1;
    int index = 0;
    for (CSSResource *resource : resources) {
        if (resource == current) {
            current_index = index;
        }
        m_CSSResources.append(QPointer<CSSResource>(resource));
        ++index;
    }
    if (current_index < 0) {
        current = nullptr;
    }

    {
        QSignalBlocker blocker(m_CSSSelector);
        m_CSSSelector->clear();
        for (const QPointer<CSSResource> &resource : m_CSSResources) {
            if (resource) {
                m_CSSSelector->addItem(resource->GetRelativePath());
            }
        }
    }

    if (m_CSSResources.isEmpty()) {
        BindResource(nullptr);
        return;
    }

    if (current_index < 0) {
        current_index = 0;
    }
    {
        QSignalBlocker blocker(m_CSSSelector);
        m_CSSSelector->setCurrentIndex(current_index);
    }

    CSSResource *selected = m_CSSResources.at(current_index).data();
    if (selected != m_CurrentResource.data()) {
        BindResource(selected);
    } else {
        UpdateStatusLabel();
    }
}


void CSSWindow::LoadSelectedCSS()
{
    if (m_Loading) {
        return;
    }

    const int index = m_CSSSelector->currentIndex();
    QPointer<CSSResource> selected = (index >= 0 && index < m_CSSResources.size())
                                         ? m_CSSResources.at(index)
                                         : QPointer<CSSResource>();

    if (selected.data() == m_CurrentResource.data()) {
        return;
    }

    BindResource(selected.data());
}


void CSSWindow::BindResource(CSSResource *resource)
{
    FlushPendingSave();
    DetachDocument();
    m_CurrentResource = resource;
    m_Loading = true;

    if (resource) {
        resource->InitialLoad();
        TextDocument &doc = resource->GetTextDocumentForWriting();
        m_Editor->CustomSetDocument(doc, false);
        m_Editor->Zoom();
        m_BoundDocument = &doc;
        connect(&doc, &QTextDocument::contentsChanged,
                this, &CSSWindow::OnDocumentContentsChanged,
                Qt::UniqueConnection);
        connect(&doc, &QObject::destroyed,
                this, &CSSWindow::DetachDocument,
                Qt::UniqueConnection);
        m_Editor->setEnabled(true);
    } else {
        m_Editor->CustomSetDocument(*m_EmptyDocument, false);
        m_Editor->Zoom();
        m_Editor->setEnabled(false);
    }

    m_ChangePending = false;
    m_Loading = false;
    UpdateStatusLabel();
}


void CSSWindow::DetachDocument()
{
    m_SaveTimer->stop();
    if (m_BoundDocument) {
        disconnect(m_BoundDocument.data(), nullptr, this, nullptr);
    }
    m_BoundDocument = nullptr;

    if (m_Editor->document() != m_EmptyDocument) {
        m_Editor->CustomSetDocument(*m_EmptyDocument, false);
        m_Editor->Zoom();
    }
    UpdateStatusLabel();
}


void CSSWindow::OnDocumentContentsChanged()
{
    if (m_Loading || !m_CurrentResource || !m_Book) {
        return;
    }

    m_ChangePending = true;
    m_Book->SetModified(true);
    m_SaveTimer->start();
    UpdateStatusLabel();
}


void CSSWindow::FlushPendingSave()
{
    m_SaveTimer->stop();
    if (!m_CurrentResource || !m_Book || !m_ChangePending) {
        UpdateStatusLabel();
        return;
    }

    // The shared document already holds the final text. Writing it as-is
    // keeps the cursor and the shared undo stack intact. A book-wide save
    // suppresses ResourceUpdatedOnDisk so linked HTML tabs are not disturbed.
    m_CurrentResource->SaveToDisk(true);
    m_ChangePending = false;
    UpdateStatusLabel();
    emit CSSSaved();
}


void CSSWindow::UpdateStatusLabel()
{
    if (!m_CurrentResource) {
        m_StatusLabel->setText(tr("No CSS files"));
        return;
    }

    if (m_ChangePending) {
        m_StatusLabel->setText(tr("Auto-saving..."));
    } else {
        m_StatusLabel->setText(tr("Auto-saved"));
    }
}


void CSSWindow::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    RefreshCSSList();
}


void CSSWindow::hideEvent(QHideEvent *event)
{
    FlushPendingSave();
    QDockWidget::hideEvent(event);
}
