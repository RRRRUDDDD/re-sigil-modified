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
#ifndef ZIP_EXTRACTION_LIMITS_H
#define ZIP_EXTRACTION_LIMITS_H

#include <stdint.h>
#include <string>

// Shared resource policy for every untrusted ZIP extraction path.  The
// defaults allow unusually large EPUBs and plugins while placing a finite
// ceiling on disk/CPU amplification.  Tests can inject much smaller values.
struct ZipExtractionLimits
{
    uint64_t max_entries;
    uint64_t max_file_uncompressed_bytes;
    uint64_t max_total_uncompressed_bytes;
    uint64_t max_compression_ratio;
    uint64_t compression_ratio_grace_bytes;

    static ZipExtractionLimits Default();
};

// Tracks both untrusted declarations and bytes actually produced by minizip.
// All additions and multiplications are checked before they are performed.
class ZipExtractionResourceLimiter
{
public:
    explicit ZipExtractionResourceLimiter(const ZipExtractionLimits &limits);

    bool BeginEntry(uint64_t compressed_bytes,
                    uint64_t uncompressed_bytes,
                    bool is_directory,
                    uint64_t output_copies,
                    std::string *error);

    bool AccountExtractedBytes(uint64_t bytes, std::string *error);
    bool AccountAdditionalOutputBytes(uint64_t bytes, std::string *error);

    uint64_t EntryCount() const;
    uint64_t DeclaredTotalBytes() const;
    uint64_t ActualTotalBytes() const;
    uint64_t CurrentEntryBytes() const;

private:
    static bool AddWithinLimit(uint64_t current,
                               uint64_t increment,
                               uint64_t limit,
                               uint64_t *result);
    bool CompressionRatioAllowed(uint64_t compressed_bytes,
                                 uint64_t uncompressed_bytes) const;

    ZipExtractionLimits m_Limits;
    uint64_t m_EntryCount;
    uint64_t m_DeclaredTotalBytes;
    uint64_t m_ActualTotalBytes;
    uint64_t m_CurrentDeclaredBytes;
    uint64_t m_CurrentEntryBytes;
};

#endif // ZIP_EXTRACTION_LIMITS_H
