/************************************************************************
**
**  Copyright (C) 2015-2021 Kevin B. Hendricks, Stratford Ontario Canada
**  Copyright (C) 2009-2011 Strahinja Markovic  <strahinja.markovic@gmail.com>
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

#include "EmbedPython/EmbeddedPython.h"

#include <exception>

#include <QtCore/QFileInfo>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTableWidget>
#include <QRegularExpression>
#include <QUrl>
#include <QVariant>
#include <QFileDialog>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <qtextcodec.h>  // modified: correctOPF

#include "BookManipulation/Book.h"
#include "BookManipulation/FolderKeeper.h"
#include "BookManipulation/XhtmlDoc.h"
#include "MainUI/ValidationResultsView.h"
#include "Misc/Utility.h"
#include "sigil_exception.h"

#include "Parsers/OPFParser.h" // modified: correctOPF
#include "MainUI/BookBrowser.h" // modified: correctOPF
#include "Misc/HTMLEncodingResolver.h" // modified: correctOPF

#if(0)
static const QBrush INFO_BRUSH    = QBrush(QColor(224, 255, 255));
static const QBrush WARNING_BRUSH = QBrush(QColor(255, 255, 230));
static const QBrush ERROR_BRUSH   = QBrush(QColor(255, 230, 230));
#endif

const QString ValidationResultsView::SEP = QString(QChar(31));

static const QString SETTINGS_GROUP = "validation_results";
static const QString OPF_NAMESPACE_URI = "http://www.idpf.org/2007/opf";

namespace
{

enum ManifestItemAction
{
    ManifestItem_Remove,
    ManifestItem_Keep,
    ManifestItem_ReplaceHref
};

void WriteEntryAttributes(QXmlStreamWriter &writer, const TagAtts &attributes)
{
    foreach(const QString &name, attributes.keys()) {
        const QString value = attributes.value(name);
        if (name == QLatin1String("xmlns")) {
            writer.writeDefaultNamespace(value);
        } else if (name.startsWith(QLatin1String("xmlns:"))) {
            writer.writeNamespace(value, name.mid(6));
        } else {
            writer.writeAttribute(name, value);
        }
    }
}

void WriteManifestEntry(QXmlStreamWriter &writer,
                        const QString &qualified_name,
                        const ManifestEntry &entry)
{
    writer.writeEmptyElement(qualified_name);
    writer.writeAttribute(QLatin1String("id"), entry.m_id);
    writer.writeAttribute(QLatin1String("href"), entry.m_href);
    writer.writeAttribute(QLatin1String("media-type"), entry.m_mtype);
    WriteEntryAttributes(writer, entry.m_atts);
}

void WriteSpineEntry(QXmlStreamWriter &writer,
                     const QString &qualified_name,
                     const SpineEntry &entry)
{
    writer.writeEmptyElement(qualified_name);
    writer.writeAttribute(QLatin1String("idref"), entry.m_idref);
    WriteEntryAttributes(writer, entry.m_atts);
}

void WriteStartElementWithHref(QXmlStreamWriter &writer,
                               const QXmlStreamReader &reader,
                               const QString &href)
{
    writer.writeStartElement(reader.qualifiedName().toString());
    foreach(const QXmlStreamNamespaceDeclaration &declaration,
            reader.namespaceDeclarations()) {
        if (declaration.prefix().isEmpty()) {
            writer.writeDefaultNamespace(declaration.namespaceUri().toString());
        } else {
            writer.writeNamespace(declaration.namespaceUri().toString(),
                                  declaration.prefix().toString());
        }
    }

    QXmlStreamAttributes attributes;
    foreach(const QXmlStreamAttribute &attribute, reader.attributes()) {
        if (attribute.namespaceUri().isEmpty() &&
            attribute.name().compare(QLatin1String("href")) == 0) {
            attributes.append(QLatin1String("href"), href);
        } else {
            attributes.append(attribute);
        }
    }
    writer.writeAttributes(attributes);
}

QString RewriteOPFPreservingExtensions(
    const QString &source,
    const QList<int> &manifest_actions,
    const QStringList &replacement_hrefs,
    const QList<ManifestEntry> &added_manifest_entries,
    const QList<SpineEntry> &added_spine_entries,
    QString &error)
{
    QString output;
    if (replacement_hrefs.count() != manifest_actions.count()) {
        error = QObject::tr("The OPF manifest update data is inconsistent.");
        return QString();
    }

    QXmlStreamReader reader(source);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(false);

    bool in_manifest = false;
    bool manifest_rewritten = false;
    int xml_depth = 0;
    int package_depth = -1;
    int manifest_depth = 0;
    int manifest_item_index = 0;
    QString manifest_item_name = QLatin1String("item");

    bool in_spine = false;
    bool spine_found = false;
    int spine_depth = 0;
    QString spine_item_name = QLatin1String("itemref");

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            ++xml_depth;
            if (package_depth == -1 &&
                reader.name().compare(QLatin1String("package")) == 0 &&
                reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                package_depth = xml_depth;
            }
            if (!in_manifest && !manifest_rewritten &&
                package_depth != -1 && xml_depth == package_depth + 1 &&
                reader.name().compare(QLatin1String("manifest")) == 0 &&
                reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                in_manifest = true;
                manifest_depth = 1;
                const QString prefix = reader.prefix().toString();
                if (prefix.isEmpty()) {
                    manifest_item_name = QLatin1String("item");
                } else {
                    manifest_item_name = prefix + QLatin1String(":item");
                }
                writer.writeCurrentToken(reader);
                continue;
            }

            if (in_manifest) {
                if (manifest_depth == 1 &&
                    reader.name().compare(QLatin1String("item")) == 0 &&
                    reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                    if (manifest_item_index >= manifest_actions.count()) {
                        error = QObject::tr("OPF manifest changed while it was being normalized.");
                        return QString();
                    }
                    const int action = manifest_actions.at(manifest_item_index);
                    const QString replacement_href =
                        replacement_hrefs.at(manifest_item_index);
                    ++manifest_item_index;

                    if (action == ManifestItem_Remove) {
                        reader.skipCurrentElement();
                        --xml_depth;
                        continue;
                    }
                    if (action == ManifestItem_ReplaceHref) {
                        WriteStartElementWithHref(writer, reader, replacement_href);
                    } else {
                        writer.writeCurrentToken(reader);
                    }
                    ++manifest_depth;
                    continue;
                }

                writer.writeCurrentToken(reader);
                ++manifest_depth;
                continue;
            }

            if (!spine_found &&
                package_depth != -1 && xml_depth == package_depth + 1 &&
                reader.name().compare(QLatin1String("spine")) == 0 &&
                reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                in_spine = true;
                spine_found = true;
                spine_depth = 1;
                const QString prefix = reader.prefix().toString();
                if (prefix.isEmpty()) {
                    spine_item_name = QLatin1String("itemref");
                } else {
                    spine_item_name = prefix + QLatin1String(":itemref");
                }
            } else if (in_spine) {
                ++spine_depth;
            }
            writer.writeCurrentToken(reader);
            continue;
        }

        if (reader.isEndElement()) {
            if (in_manifest) {
                if (manifest_depth == 1 &&
                    reader.name().compare(QLatin1String("manifest")) == 0 &&
                    reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                    foreach(const ManifestEntry &entry, added_manifest_entries) {
                        writer.writeCharacters(QLatin1String("\n    "));
                        WriteManifestEntry(writer, manifest_item_name, entry);
                    }
                    if (!added_manifest_entries.isEmpty()) {
                        writer.writeCharacters(QLatin1String("\n  "));
                    }
                    writer.writeCurrentToken(reader);
                    in_manifest = false;
                    manifest_rewritten = true;
                    manifest_depth = 0;
                    --xml_depth;
                    continue;
                }
                writer.writeCurrentToken(reader);
                --manifest_depth;
                --xml_depth;
                continue;
            }

            if (in_spine) {
                if (spine_depth == 1 &&
                    reader.name().compare(QLatin1String("spine")) == 0 &&
                    reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                    foreach(const SpineEntry &entry, added_spine_entries) {
                        writer.writeCharacters(QLatin1String("\n    "));
                        WriteSpineEntry(writer, spine_item_name, entry);
                    }
                    if (!added_spine_entries.isEmpty()) {
                        writer.writeCharacters(QLatin1String("\n  "));
                    }
                    writer.writeCurrentToken(reader);
                    in_spine = false;
                    spine_depth = 0;
                    --xml_depth;
                    continue;
                }
                writer.writeCurrentToken(reader);
                --spine_depth;
                --xml_depth;
                continue;
            }

            writer.writeCurrentToken(reader);
            --xml_depth;
            continue;
        }

        writer.writeCurrentToken(reader);
    }

    if (reader.hasError()) {
        error = reader.errorString();
        return QString();
    }
    if (!manifest_rewritten || manifest_item_index != manifest_actions.count()) {
        error = QObject::tr("The OPF manifest could not be rewritten safely.");
        return QString();
    }
    if (!added_spine_entries.isEmpty() && !spine_found) {
        error = QObject::tr("The OPF spine could not be updated safely.");
        return QString();
    }

    QXmlStreamReader verification_reader(output);
    while (!verification_reader.atEnd()) {
        verification_reader.readNext();
    }
    if (verification_reader.hasError()) {
        error = verification_reader.errorString();
        return QString();
    }
    return output;
}

}


ValidationResultsView::ValidationResultsView(QWidget *parent)
    :
    QDockWidget(tr("Validation Results"), parent),
    m_ResultTable(new QTableWidget(this)),
    m_BookBrowser(NULL),
    m_NoProblems(false),
    m_ContextMenu(new QMenu(this))
{
    setWidget(m_ResultTable);
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    SetUpTable();
    m_ResultTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ExportAll = new QAction(tr("Export All") + "...", this);
    ReadSettings();
    connect(m_ResultTable, SIGNAL(itemDoubleClicked(QTableWidgetItem *)),
            this, SLOT(ResultDoubleClicked(QTableWidgetItem *)));
    connect(m_ResultTable, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(OpenContextMenu(const QPoint &)));
    connect(m_ExportAll,   SIGNAL(triggered()), this, SLOT(ExportAll()));
}

void ValidationResultsView::ReadSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    m_LastFolderOpen = settings.value("last_folder_open").toString();
    settings.endGroup();
}

void ValidationResultsView::WriteSettings()
{
    SettingsStore settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("last_folder_open", m_LastFolderOpen);
    settings.endGroup();
}


void ValidationResultsView::OpenContextMenu(const QPoint &point)
{
    m_ContextMenu->addAction(m_ExportAll);
    m_ContextMenu->exec(m_ResultTable->viewport()->mapToGlobal(point));
    if (!m_ContextMenu.isNull()) {
        m_ContextMenu->clear();
        m_ExportAll->setEnabled(true);
    }
}


void ValidationResultsView::ExportAll()
{
    if (m_NoProblems || m_ResultTable->rowCount() == 0) return;

    // Get the filename to use
    QMap<QString,QString> file_filters;
    file_filters[ "csv" ] = tr("CSV files (*.csv)");
    file_filters[ "txt" ] = tr("Text files (*.txt)");
    QStringList filters = file_filters.values();
    QString filter_string = "";
    foreach(QString filter, filters) {
        filter_string += filter + ";;";
    }
    QString default_filter = file_filters.value("csv");

    QFileDialog::Options options = QFileDialog::Options();
#ifdef Q_OS_MAC
    options = options | QFileDialog::DontUseNativeDialog;
#endif

    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Export Validation Results"),
                                                    m_LastFolderOpen,
                                                    filter_string,
                                                    &default_filter,
                                                    options);
    if (filename.isEmpty()) return;

    QString ext = QFileInfo(filename).suffix().toLower();
    QChar sep = QChar(',');
    if (ext == "txt") sep = QChar(9);

    QStringList res;
    for (int i = 0; i < m_ResultTable->rowCount(); i++) {
        QStringList data;
        QTableWidgetItem *path_item = m_ResultTable->item(i, 0);
        data << path_item->data(Qt::UserRole+1).toString();
        data << m_ResultTable->item(i,1)->text();
        data << m_ResultTable->item(i,2)->text();
        data << m_ResultTable->item(i,3)->text();
        if (sep == ',') {
            res << Utility::createCSVLine(data);
        } else {
            res << data.join(sep);
        }
    }
    QString text = res.join('\n');
    QString message;
    try {
        Utility::WriteUnicodeTextFile(text, filename);
        m_LastFolderOpen = QFileInfo(filename).absolutePath();
        WriteSettings();
    } catch (CannotOpenFile& e) {
        message = QString(e.what());
        Utility::DisplayStdWarningDialog(tr("Export of Validation Results failed: "), message);
    }
}


void ValidationResultsView::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    raise();
}


QStringList ValidationResultsView::ValidateFile(QString &apath)
{
    int rv = 0;
    QString error_traceback;
    QStringList results;

    QList<QVariant> args;
    args.append(QVariant(apath));

    EmbeddedPython * epython  = EmbeddedPython::instance();

    QVariant res = epython->runInPython( QString("sanitycheck"),
                                         QString("perform_sanity_check"),
                                         args,
                                         &rv,
                                         error_traceback);    
    if (rv != 0) {
        Utility::DisplayStdWarningDialog(QString("error in sanitycheck perform_sanity_check: ") + QString::number(rv), 
                                         error_traceback);
        // an error happened - make no changes
        return results;
    }
    return res.toStringList();
}


//----------------------------------------------------modified: correctOPF-------------------------------------------------
QList<ValidationResult> ValidationResultsView::correctOPF()
{
    QList<ValidationResult> results;
    if (m_Book.isNull() || !m_Book->GetFolderKeeper()) {
        return results;
    }

    OPFResource *opf = m_Book->GetOPF();
    if (!opf) {
        return results;
    }

    const QString opfpath = opf->GetCurrentBookRelPath();
    const ValidationResult::ResType error_type = ValidationResult::ResType_Error;
    const ValidationResult::ResType warning_type = ValidationResult::ResType_Warn;
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    const auto utf8 = [codec](const char *message) {
        return codec ? codec->toUnicode(message) : QString::fromUtf8(message);
    };

    const QString opf_source = opf->GetText();
    if (opf_source.trimmed().isEmpty()) {
        results << ValidationResult(error_type, opfpath, -1, -1,
                                    utf8("OPF规范：OPF内容为空，无法执行规范化。"));
        return results;
    }

    bool has_package = false;
    bool has_metadata = false;
    bool has_manifest = false;
    bool has_spine = false;
    int manifest_item_count = 0;
    int xml_depth = 0;
    int package_depth = -1;
    int manifest_depth = -1;
    QXmlStreamReader opf_reader(opf_source);
    while (!opf_reader.atEnd()) {
        opf_reader.readNext();
        if (opf_reader.isEndElement()) {
            if (xml_depth == manifest_depth &&
                opf_reader.name().compare(QLatin1String("manifest")) == 0 &&
                opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
                manifest_depth = -1;
            }
            --xml_depth;
            continue;
        }
        if (!opf_reader.isStartElement()) {
            continue;
        }
        ++xml_depth;
        if (package_depth == -1 &&
            opf_reader.name().compare(QLatin1String("package")) == 0 &&
            opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
            has_package = true;
            package_depth = xml_depth;
        } else if (package_depth != -1 && xml_depth == package_depth + 1 &&
                   opf_reader.name().compare(QLatin1String("metadata")) == 0 &&
                   opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
            has_metadata = true;
        } else if (package_depth != -1 && xml_depth == package_depth + 1 &&
                   opf_reader.name().compare(QLatin1String("manifest")) == 0 &&
                   opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
            has_manifest = true;
            manifest_depth = xml_depth;
        } else if (package_depth != -1 && xml_depth == package_depth + 1 &&
                   opf_reader.name().compare(QLatin1String("spine")) == 0 &&
                   opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
            has_spine = true;
        } else if (manifest_depth != -1 &&
                   xml_depth == manifest_depth + 1 &&
                   opf_reader.name().compare(QLatin1String("item")) == 0 &&
                   opf_reader.namespaceUri().compare(OPF_NAMESPACE_URI) == 0) {
            ++manifest_item_count;
        }
    }
    if (opf_reader.hasError()) {
        const QString msg = utf8("OPF规范：OPF XML解析失败：%1").arg(opf_reader.errorString());
        results << ValidationResult(error_type, opfpath,
                                    static_cast<int>(opf_reader.lineNumber()),
                                    static_cast<int>(opf_reader.columnNumber()), msg);
        return results;
    }

    QStringList missing_sections;
    if (!has_package) {
        missing_sections << "package";
    }
    if (!has_metadata) {
        missing_sections << "metadata";
    }
    if (!has_manifest) {
        missing_sections << "manifest";
    }
    if (!has_spine) {
        missing_sections << "spine";
    }
    if (!missing_sections.isEmpty()) {
        const QString msg = utf8("OPF规范：缺少关键结构【%1】，无法安全执行规范化。")
                            .arg(missing_sections.join(", "));
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
        return results;
    }

    OPFParser p;
    p.parse(opf_source);
    if (p.m_manifest.count() != manifest_item_count) {
        results << ValidationResult(error_type, opfpath, -1, -1,
                                    utf8("OPF规范：未能完整解析Manifest项，已跳过规范化以避免损坏OPF。"));
        return results;
    }

    // Inspect the namespace.
    if (!p.m_package.m_atts.contains("xmlns")) {
        results << ValidationResult(
            error_type, opfpath, -1, -1,
            utf8("OPF规范：找不到在package节点的xmlns属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_package.m_atts["xmlns"] != "http://www.idpf.org/2007/opf") {
        const QString msg = utf8("OPF规范：不规范的xmlns属性值【%1】，建议改为\"http://www.idpf.org/2007/opf\"。")
                            .arg(p.m_package.m_atts["xmlns"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }

    if (!p.m_metans.m_atts.contains("xmlns:dc")) {
        results << ValidationResult(
            error_type, opfpath, -1, -1,
            utf8("OPF规范：找不到在metadata节点的xmlns:dc属性，建议以\"http://purl.org/dc/elements/1.1/\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:dc"] != "http://purl.org/dc/elements/1.1/") {
        const QString msg = utf8("OPF规范：不规范的xmlns:dc属性值【%1】，建议改为\"http://purl.org/dc/elements/1.1/\"。")
                            .arg(p.m_metans.m_atts["xmlns:dc"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }

    if (!p.m_metans.m_atts.contains("xmlns:opf")) {
        results << ValidationResult(
            error_type, opfpath, -1, -1,
            utf8("OPF规范：找不到在metadata节点的xmlns:opf属性，建议以\"http://www.idpf.org/2007/opf\"值补上该属性。"));
    } else if (p.m_metans.m_atts["xmlns:opf"] != "http://www.idpf.org/2007/opf") {
        const QString msg = utf8("OPF规范：不规范的xmlns:opf属性值【%1】，建议改为\"http://www.idpf.org/2007/opf\"。")
                            .arg(p.m_metans.m_atts["xmlns:opf"]);
        results << ValidationResult(error_type, opfpath, -1, -1, msg);
    }

    // Inspect duplicate ids, invalid hrefs, and invalid idrefs.
    FolderKeeper *folder_keeper = m_Book->GetFolderKeeper();
    const QString tempfolder = folder_keeper->GetFullPathToMainFolder();
    const QStringList filepathList = Utility::walkDirs(tempfolder);
    QSet<QString> bookPaths;
    QHash<QString, QString> lowerBkPath_to_originalBkPath;
    QSet<QString> ambiguousLowerBkPaths;
    foreach(QString filepath, filepathList) {
        const QString bkpath = filepath.mid(tempfolder.size() + 1);
        const QString lowerBkpath = bkpath.toLower();
        bookPaths.insert(bkpath);
        if (lowerBkPath_to_originalBkPath.contains(lowerBkpath) &&
            lowerBkPath_to_originalBkPath.value(lowerBkpath) != bkpath) {
            ambiguousLowerBkPaths.insert(lowerBkpath);
        } else {
            lowerBkPath_to_originalBkPath[lowerBkpath] = bkpath;
        }
    }

    const auto isRemoteHref = [](const QString &href) {
        const QUrl url(href);
        return !url.scheme().isEmpty() || !url.host().isEmpty();
    };
    const auto decodedBookPath = [opf](const QString &href) {
        return Utility::buildBookPath(Utility::URLDecodePath(href), opf->GetFolder());
    };
    const auto hrefKey = [&decodedBookPath, &ambiguousLowerBkPaths,
                          &isRemoteHref](const QString &href) -> QString {
        if (isRemoteHref(href)) {
            return QString(QChar(30)) + href;
        }
        const QString bookpath = decodedBookPath(href);
        const QString lowerBookpath = bookpath.toLower();
        return ambiguousLowerBkPaths.contains(lowerBookpath) ? bookpath : lowerBookpath;
    };

    QStringList spineIdrefList;
    foreach(SpineEntry se, p.m_spine) {
        spineIdrefList << se.m_idref;
    }

    QString cover_idref;
    foreach(MetaEntry meta, p.m_metadata) {
        if (meta.m_name == "meta" && meta.m_atts["name"] == "cover") {
            cover_idref = meta.m_atts["content"];
        }
    }

    QStringList allIdrefList = spineIdrefList;
    if (!cover_idref.isEmpty()) {
        allIdrefList.prepend(cover_idref);
    }

    QHash<QString, QString> hrefKey_to_id;
    QHash<QString, QString> id_to_hrefKey;
    foreach(ManifestEntry me, p.m_manifest) {
        const QString key = hrefKey(me.m_href);

        // If multiple ids target the same href, prefer an id that is referenced.
        if (hrefKey_to_id.contains(key)) {
            if (allIdrefList.contains(me.m_id)) {
                hrefKey_to_id[key] = me.m_id;
            }
        } else {
            hrefKey_to_id[key] = me.m_id;
        }

        // If an id targets multiple hrefs, prefer one that exists in the book.
        if (id_to_hrefKey.contains(me.m_id)) {
            if (!isRemoteHref(me.m_href)) {
                const QString bookpath = decodedBookPath(me.m_href);
                const QString lowerBkpath = bookpath.toLower();
                if (bookPaths.contains(bookpath) ||
                    (!ambiguousLowerBkPaths.contains(lowerBkpath) &&
                     lowerBkPath_to_originalBkPath.contains(lowerBkpath))) {
                    id_to_hrefKey[me.m_id] = key;
                }
            }
        } else {
            id_to_hrefKey[me.m_id] = key;
        }
    }

    QList<ManifestEntry> new_manifest;
    QList<int> manifest_actions;
    QStringList replacement_hrefs;
    QSet<QString> kept_manifest_ids;
    QSet<QString> kept_manifest_keys;
    bool opf_changed = false;
    for (int i = 0; i < p.m_manifest.count(); ++i) {
        ManifestEntry me = p.m_manifest.at(i);
        const QString key = hrefKey(me.m_href);
        const QString id = me.m_id;

        if (key != id_to_hrefKey.value(id)) {
            const QString msg =
                utf8("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。").arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            manifest_actions << ManifestItem_Remove;
            replacement_hrefs << QString();
            opf_changed = true;
            continue;
        }
        if (id != hrefKey_to_id.value(key)) {
            const QString msg =
                utf8("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。").arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            manifest_actions << ManifestItem_Remove;
            replacement_hrefs << QString();
            opf_changed = true;
            continue;
        }

        if (isRemoteHref(me.m_href)) {
            manifest_actions << ManifestItem_Keep;
            replacement_hrefs << QString();
        } else {
            const QString bkpath = decodedBookPath(me.m_href);
            const QString lowerBkpath = bkpath.toLower();
            const bool exactPathExists = bookPaths.contains(bkpath);
            const bool uniqueCaseInsensitiveMatch =
                !ambiguousLowerBkPaths.contains(lowerBkpath) &&
                lowerBkPath_to_originalBkPath.contains(lowerBkpath);
            if (!exactPathExists && !uniqueCaseInsensitiveMatch) {
                const QString msg =
                    utf8("OPF规范：无效OPF超链接：在manifest项发现无效href【%1】，已进行删除对应项的处理。")
                    .arg(me.m_href);
                results << ValidationResult(error_type, opfpath, -1, -1, msg);
                manifest_actions << ManifestItem_Remove;
                replacement_hrefs << QString();
                opf_changed = true;
                continue;
            }

            const QString originalBkpath = exactPathExists ? bkpath :
                                           lowerBkPath_to_originalBkPath.value(lowerBkpath);
            if (bkpath != originalBkpath) {
                const QString msg =
                    utf8("OPF规范：大小写不一致：发现OPF超链接【%1】与实际路径大小写不一致，已自动校正。")
                    .arg(me.m_href);
                results << ValidationResult(error_type, opfpath, -1, -1, msg);
                me.m_href = Utility::URLEncodePath(
                    Utility::relativePath(originalBkpath, opf->GetFolder()));
                manifest_actions << ManifestItem_ReplaceHref;
                replacement_hrefs << me.m_href;
                opf_changed = true;
            } else {
                manifest_actions << ManifestItem_Keep;
                replacement_hrefs << QString();
            }
        }

        if (kept_manifest_ids.contains(id)) {
            const QString msg =
                utf8("OPF规范：非唯一ID：在manifest项发现非唯一ID【%1】，已进行删除对应项的处理。")
                .arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            manifest_actions.last() = ManifestItem_Remove;
            replacement_hrefs.last().clear();
            opf_changed = true;
            continue;
        }
        if (kept_manifest_keys.contains(key)) {
            const QString msg =
                utf8("OPF规范：多余ID：在manifest项发现多余ID【%1】，已进行删除对应项的处理。")
                .arg(id);
            results << ValidationResult(error_type, opfpath, -1, -1, msg);
            manifest_actions.last() = ManifestItem_Remove;
            replacement_hrefs.last().clear();
            opf_changed = true;
            continue;
        }

        kept_manifest_ids.insert(id);
        kept_manifest_keys.insert(key);
        new_manifest.append(me);
    }

    QStringList allIdsWithoutDuplication;
    foreach(ManifestEntry me, new_manifest) {
        allIdsWithoutDuplication << me.m_id;
    }

    QSet<QString> bookpathKeysOnManifest;
    foreach(ManifestEntry me, new_manifest) {
        bookpathKeysOnManifest.insert(hrefKey(me.m_href));
    }

    struct PendingResource
    {
        QString filepath;
        QString bookpath;
        QString media_type;
        QString html_text;
        bool is_html;
    };

    QList<PendingResource> pending_resources;
    QList<ManifestEntry> added_manifest_entries;
    QList<SpineEntry> added_spine_entries;
    bool refreshBookBrowser = false;
    foreach(QString filepath, filepathList) {
        const QString bkpath = filepath.mid(tempfolder.size() + 1);
        const QString lowerBkpath = bkpath.toLower();
        const QString key = ambiguousLowerBkPaths.contains(lowerBkpath) ? bkpath : lowerBkpath;
        if (bookpathKeysOnManifest.contains(key)) {
            continue;
        }

        const QFileInfo bkpath_info(bkpath);
        QString ext = bkpath_info.suffix().toLower();
        const QString media_type = Utility::ExtToMTypeMap(ext);
        if (ext == "xml" || ext == "opf" || media_type.isEmpty()) {
            continue;
        }

        if (!folder_keeper->GetAllBookPaths().contains(bkpath)) {
            PendingResource pending;
            pending.filepath = filepath;
            pending.bookpath = bkpath;
            pending.media_type = media_type;
            pending.is_html = media_type == QLatin1String("application/xhtml+xml");
            if (pending.is_html) {
                // Read potentially fallible content before changing FolderKeeper.
                pending.html_text = HTMLEncodingResolver::ReadHTMLFile(filepath);
            }
            pending_resources << pending;
        }

        ManifestEntry me;
        me.m_href = Utility::URLEncodePath(
            Utility::buildRelativePath(opf->GetRelativePath(), bkpath));
        QString unique_id = bkpath_info.baseName();
        while (allIdsWithoutDuplication.contains(unique_id)) {
            unique_id.prepend("_");
        }
        allIdsWithoutDuplication << unique_id;
        me.m_id = unique_id;
        me.m_mtype = media_type;
        new_manifest << me;
        added_manifest_entries << me;

        if (me.m_mtype == "application/xhtml+xml" &&
            !spineIdrefList.contains(me.m_id)) {
            SpineEntry se;
            se.m_idref = me.m_id;
            p.m_spine << se;
            added_spine_entries << se;
        }

        refreshBookBrowser = true;
        opf_changed = true;
        const QString msg =
            utf8("OPF规范：发现文件【%1】未登记到Manifest，已自动登记。").arg(bkpath);
        results << ValidationResult(warning_type, opfpath, -1, -1, msg);
    }

    if (!cover_idref.isEmpty() &&
        !allIdsWithoutDuplication.contains(cover_idref)) {
        const QString msg =
            utf8("OPF规范：无效ID引用：在meta项发现无效引用ID【%1】，建议检查metadata对应引用处并手动修改。")
            .arg(cover_idref);
        results << ValidationResult(warning_type, opfpath, -1, -1, msg);
    }

    foreach(SpineEntry se, p.m_spine) {
        if (!allIdsWithoutDuplication.contains(se.m_idref)) {
            const QString msg =
                utf8("OPF规范：无效ID引用：在spine项发现无效引用ID【%1】，建议检查spine对应引用处并手动修改。")
                .arg(se.m_idref);
            results << ValidationResult(warning_type, opfpath, -1, -1, msg);
        }
    }

    if (opf_changed) {
        QString rewrite_error;
        const QString new_opf = RewriteOPFPreservingExtensions(
            opf_source, manifest_actions, replacement_hrefs,
            added_manifest_entries, added_spine_entries, rewrite_error);
        if (new_opf.trimmed().isEmpty()) {
            results << ValidationResult(
                error_type, opfpath, -1, -1,
                utf8("OPF规范：无法安全写入规范化结果，已保留原OPF内容：%1")
                .arg(rewrite_error));
            return results;
        }

        QList<Resource *> registered_resources;
        const auto rollback_registered_resources = [&]() {
            foreach(Resource *resource, registered_resources) {
                folder_keeper->DiscardResourceRegistration(resource);
            }
            registered_resources.clear();
        };

        try {
            foreach(const PendingResource &pending, pending_resources) {
                Resource *resource = folder_keeper->AddContentFileToFolder(
                    pending.filepath, false, pending.media_type, pending.bookpath);
                if (!resource) {
                    rollback_registered_resources();
                    const QString msg =
                        utf8("OPF规范：无法将未登记文件【%1】加入书籍，规范化结果未应用。")
                        .arg(pending.bookpath);
                    results << ValidationResult(error_type, opfpath, -1, -1, msg);
                    return results;
                }
                registered_resources << resource;

                if (pending.is_html) {
                    HTMLResource *hresource = qobject_cast<HTMLResource *>(resource);
                    if (!hresource) {
                        rollback_registered_resources();
                        const QString msg =
                            utf8("OPF规范：无法读取未登记XHTML文件【%1】，规范化结果未应用。")
                            .arg(pending.bookpath);
                        results << ValidationResult(error_type, opfpath, -1, -1, msg);
                        return results;
                    }
                    hresource->SetText(pending.html_text);
                }
            }

            opf->SetText(new_opf);
        } catch (...) {
            rollback_registered_resources();
            throw;
        }

        m_Book->SetModified(true);
        if (refreshBookBrowser && m_BookBrowser) {
            m_BookBrowser->Refresh();
        }
    }

    return results;
}
//-------------------------------------------------------------------------------------------------------------------------


void ValidationResultsView::ValidateCurrentBook()
{
    ClearResults();
    QList<ValidationResult> results;

    if (m_Book.isNull() || !m_Book->GetFolderKeeper()) {
        DisplayResults(results);
        show();
        raise();
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        m_Book->SaveAllResourcesToDisk();

        const QList<Resource *> resources =
            m_Book->GetFolderKeeper()->GetResourceList();
        foreach(Resource *resource, resources) {
            if (!resource || resource->Type() != Resource::HTMLResourceType) {
                continue;
            }

            const QString source =
                Utility::ReadUnicodeTextFile(resource->GetFullPath());
            const XhtmlDoc::WellFormedError error =
                XhtmlDoc::WellFormedErrorForSource(source, resource->GetEpubVersion());
            if (error.line != -1) {
                results.append(ValidationResult(
                    ValidationResult::ResType_Error,
                    resource->GetRelativePath(),
                    error.line,
                    error.column,
                    error.message));
            }
        }

        results += correctOPF();
    } catch (const std::exception &error) {
        results.append(ValidationResult(
            ValidationResult::ResType_Error, QString(), -1, -1,
            tr("EPUB well-formedness check failed: %1").arg(QString::fromLocal8Bit(error.what()))));
    } catch (...) {
        results.append(ValidationResult(
            ValidationResult::ResType_Error, QString(), -1, -1,
            tr("EPUB well-formedness check failed because of an unknown error.")));
    }

    QApplication::restoreOverrideCursor();
    DisplayResults(results);
    show();
    raise();
}


void ValidationResultsView::LoadResults(const QList<ValidationResult> &results)
{
    ClearResults();
    DisplayResults(results);
    show();
    raise();
}


void ValidationResultsView::ClearResults()
{
    m_ResultTable->clearContents();
    m_ResultTable->setRowCount(0);
}


void ValidationResultsView::SetBook(QSharedPointer<Book> book)
{
    m_Book = book;
    ClearResults();
}

//------------------------modified: correctOPF-------------------------
void ValidationResultsView::SetBookBrowser(BookBrowser* bookbrowser)
{
    m_BookBrowser = bookbrowser;
    ClearResults();
}
//---------------------------------------------------------------------

void ValidationResultsView::ResultDoubleClicked(QTableWidgetItem *item)
{
    Q_ASSERT(item);
    int row = item->row();
    QTableWidgetItem *path_item = m_ResultTable->item(row, 0);

    if (!path_item) {
        return;
    }

    QString shortname = path_item->text();
    QString bookpath = path_item->data(Qt::UserRole+1).toString();
    QTableWidgetItem *line_item = m_ResultTable->item(row, 1);
    QTableWidgetItem *offset_item = m_ResultTable->item(row, 2);

    if (!line_item || !offset_item) {
        return;
    }


    int line = line_item->text() != "N/A" ? line_item->text().toInt(): -1;
    int charoffset = offset_item->text() != "N/A" ? offset_item->text().toInt(): -1;

    try {
        Resource *resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(bookpath);
        // if character offset info exists, use it in preference to just the line number
        if (charoffset != -1) {
            emit OpenResourceRequest(resource, line, charoffset, QString());
        } else {
            emit OpenResourceRequest(resource, line, -1, QString());
        }
    } catch (ResourceDoesNotExist&) {
        return;
    }
}


void ValidationResultsView::SetUpTable()
{
    m_ResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ResultTable->setTabKeyNavigation(false);
    m_ResultTable->setDropIndicatorShown(false);
    m_ResultTable->horizontalHeader()->setStretchLastSection(true);
    m_ResultTable->verticalHeader()->setVisible(false);
}


void ValidationResultsView::DisplayResults(const QList<ValidationResult> &results)
{
    m_ResultTable->clear();
    m_NoProblems = false;

    if (results.empty()) {
        m_NoProblems = true;
        DisplayNoProblemsMessage();
        return;
    }

    ConfigureTableForResults();

    Q_FOREACH(ValidationResult result, results) {
        int rownum = m_ResultTable->rowCount();
        QTableWidgetItem *item = NULL;

        QBrush row_brush = Utility::ValidationResultBrush(Utility::INFO_BRUSH);
        if (result.Type() == ValidationResult::ResType_Warn) {
            row_brush = Utility::ValidationResultBrush(Utility::WARNING_BRUSH);
        } else if (result.Type() == ValidationResult::ResType_Error) {
            row_brush = Utility::ValidationResultBrush(Utility::ERROR_BRUSH);
        }

        m_ResultTable->insertRow(rownum);
 
        QString path;
        QString bookpath = result.BookPath();
        try {
            Resource * resource = m_Book->GetFolderKeeper()->GetResourceByBookPath(bookpath);
            path = resource->ShortPathName();
        } catch (ResourceDoesNotExist&) {
            if (bookpath.isEmpty()) {
                path = "***Invalid Book Path Provided ***";
            } else {
                path = bookpath;
            }
        }

        item = new QTableWidgetItem(RemoveEpubPathPrefix(path));
        item->setData(Qt::UserRole+1, bookpath);
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 0, item);

        item = result.LineNumber() > 0 ? new QTableWidgetItem(QString::number(result.LineNumber())) : new QTableWidgetItem("N/A");
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 1, item);

        item = result.CharOffset() >= 0 ? new QTableWidgetItem(QString::number(result.CharOffset())) : new QTableWidgetItem("N/A");
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 2, item);

        item = new QTableWidgetItem(result.Message());
        SetItemPalette(item, row_brush);
        m_ResultTable->setItem(rownum, 3, item);
    }

    // Make Line and Offset columns as small as possible
    // Ditto for Filename
    m_ResultTable->resizeColumnToContents(0);
    m_ResultTable->resizeColumnToContents(1);
    m_ResultTable->resizeColumnToContents(2);
    //m_ResultTable->resizeColumnsToContents();
}

int ValidationResultsView::ResultCount()
{
    if (m_NoProblems) return 0;
    return m_ResultTable->rowCount();
}

void ValidationResultsView::DisplayNoProblemsMessage()
{
    m_ResultTable->setRowCount(1);
    m_ResultTable->setColumnCount(1);
    m_ResultTable->setHorizontalHeaderLabels(
        QStringList() << tr("Message"));
    QTableWidgetItem *item = new QTableWidgetItem(tr("No problems found!"));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    QFont font = item->font();
    font.setPointSize(16);
    item->setFont(font);
    m_ResultTable->setItem(0, 0, item);
    m_ResultTable->resizeRowToContents(0);
}


void ValidationResultsView::ConfigureTableForResults()
{
    m_ResultTable->setRowCount(0);
    m_ResultTable->setColumnCount(4);
    m_ResultTable->setHorizontalHeaderLabels(
    QStringList() << tr("File") << tr("Line") << tr("Offset") << tr("Message"));
    m_ResultTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_ResultTable->setSortingEnabled(true);
    m_ResultTable->horizontalHeader()->setSortIndicatorShown(true);

}


QString ValidationResultsView::RemoveEpubPathPrefix(const QString &path)
{
    return QString(path).remove(QRegularExpression("^[\\w-]+\\.epub/?"));
}

void ValidationResultsView::SetItemPalette(QTableWidgetItem * item, QBrush &row_brush)
{
    if (Utility::IsDarkMode()) {
        item->setForeground(row_brush);
    } else {
        item->setBackground(row_brush);
    }   
}
