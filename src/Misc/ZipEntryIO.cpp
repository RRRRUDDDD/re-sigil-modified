/************************************************************************
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include <string.h>

#include "unzip.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QTime>

#include "Misc/QCodePage437Codec.h"
#include "Misc/ZipEntryIO.h"

// The ZIP format stores the name length in a 16 bit field, so a name anywhere
// near this is already malformed.  The cap only keeps a corrupt header from
// asking for an absurd allocation and from producing a path no file system
// could ever hold.
static const uLong MAX_ENTRY_NAME_BYTES = 4096;

// This is the same read buffer size used by Java and Perl.
static const int BUFF_SIZE = 8192;

static QCodePage437Codec *cp437 = 0;

bool ZipEntryIO::ReadCurrentEntry(void *zfile, ZipEntryInfo *info, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!zfile || !info) {
        return false;
    }
    *info = ZipEntryInfo();

    unz_file_info64 file_info;
    memset(&file_info, 0, sizeof(file_info));

    // First pass without a name buffer, purely to learn how long the name
    // really is.  Reading into a fixed buffer would truncate longer names.
    if (unzGetCurrentFileInfo64(static_cast<unzFile>(zfile), &file_info,
                                NULL, 0, NULL, 0, NULL, 0) != UNZ_OK) {
        if (error) {
            *error = QCoreApplication::translate("ZipEntryIO",
                         "the entry header could not be read");
        }
        return false;
    }

    if (file_info.size_filename > MAX_ENTRY_NAME_BYTES) {
        if (error) {
            *error = QCoreApplication::translate("ZipEntryIO",
                         "the entry name is %1 bytes long")
                         .arg(quint64(file_info.size_filename));
        }
        return false;
    }

    // minizip only terminates the name when it is strictly shorter than the
    // buffer, so the buffer has to hold one byte more than the name itself.
    QByteArray raw_name(int(file_info.size_filename) + 1, '\0');
    if (unzGetCurrentFileInfo64(static_cast<unzFile>(zfile), &file_info,
                                raw_name.data(), uLong(raw_name.size()),
                                NULL, 0, NULL, 0) != UNZ_OK) {
        if (error) {
            *error = QCoreApplication::translate("ZipEntryIO",
                         "the entry name could not be read");
        }
        return false;
    }

    const int name_length = int(file_info.size_filename);

    // An embedded NUL makes the name decode into something shorter than what
    // the archive actually declares, so every later containment and duplicate
    // check would be made against the wrong path.
    if (raw_name.indexOf('\0') != name_length) {
        if (error) {
            *error = QCoreApplication::translate("ZipEntryIO",
                         "the entry name contains an embedded NUL byte");
        }
        return false;
    }

    info->name = QString::fromUtf8(raw_name.constData(), name_length);
    if (!(file_info.flag & (1 << 11))) {
        // General purpose bit 11 says the filename is utf-8 encoded. If not set
        // then IBM 437 encoding might be used.
        if (!cp437) {
            cp437 = new QCodePage437Codec();
        }
        info->cp437_name = cp437->toUnicode(raw_name.constData(), name_length);
    }

    info->compressed_size = quint64(file_info.compressed_size);
    info->uncompressed_size = quint64(file_info.uncompressed_size);
    info->crc = quint32(file_info.crc);

    const QDate moddate(file_info.tmu_date.tm_year,
                        file_info.tmu_date.tm_mon + 1,
                        file_info.tmu_date.tm_mday);
    const QTime modtime(file_info.tmu_date.tm_hour,
                        file_info.tmu_date.tm_min,
                        file_info.tmu_date.tm_sec);
    info->modified = QDateTime(moddate, modtime);
    info->is_directory = (file_info.uncompressed_size == 0) && info->name.endsWith('/');
    return true;
}

bool ZipEntryIO::SanitizePath(const QString &raw_name, QString *sanitized)
{
    // For security reasons against maliciously crafted zip archives we need the
    // file path to always be inside the target folder and not outside, so we
    // remove all illegal backslashes and all relative upward path segments
    // "/../" from the zip's local file name/path before the target folder is
    // prepended to create the final path.
    QString name = raw_name;
    bool evil_or_corrupt = false;

    if (name.contains("\\")) evil_or_corrupt = true;
    name = "/" + name.replace("\\", "");

    if (name.contains("/../")) evil_or_corrupt = true;
    name = name.replace("/../", "/");

    while (name.startsWith("/")) {
        name = name.remove(0, 1);
    }

    if (sanitized) {
        *sanitized = name;
    }
    return !evil_or_corrupt;
}

bool ZipEntryIO::ReserveOutputPath(QSet<QString> *reserved, const QString &path)
{
    if (!reserved) {
        return true;
    }
    if (reserved->contains(path)) {
        return false;
    }
    reserved->insert(path);
    return true;
}

ZipEntryIO::ExtractResult ZipEntryIO::ExtractCurrentEntry(
    void *zfile,
    const QString &output_path,
    ZipExtractionResourceLimiter *limiter)
{
    ExtractResult result;

    if (unzOpenCurrentFile(static_cast<unzFile>(zfile)) != UNZ_OK) {
        result.status = Status::ArchiveEntryOpenFailed;
        return result;
    }

    QFile entry(output_path);

    if (!entry.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        unzCloseCurrentFile(static_cast<unzFile>(zfile));
        result.status = Status::OutputOpenFailed;
        return result;
    }

    // Buffered reading and writing.
    char buff[BUFF_SIZE] = {0};
    int read = 0;
    qint64 written_bytes = 0;

    while ((read = unzReadCurrentFile(static_cast<unzFile>(zfile), buff, BUFF_SIZE)) > 0) {
        if (limiter && !limiter->AccountExtractedBytes(quint64(read), &result.limit_error)) {
            result.status = Status::LimitExceeded;
            break;
        }
        // A short write means those bytes never reached the disk (full volume,
        // quota, I/O error).  The entry CRC only proves the compressed stream
        // was read correctly, so it can never catch a failed write.
        if (entry.write(buff, read) != qint64(read)) {
            result.status = Status::WriteFailed;
            break;
        }
        written_bytes += read;
    }

    // Read errors are marked by a negative read amount.
    if (result.status == Status::Ok && read < 0) {
        result.status = Status::ReadFailed;
    }

    if (result.status == Status::Ok && (!entry.flush() || entry.error() != QFile::NoError)) {
        result.status = Status::WriteFailed;
    }

    entry.close();

    // A successful close clears the device error, so this only reports a
    // failure of the close itself; earlier errors were captured above.
    if (result.status == Status::Ok && entry.error() != QFile::NoError) {
        result.status = Status::WriteFailed;
    }

    // The file was read but the CRC did not match.
    // We don't check the read file size vs the uncompressed file size
    // because if they're different there should be a CRC error.
    const int close_result = unzCloseCurrentFile(static_cast<unzFile>(zfile));
    if (result.status == Status::Ok && close_result == UNZ_CRCERROR) {
        result.status = Status::ReadFailed;
    }

    // Catches a truncated or short-written output: the size the filesystem
    // reports after close must match what we streamed.  This is not a
    // durability guarantee -- nothing here forces an fsync.
    if (result.status == Status::Ok && QFileInfo(output_path).size() != written_bytes) {
        result.status = Status::WriteFailed;
    }

    if (result.status != Status::Ok) {
        entry.remove();
    }

    return result;
}
