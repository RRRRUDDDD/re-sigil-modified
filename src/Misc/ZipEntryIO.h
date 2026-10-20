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

#pragma once
#ifndef ZIP_ENTRY_IO_H
#define ZIP_ENTRY_IO_H

#include <string>

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include "Misc/ZipExtractionLimits.h"

// Metadata for the archive entry an open minizip handle is currently
// positioned on.  The name is always read in full: a fixed size buffer
// silently truncates long names, which makes two different entries collapse
// onto a single output path.
struct ZipEntryInfo
{
    QString name;               // the entry name as declared, decoded as UTF-8
    QString cp437_name;         // IBM437 reading, empty when the entry flags UTF-8
    quint64 compressed_size;
    quint64 uncompressed_size;
    quint32 crc;
    QDateTime modified;
    bool is_directory;

    ZipEntryInfo()
        : compressed_size(0),
          uncompressed_size(0),
          crc(0),
          is_directory(false)
    {
    }
};

// Shared handling for the untrusted entries of a ZIP/EPUB archive.  The EPUB
// importer and the plugin unzip path both go through here so their metadata,
// path and write integrity checks can never drift apart.
namespace ZipEntryIO
{

enum class Status {
    Ok,
    ArchiveEntryOpenFailed,
    OutputOpenFailed,
    ReadFailed,
    WriteFailed,
    LimitExceeded
};

struct ExtractResult
{
    Status status;
    std::string limit_error;    // only set when status is LimitExceeded

    ExtractResult() : status(Status::Ok) {}
};

// Reads the metadata of the entry `zfile` is currently positioned on.  `zfile`
// is an open minizip unzFile.  Returns false with *error set when minizip
// fails, when the name is absurdly long, or when it contains an embedded NUL
// (which would silently truncate every later path comparison).
bool ReadCurrentEntry(void *zfile, ZipEntryInfo *info, QString *error);

// Removes the separators and upward path segments an archive name must never
// contain, so the result can only ever resolve inside the extraction folder.
// Returns false when the raw name was evil or corrupt; *sanitized is still
// filled in so the caller can report it.
bool SanitizePath(const QString &raw_name, QString *sanitized);

// Refuses a second entry that would write an output path an earlier entry has
// already produced: that is how a crafted archive replaces a file which has
// already passed inspection.  Returns false when `path` was already taken.
bool ReserveOutputPath(QSet<QString> *reserved, const QString &path);

// Streams the current entry to `output_path`, enforcing `limiter` and
// requiring every byte to actually reach the disk.  Owns the whole
// open/read/close cycle of the archive entry: the entry is always closed when
// this returns, and a failed extraction never leaves a partial output file
// behind.
ExtractResult ExtractCurrentEntry(void *zfile,
                                  const QString &output_path,
                                  ZipExtractionResourceLimiter *limiter);

}

#endif // ZIP_ENTRY_IO_H
