/************************************************************************
**
**  Copyright (C) 2019-2023 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2019-2023 Doug Massay
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

#include <QEvent>
#include <QEventLoop>
#include <QDeadlineTimer>
#include <QDateTime>
#include <QTimer>
#include <QSize>
#include <QUrl>
#include <QDir>
#include <QtWebEngineWidgets>
#include <QtWebEngineCore>
#include <QWebEngineSettings>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QWebEngineScript>
#include <QApplication>
#include <QGuiApplication>
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPointer>
#include <QDebug>

#include "MainUI/MainApplication.h"
#include "Misc/SettingsStore.h"
#include "Misc/Utility.h"
#include "sigil_constants.h"
#include "ViewEditors/WebEngPage.h"
#include "ViewEditors/ViewPreview.h"
#include "ViewEditors/Overlay.h"

#define DBG if(0)

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    #define QT_ENUM_SKIPEMPTYPARTS Qt::SkipEmptyParts
    #define QT_ENUM_KEEPEMPTYPARTS Qt::KeepEmptyParts
#else
    #define QT_ENUM_SKIPEMPTYPARTS QString::SkipEmptyParts
    #define QT_ENUM_KEEPEMPTYPARTS QString::KeepEmptyParts
#endif

const QString SET_CURSOR_JS2 =
    "var range = document.createRange();"
    "range.setStart(element, 0);"
    "range.setEnd(element, 0);"
    "var selection = window.getSelection();"
    "selection.removeAllRanges();"
    "selection.addRange(range);";

const QString SET_PREVIEW_COLORS =
    "document.body.style.backgroundColor=\"%1\"; "
    "document.body.style.color=\"%2\";";

// Chromium's own copy serialization inserts extra newlines around block
// elements. Build the text from leaf blocks instead so explicit empty
// blocks map to exactly one empty line and <ruby> keeps its annotation.
// The preview is served as application/xhtml+xml, so tagName/localName are
// lower case in the XML DOM. Always normalize through tag().
static const char COPY_SELECTION_JS[] = R"JS(
(function() {
  var sel = window.getSelection();
  if (!sel || sel.rangeCount === 0 || sel.isCollapsed) {
    return { ok: false, reason: 'no-selection' };
  }
  var range = sel.getRangeAt(0);
  var frag = range.cloneContents();

  var BLOCKS = {
    ADDRESS: 1, ARTICLE: 1, ASIDE: 1, BLOCKQUOTE: 1, BODY: 1, DD: 1,
    DETAILS: 1, DIV: 1, DL: 1, DT: 1, FIELDSET: 1, FIGCAPTION: 1,
    FIGURE: 1, FOOTER: 1, FORM: 1, H1: 1, H2: 1, H3: 1, H4: 1,
    H5: 1, H6: 1, HEADER: 1, HTML: 1, LI: 1, MAIN: 1, NAV: 1,
    OL: 1, P: 1, PRE: 1, SECTION: 1, SUMMARY: 1, TABLE: 1,
    TBODY: 1, TFOOT: 1, THEAD: 1, TR: 1, UL: 1
  };
  var SKIP = {
    SCRIPT: 1, STYLE: 1, NOSCRIPT: 1, HEAD: 1, TEMPLATE: 1,
    IFRAME: 1, OBJECT: 1, EMBED: 1, CANVAS: 1, VIDEO: 1, AUDIO: 1
  };

  function tag(el) {
    return (el.localName || el.tagName || '').toUpperCase();
  }
  function isHidden(el) {
    if (el.hasAttribute && el.hasAttribute('hidden')) return true;
    var style = el.getAttribute ? (el.getAttribute('style') || '') : '';
    return /display\s*:\s*none|visibility\s*:\s*hidden/i.test(style);
  }
  function nearestPre(node) {
    for (var n = node; n; n = n.parentNode) {
      if (n.nodeType === 1 && tag(n) === 'PRE') return n;
    }
    return null;
  }
  function isBlock(el) {
    return el.nodeType === 1 && BLOCKS[tag(el)] === 1;
  }
  function hasBlockChild(el) {
    for (var i = 0; i < el.children.length; i++) {
      var c = el.children[i];
      if (SKIP[tag(c)] || isHidden(c)) continue;
      if (isBlock(c)) return true;
    }
    return false;
  }

  var lines = [];
  var buf = [];
  var bufPre = false;
  var hasMedia = false;

  function flush(forceEmpty) {
    if (buf.length === 0) {
      if (forceEmpty) lines.push('');
      bufPre = false;
      return;
    }
    var text = '';
    for (var i = 0; i < buf.length; i++) {
      if (buf[i] === null) {
        text += '\n';
      } else {
        text += bufPre ? buf[i] : buf[i].replace(/[\s\u00A0]+/g, ' ');
      }
    }
    if (bufPre) {
      var preParts = text.split('\n');
      for (var p = 0; p < preParts.length; p++) {
        lines.push(preParts[p]);
      }
    } else {
      var visible = text.replace(/[\s\u00A0\n]/g, '');
      if (visible === '') {
        var breaks = (text.match(/\n/g) || []).length;
        for (var b = 0; b < breaks; b++) lines.push('');
        if (breaks === 0 && forceEmpty) lines.push('');
      } else {
        var parts = text.split('\n');
        for (var j = 0; j < parts.length; j++) {
          lines.push(parts[j].replace(/^[\s\u00A0]+|[\s\u00A0]+$/g, ''));
        }
      }
    }
    buf = [];
    bufPre = false;
  }

  function rubyBase(node) {
    var s = '';
    for (var i = 0; i < node.childNodes.length; i++) {
      var c = node.childNodes[i];
      if (c.nodeType === 3) {
        s += c.data;
      } else if (c.nodeType === 1) {
        var name = tag(c);
        if (name === 'RT' || name === 'RP' || name === 'RTC') {
          continue;
        }
        s += (name === 'RUBY') ? rubyText(c) : rubyBase(c);
      }
    }
    return s;
  }
  function rubyAnnotation(rt) {
    var text = (rt.textContent || '').replace(/\s+/g, ' ').trim();
    return text === '' ? '' : '\uFF08' + text + '\uFF09';
  }
  function rubyText(node) {
    var ann = '';
    for (var i = 0; i < node.childNodes.length; i++) {
      var c = node.childNodes[i];
      if (c.nodeType !== 1) continue;
      var name = tag(c);
      if (name === 'RT') {
        ann += rubyAnnotation(c);
      } else if (name === 'RTC') {
        for (var j = 0; j < c.children.length; j++) {
          if (tag(c.children[j]) === 'RT') {
            ann += rubyAnnotation(c.children[j]);
          }
        }
      }
    }
    return rubyBase(node) + ann;
  }

  function walkInline(node, inPre) {
    if (node.nodeType === 3) {
      buf.push(node.data);
      if (inPre) bufPre = true;
      return;
    }
    if (node.nodeType !== 1) return;
    var name = tag(node);
    if (name === 'IMG' || name === 'VIDEO' || name === 'AUDIO' ||
        name === 'CANVAS' || name === 'OBJECT' || name === 'EMBED' ||
        name === 'IFRAME') {
      hasMedia = true;
    }
    if (SKIP[name] || isHidden(node)) return;
    if (name === 'BR') {
      buf.push(null);
      return;
    }
    if (name === 'RUBY') {
      buf.push(rubyText(node));
      if (inPre) bufPre = true;
      return;
    }
    var pre = inPre || name === 'PRE';
    if (pre) bufPre = true;
    for (var i = 0; i < node.childNodes.length; i++) {
      walkInline(node.childNodes[i], pre);
    }
    if (name === 'TD' || name === 'TH') buf.push('\t');
  }

  function walkBlock(el) {
    var pre = tag(el) === 'PRE';
    if (!hasBlockChild(el)) {
      buf = [];
      bufPre = pre;
      for (var i = 0; i < el.childNodes.length; i++) {
        walkInline(el.childNodes[i], pre);
      }
      flush(true);
      return;
    }
    for (var j = 0; j < el.childNodes.length; j++) {
      var c = el.childNodes[j];
      if (c.nodeType === 1 && isBlock(c) && !SKIP[tag(c)] && !isHidden(c)) {
        flush(false);
        walkBlock(c);
      } else {
        walkInline(c, pre);
      }
    }
    flush(false);
  }

  // When both ends of the selection live in the same <pre>, cloneContents
  // returns bare text nodes and loses the PRE ancestor that tells flush()
  // to preserve whitespace. Restore that context for the top-level nodes.
  var startPre = nearestPre(range.startContainer);
  var endPre = nearestPre(range.endContainer);
  var selectionInPre = startPre && startPre === endPre;

  for (var i = 0; i < frag.childNodes.length; i++) {
    var c = frag.childNodes[i];
    if (c.nodeType === 1 && isBlock(c) && !SKIP[tag(c)] && !isHidden(c)) {
      flush(false);
      walkBlock(c);
    } else {
      walkInline(c, selectionInPre);
    }
  }
  flush(false);

  // Only trim outer blank lines when the selection contains visible text.
  // An all-blank selection must keep its blank lines instead.
  var hasVisibleText = false;
  for (var k = 0; k < lines.length; k++) {
    if (lines[k].replace(/[\s\u00A0]/g, '') !== '') {
      hasVisibleText = true;
      break;
    }
  }
  if (hasVisibleText) {
    while (lines.length > 0 && lines[0].replace(/[\s\u00A0]/g, '') === '') {
      lines.shift();
    }
    while (lines.length > 0 &&
           lines[lines.length - 1].replace(/[\s\u00A0]/g, '') === '') {
      lines.pop();
    }
  }

  if (!hasVisibleText && hasMedia) {
    // Keep the engine's native copy for selections that are only images
    // or other non-text media.
    return { ok: false, reason: 'media-only' };
  }
  return { ok: true, text: lines.join('\n') };
})();
)JS";


struct JSResult {
  QVariant res;
  bool     finished;
  JSResult() : res(QVariant("")), finished(false) {}
  JSResult(JSResult * pRes) : res(pRes->res),  finished(pRes->finished) {}
  ~JSResult() { }
  bool isFinished() { return finished; }
};

struct SetJavascriptResultFunctor {
    JSResult * pres;
    SetJavascriptResultFunctor(JSResult * pres) : pres(pres) {}
    void operator()(const QVariant &result) {
        pres->res.setValue(result);
        pres->finished = true;
        DBG qDebug() << "javascript done";
    }
};

struct HTMLResult {
   QString res;
   bool finished;
   HTMLResult() : res(QString("")), finished(false) {}
   HTMLResult(HTMLResult * pRes) : res(pRes->res),  finished(pRes->finished) {}
   bool isFinished() { return finished; }
 };

 struct SetToHTMLResultFunctor {
     HTMLResult * pres;
     SetToHTMLResultFunctor(HTMLResult * pres) : pres(pres) {}
     void operator()(const QString &result) {
         pres->res = result;
         pres->finished = true;
     }
 };

ViewPreview::ViewPreview(QWidget *parent, bool setbackground)
    : QWebEngineView(parent),
      m_isLoadFinished(false),
      m_ViewWebPage(new WebEngPage(this, setbackground)),
      c_jQuery(Utility::ReadUnicodeTextFile(":/javascript/jquery-2.2.4.min.js")),
      c_jQueryScrollTo(Utility::ReadUnicodeTextFile(":/javascript/jquery.scrollTo-2.1.2-min.js")),
      c_GetCaretLocation(Utility::ReadUnicodeTextFile(":/javascript/book_view_current_location.js")),
      m_CaretLocationUpdate(QString()),
      m_CustomSetDocumentInProgress(false),
      m_pendingScrollToFragment(QString()),
      m_LastHttpCacheClearMsecs(0),
      m_LoadOkay(false),
      m_overlay(new LoadingOverlay(this))
{
    setPage(m_ViewWebPage);
    // Now handled in the WebEngPage constructor to be faster
    // page()->setBackgroundColor(Utility::WebViewBackgroundColor(true));
    // Keep the default context-menu policy so contextMenuEvent() receives
    // the web page's context menu requests; the menu is built there.
    setContextMenuPolicy(Qt::DefaultContextMenu);
    // Set the Zoom factor but be sure no signals are set because of this.
    SettingsStore settings;
    SetCurrentZoomFactor(settings.zoomPreview());
    page()->settings()->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
    page()->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    page()->settings()->setDefaultTextEncoding("UTF-8");
    // Javascript is allowed 
    page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, (settings.javascriptOn() == 1));
    page()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, (settings.javascriptOn() == 1));
    // Allow epubs to access remote resources via the net
    page()->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, (settings.remoteOn() == 1));
    page()->settings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
    // Enable local-storage for epub3
    page()->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    QString localStorePath = Utility::DefinePrefsDir() + "/local-storage";
    QDir storageDir(localStorePath);
    if (!storageDir.exists()) {
        storageDir.mkpath(localStorePath);
    }
    page()->profile()->setPersistentStoragePath(localStorePath);
    ConnectSignalsToSlots();
}

ViewPreview::~ViewPreview()
{
    if (m_ViewWebPage != NULL) {
        delete m_ViewWebPage;
        m_ViewWebPage = 0;
    }
}

QString ViewPreview::GetCaretLocationUpdate()
{
    StoreCaretLocationUpdate(GetCaretLocation());
    return m_CaretLocationUpdate;
}

// Pick a sane/modest initial size for the Preview dockwidget
QSize ViewPreview::sizeHint() const
{
    return QSize(200, 400);
}

void ViewPreview::CustomSetDocument(const QString &path, const QString &html)
{
    if (html.isEmpty()) {
        return;
    }

    m_CustomSetDocumentInProgress = true;

    // If this is not the very first load of this document, store the caret location
    if (!url().isEmpty()) {

        // This next line really causes problems as it happens to interfere with later loading
        // StoreCurrentCaretLocation();
 
        // keep memory footprint small clear any caches when a new page loads
        // but throttle it: clearing on every document switch is expensive and
        // can race with an in-progress load
        if (url().toLocalFile() != path) {
            static const qint64 HTTP_CACHE_CLEAR_INTERVAL_MS = 60000;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (m_LastHttpCacheClearMsecs == 0 ||
                now - m_LastHttpCacheClearMsecs >= HTTP_CACHE_CLEAR_INTERVAL_MS) {
                qDebug() << "ViewPreview: clearing HTTP cache before loading" << path;
                page()->profile()->clearHttpCache();
                m_LastHttpCacheClearMsecs = now;
            }
        }
    }

    m_isLoadFinished = false;

    // If Tidy is turned off, then Sigil will explode if there is no xmlns
    // on the <html> element. So we will silently add it if needed to ensure
    // no errors occur, to allow loading of documents created outside of
    // Sigil as well as catering for section splits etc.
    QString replaced_html = html;
    replaced_html = replaced_html.replace("<html>", "<html xmlns=\"http://www.w3.org/1999/xhtml\">");
    MainApplication *mainApplication = qobject_cast<MainApplication *>(qApp);
    QString key = Utility::CreateUUID();
    mainApplication->saveInPreviewCache(key, replaced_html);
    QUrl tgturl = QUrl::fromLocalFile(path);
    tgturl.setScheme("sigil");
    tgturl.setHost("");
    tgturl.setQuery("sigilpreview=" + key); 
    page()->load(tgturl);
    // setContent(replaced_html.toUtf8(), "application/xhtml+xml;charset=UTF-8", QUrl::fromLocalFile(path));
}

bool ViewPreview::IsLoadingFinished()
{
    return m_isLoadFinished;
}

QString ViewPreview::GetHoverUrl()
{
    return m_hoverUrl;
}

void ViewPreview::SetZoomFactor(float factor)
{
    SettingsStore settings;
    settings.setZoomPreview(factor);
    SetCurrentZoomFactor(factor);
    Zoom();
    emit ZoomFactorChanged(factor);
}

void ViewPreview::SetCurrentZoomFactor(float factor)
{
    m_CurrentZoomFactor = factor;
}

float ViewPreview::GetZoomFactor() const
{
    return m_CurrentZoomFactor;
}

void ViewPreview::Zoom()
{
    setZoomFactor(m_CurrentZoomFactor);
}

void ViewPreview::UpdateDisplay()
{
#if 0
    SettingsStore settings;
    float stored_factor = settings.zoomWeb();

    if (stored_factor != m_CurrentZoomFactor) {
        m_CurrentZoomFactor = stored_factor;
        Zoom();
    }
#endif
}

void ViewPreview::SetPreviewColors(const QString &bg, const QString &fg)
{
    DoJavascript(SET_PREVIEW_COLORS.arg(bg).arg(fg));
}

void ViewPreview::ScrollToTop()
{
    QString caret_location = "var elementList = document.getElementsByTagName(\"body\");"
                             "var element = elementList[0];";
    QString scroll = "var from_top = window.innerHeight / 2;"
                     "if (typeof element !== 'undefined') {"
                     "    $.scrollTo(element, 0, {offset: {top:-from_top, left:0} });";
    QString script = caret_location + scroll + SET_CURSOR_JS2 + "}";

    DoJavascript(script);
}

void ViewPreview::ScrollToFragment(const QString &fragment)
{
    if (IsLoadingFinished()) {
        ScrollToFragmentInternal(fragment);
    } else {
        m_pendingScrollToFragment = fragment;
    }
}

void ViewPreview::ScrollToFragmentInternal(const QString &fragment)
{
    if (fragment.isEmpty()) {
        ScrollToTop();
        return;
    }
    QString caret_location = "var element = document.getElementById(\"" % fragment % "\");";
    QString scroll = "var from_top = window.innerHeight / 2.5;"
                     "if (typeof element !== 'undefined') {"
                     "$.scrollTo(element, 0, {offset: {top:-from_top, left:0 } });";
    QString script = caret_location + scroll + SET_CURSOR_JS2 + "}";
    DoJavascript(script);
}

void ViewPreview::LinkHovered(const QString &url)
{
    DBG qDebug() << "linkHovered received " << url;;
    m_hoverUrl = url;
}

void ViewPreview::LoadingStarted()
{
    DBG qDebug() << "Loading a page started";
    m_isLoadFinished = false;
    m_LoadOkay = false;
}


// AAARRRGGGHHHH - when a user hits a link that is a fragment
// that points to the current page, a load started signal is 
// generated but NO proper load finished signal ever comes.
// This is a horrible Qt bug to exist in a Qt 5.12.3 LTS release

// To workaround this keep track of our loading state
// and set it to finished when progress hits 100
void ViewPreview::LoadingProgress(int progress)
{
    DBG qDebug() << "Loading progress " << progress;
    if (progress >= 100 && !m_CustomSetDocumentInProgress) {
        m_isLoadFinished = true;
        m_LoadOkay = true;
    }
    if (zoomFactor() != m_CurrentZoomFactor) {
        setZoomFactor(m_CurrentZoomFactor);
    }
    emit ViewProgress(progress);
}

void ViewPreview::UpdateFinishedState(bool okay)
{
    // AAArrrrggggggghhhhhhhh - Qt 5.12.2 has a bug that returns 
    // loadFinished with okay set to false when caused by 
    // clicking a link that acceptNavigationRequest denies
    // even when there are no apparent errors!

    DBG qDebug() << "UpdateFinishedState with okay " << okay;
    // m_isLoadFinished = true;
    m_LoadOkay = okay;
}

void ViewPreview::HideOverlay()
{
    m_overlay->hide();
}

void ViewPreview::ShowOverlay()
{
    m_overlay->show();
}

QString ViewPreview::GetHTML() const 
 {
     HTMLResult * pres = new HTMLResult();
     page()->toHtml(SetToHTMLResultFunctor(pres));
     QDeadlineTimer deadline(5000);  // in milliseconds
     while(!pres->isFinished() && !deadline.hasExpired()) {
         qApp->processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers, 100);
     }
     QString res;
     if (pres->isFinished()) {
         res = pres->res;
         delete pres;
     } else {
         qWarning() << "Error: ViewPreview::GetHTML timed out";
         qWarning() << "   ... intentionally leaking the HTMLResult structure";
     }
     return res;
 }


QVariant ViewPreview::EvaluateJavascript(const QString &javascript)
{
    DBG qDebug() << "EvaluateJavascript: " << m_isLoadFinished;

    // do not try to evaluate javascripts with the page not loaded yet
    if (!m_isLoadFinished) return QVariant();

    JSResult * pres = new JSResult();
    QDeadlineTimer deadline(10000);  // in milliseconds

    // DBG qDebug() << "evaluate javascript" << javascript;

    page()->runJavaScript(javascript,QWebEngineScript::ApplicationWorld, SetJavascriptResultFunctor(pres));
    while(!pres->isFinished() && (!deadline.hasExpired())) {
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents, 100);
    }
    QVariant res;
    if (pres->isFinished()) {
        res = pres->res;
        delete pres;
    } else {
        qDebug() << "Error: VP EvaluateJavascript timed out";
        qDebug() << "   ... intentionally leaking the JSResult structure";
    }
    return res;
}

void ViewPreview::DoJavascript(const QString &javascript)
{
    DBG qDebug() << "DoJavascript with " << m_isLoadFinished;
    // DBG qDebug() << "In DoJavaScript with: " << javascript;

     // do not try to evaluate javascripts with the page not loaded yet
    if (!m_isLoadFinished) return;

    page()->runJavaScript(javascript,QWebEngineScript::ApplicationWorld);
}


void ViewPreview::CopySelectionToClipboard()
{
    if (!page()) {
        return;
    }
    if (!m_isLoadFinished) {
        return;
    }

    if (!page()->settings()->testAttribute(QWebEngineSettings::JavascriptEnabled)) {
        // The user disabled JavaScript; the normalized copy is unavailable
        // and the engine's native copy is the only possible path.
        triggerPageAction(QWebEnginePage::Copy);
        return;
    }

    QPointer<ViewPreview> guard(this);
    page()->runJavaScript(QString::fromLatin1(COPY_SELECTION_JS),
                          [guard](const QVariant &result) {
        if (!guard) {
            return;
        }
        const QVariantMap map = result.toMap();
        if (!result.isValid() || !map.contains(QStringLiteral("ok"))) {
            qWarning() << "ViewPreview: copy script returned an invalid result"
                       << result;
            guard->triggerPageAction(QWebEnginePage::Copy);
            return;
        }
        if (map.value(QStringLiteral("ok")).toBool()) {
            // An empty string is a valid result (a selection consisting
            // only of blank blocks), so never treat it as a failure.
            QApplication::clipboard()->setText(
                map.value(QStringLiteral("text")).toString());
            return;
        }
        const QString reason = map.value(QStringLiteral("reason")).toString();
        if (reason == QStringLiteral("media-only")) {
            // Preserve the engine's image/media copy support.
            guard->triggerPageAction(QWebEnginePage::Copy);
        } else if (reason != QStringLiteral("no-selection")) {
            qWarning() << "ViewPreview: copy script failed:" << reason;
            guard->triggerPageAction(QWebEnginePage::Copy);
        }
    });
}


void ViewPreview::contextMenuEvent(QContextMenuEvent *event)
{
    if (!page()) {
        QWebEngineView::contextMenuEvent(event);
        return;
    }

    // Build our own menu so every entry can be translated and the Copy
    // entry can be routed through the normalized copy above.
    QMenu menu(this);

    auto action_enabled = [this](QWebEnginePage::WebAction web_action) {
        QAction *source = page()->action(web_action);
        return source && source->isEnabled();
    };
    auto add_source_action = [this, &menu](QWebEnginePage::WebAction web_action,
                                           const QString &text) {
        QAction *source = page()->action(web_action);
        if (!source || !source->isEnabled()) {
            return;
        }
        QAction *item = menu.addAction(text);
        if (web_action == QWebEnginePage::Copy) {
            connect(item, &QAction::triggered,
                    this, &ViewPreview::CopySelectionToClipboard);
        } else {
            connect(item, &QAction::triggered, source, &QAction::trigger);
        }
    };

    const bool has_history = action_enabled(QWebEnginePage::Undo) ||
                             action_enabled(QWebEnginePage::Redo);
    const bool has_edit = action_enabled(QWebEnginePage::Cut) ||
                          action_enabled(QWebEnginePage::Copy) ||
                          action_enabled(QWebEnginePage::Paste);
    const bool has_select = action_enabled(QWebEnginePage::SelectAll);
    const QString hover_url = GetHoverUrl();
    const bool has_link = !hover_url.isEmpty();

    if (has_history) {
        add_source_action(QWebEnginePage::Undo, tr("&Undo"));
        add_source_action(QWebEnginePage::Redo, tr("&Redo"));
        menu.addSeparator();
    }
    if (has_edit) {
        add_source_action(QWebEnginePage::Cut, tr("Cu&t"));
        add_source_action(QWebEnginePage::Copy, tr("&Copy"));
        add_source_action(QWebEnginePage::Paste, tr("&Paste"));
        menu.addSeparator();
    }
    if (has_select) {
        add_source_action(QWebEnginePage::SelectAll, tr("Select &All"));
        menu.addSeparator();
    }
    if (has_link) {
        QAction *link_action = menu.addAction(tr("Copy &Link Address"));
        connect(link_action, &QAction::triggered, this, [hover_url]() {
            QApplication::clipboard()->setText(hover_url);
        });
        menu.addSeparator();
    }

    QAction *reload_action = menu.addAction(tr("&Reload Preview"));
    connect(reload_action, &QAction::triggered,
            this, &ViewPreview::ReloadPreviewRequested);
    QAction *print_action = menu.addAction(tr("&Print..."));
    connect(print_action, &QAction::triggered,
            this, &ViewPreview::PrintRequested);
    QAction *inspect_action = menu.addAction(tr("&Inspect Page"));
    connect(inspect_action, &QAction::triggered,
            this, &ViewPreview::InspectPageRequested);

    menu.exec(event->globalPos());
}


// If we don't steal focus first, then the QWebView
// can have focus (and its QWebFrame) and still not
// really have it (no blinking cursor).
//   We also still need to attempt to GrabFocus even
// when shown as a Preview page (even though no cursor
// is shown) or else the QStackWidget will explode on
// Windows when switching to another tab when it tries
// to determine where the previous focus was.
void ViewPreview::GrabFocus()
{
    // qobject_cast<QWidget *>(parent())->setFocus();
    QWebEngineView::setFocus();
}

// do not set load to finished until jqery has been loaded
void ViewPreview::WebPageJavascriptOnLoad()
{
    DBG qDebug() << "WebPageJavascriptOnLoad start";
    page()->runJavaScript(c_jQuery, QWebEngineScript::ApplicationWorld);
    page()->runJavaScript(c_jQueryScrollTo, QWebEngineScript::ApplicationWorld);
    DBG qDebug() << "WebPageJavascriptOnLoad end";
    DBG qDebug() << "WebPageJavascriptOnLoad with m_CustomSetDocumentInProgress: " << m_CustomSetDocumentInProgress;
    m_isLoadFinished = true;
    if (m_CustomSetDocumentInProgress) {
        // setZoomFactor(m_CurrentZoomFactor);
        if (!m_pendingScrollToFragment.isEmpty()) {
            ScrollToFragment(m_pendingScrollToFragment);
            m_pendingScrollToFragment.clear();
        } else {
            // Zoom must be complete before scrolling to an element and centering on it
            // *but* is not instantaneous.
            // It is better to delay this and handle it in PreviewWindow
            // executeCaretUpdateInternal();
        }
        m_CustomSetDocumentInProgress = false;
    }
    emit DocumentLoaded();
}

QString ViewPreview::GetElementSelectingJS_NoTextNodes(const QList<ElementIndex> &hierarchy) const
{
    // TODO: see if replacing jQuery with pure JS will speed up
    // caret location scrolling... note the children()/contents() difference:
    // children() only considers element nodes, contents() considers text nodes too.
    QString element_selector = "$('html')";
    // We will have a different hierarchy depending on whether it was generated by CodeView or
    // generated by Preview. If the last element is '#text', strip it off and make sure the
    // element preceding it has a -1 index in order for the caret to move correctly.
    bool adjust_last_index = false;
    int hierarchy_length = hierarchy.count() - 1;

    if (hierarchy_length > 1 && hierarchy.last().name == "#text") {
        hierarchy_length--;
    }

    for (int i = 0; i < hierarchy_length; ++i) {
        int index = hierarchy[ i ].index;

        if ((i == hierarchy_length) && adjust_last_index) {
            index = -1;
        }

        element_selector.append(QString(".children().eq(%1)").arg(index));
    }

    element_selector.append(".get(0)");
    return element_selector;
}

QList<ElementIndex> ViewPreview::GetCaretLocation()
{
    // The location element hierarchy encoded in a string
    QString location_string = EvaluateJavascript(c_GetCaretLocation).toString();
    DBG qDebug() << "GetCaretLocation: " << location_string;
    return ConvertQWebPathToHierarchy(location_string);
}

QList<ElementIndex> ViewPreview::ConvertQWebPathToHierarchy(const QString & webpath) const
{
    // The location element hierarchy encoded in a string
    QString location_string = webpath;
    QStringList elements    = location_string.split(",", QT_ENUM_SKIPEMPTYPARTS);
    QList<ElementIndex> location;
    foreach(QString element, elements) {
        ElementIndex new_element;
        new_element.name  = element.split(" ")[ 0 ];
        new_element.index = element.split(" ")[ 1 ].toInt();
        location.append(new_element);
    }
    return location;
}

QString ViewPreview::ConvertHierarchyToQWebPath(const QList<ElementIndex>& hierarchy)
{
    QStringList pathparts;
    for (int i=0; i < hierarchy.count(); i++) {
        QString part = hierarchy.at(i).name + " " + QString::number(hierarchy.at(i).index);
        pathparts.append(part);
    }
    return pathparts.join(",");
}

void ViewPreview::StoreCurrentCaretLocation()
{
    // Only overwrite the current location stored if it is empty, in case we specifically
    // want a new location when switching to a new view
    if (m_CaretLocationUpdate.isEmpty()) {
        StoreCaretLocationUpdate(GetCaretLocation());
    }
}

void ViewPreview::StoreCaretLocationUpdate(const QList<ElementIndex> &hierarchy)
{
    DBG qDebug() << "in StoreCaretLocationUpdate";
    QString caret_location = "var element = " + GetElementSelectingJS_NoTextNodes(hierarchy) + ";";
    // We scroll to the element and center the screen on it
    QString scroll = "var from_top = window.innerHeight / 2;"
                     "if (typeof element !== 'undefined') {"
                     "$.scrollTo( element, 0, {offset: {top:-from_top, left:0 } } );";
    m_CaretLocationUpdate = caret_location + scroll + SET_CURSOR_JS2 + "}";
}

QString ViewPreview::GetElementSelectingJS_WithTextNode(const QList<ElementIndex> &hierarchy) const
{
    QString element_selector = "$('html')";

    for (int i = 0; i < hierarchy.count() - 1; ++i) {
        element_selector.append(QString(".children().eq(%1)").arg(hierarchy[ i ].index));
    }

    element_selector.append(QString(".contents().eq(%1)").arg(hierarchy.last().index));
    element_selector.append(".get(0)");
    return element_selector;
}


bool ViewPreview::ExecuteCaretUpdate()
{

    // If there is no caret location update pending...
    if (m_CaretLocationUpdate.isEmpty()) {
        return false;
    }

    // run it...
    DoJavascript(m_CaretLocationUpdate);
    // ...and clear the update.
    m_CaretLocationUpdate.clear();
    return true;
}

bool ViewPreview::ExecuteCaretUpdate(const QString &caret_update)
{
    // This overload is for use with the Back To Link type functionality, where we have
    // a specific caret location javascript we want to force when the tab is fully loaded.
    if (!caret_update.isEmpty()) {
        m_CaretLocationUpdate = caret_update;
        return ExecuteCaretUpdate();
    }

    return false;
}


void ViewPreview::ConnectSignalsToSlots()
{
    connect(page(), SIGNAL(loadFinished(bool)), this, SLOT(UpdateFinishedState(bool)));
    connect(page(), SIGNAL(loadFinished(bool)), this, SLOT(WebPageJavascriptOnLoad()));
    connect(page(), SIGNAL(loadStarted()), this, SLOT(LoadingStarted()));
    connect(page(), SIGNAL(LinkClicked(const QUrl &)), this, SIGNAL(LinkClicked(const QUrl &)));
    connect(page(), SIGNAL(loadProgress(int)), this, SLOT(LoadingProgress(int)));
    connect(page(), SIGNAL(linkHovered(const QString &)), this, SLOT(LinkHovered(const QString &)));
}
