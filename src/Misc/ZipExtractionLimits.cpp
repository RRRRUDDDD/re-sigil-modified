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

#include "Misc/ZipExtractionLimits.h"

ZipExtractionLimits ZipExtractionLimits::Default()
{
    ZipExtractionLimits limits;
    limits.max_entries = uint64_t(10000);
    limits.max_file_uncompressed_bytes = uint64_t(512) * 1024 * 1024;
    limits.max_total_uncompressed_bytes = uint64_t(2) * 1024 * 1024 * 1024;
    limits.max_compression_ratio = uint64_t(1000);
    // Tiny, highly compressible text/resources are legitimate and bounded by
    // the size limits, so ratio enforcement begins after the first MiB.
    limits.compression_ratio_grace_bytes = uint64_t(1) * 1024 * 1024;
    return limits;
}

ZipExtractionResourceLimiter::ZipExtractionResourceLimiter(const ZipExtractionLimits &limits)
    : m_Limits(limits),
      m_EntryCount(0),
      m_DeclaredTotalBytes(0),
      m_ActualTotalBytes(0),
      m_CurrentDeclaredBytes(0),
      m_CurrentEntryBytes(0)
{
}

bool ZipExtractionResourceLimiter::AddWithinLimit(uint64_t current,
                                                  uint64_t increment,
                                                  uint64_t limit,
                                                  uint64_t *result)
{
    if (current > limit || increment > limit - current) {
        return false;
    }
    if (result) {
        *result = current + increment;
    }
    return true;
}

bool ZipExtractionResourceLimiter::CompressionRatioAllowed(uint64_t compressed_bytes,
                                                            uint64_t uncompressed_bytes) const
{
    if (uncompressed_bytes == 0) {
        return true;
    }
    if (compressed_bytes == 0) {
        return false;
    }
    if (uncompressed_bytes <= m_Limits.compression_ratio_grace_bytes) {
        return true;
    }

    // Check: uncompressed <= grace + compressed * ratio.  Division and a
    // remainder avoid overflowing either the multiplication or the sum.
    const uint64_t bytes_subject_to_ratio =
        uncompressed_bytes - m_Limits.compression_ratio_grace_bytes;
    const uint64_t quotient = bytes_subject_to_ratio / compressed_bytes;
    const uint64_t remainder = bytes_subject_to_ratio % compressed_bytes;
    return quotient < m_Limits.max_compression_ratio ||
           (quotient == m_Limits.max_compression_ratio && remainder == 0);
}

bool ZipExtractionResourceLimiter::BeginEntry(uint64_t compressed_bytes,
                                               uint64_t uncompressed_bytes,
                                               bool is_directory,
                                               uint64_t output_copies,
                                               std::string *error)
{
    uint64_t next_entry_count = 0;
    if (!AddWithinLimit(m_EntryCount, 1, m_Limits.max_entries, &next_entry_count)) {
        if (error) {
            *error = "entry count limit exceeded (" +
                     std::to_string(m_Limits.max_entries) + " entries)";
        }
        return false;
    }

    if (!is_directory && uncompressed_bytes > m_Limits.max_file_uncompressed_bytes) {
        if (error) {
            *error = "single-file uncompressed size limit exceeded (" +
                     std::to_string(m_Limits.max_file_uncompressed_bytes) + " bytes)";
        }
        return false;
    }

    if (!is_directory && !CompressionRatioAllowed(compressed_bytes, uncompressed_bytes)) {
        if (error) {
            if (compressed_bytes == 0 && uncompressed_bytes != 0) {
                *error = "non-empty entry declares zero compressed size";
            } else {
                *error = "compression ratio limit exceeded (" +
                         std::to_string(m_Limits.max_compression_ratio) +
                         ":1 after " +
                         std::to_string(m_Limits.compression_ratio_grace_bytes) +
                         " grace bytes)";
            }
        }
        return false;
    }

    uint64_t next_declared_total = m_DeclaredTotalBytes;
    if (!is_directory && uncompressed_bytes != 0 && output_copies != 0) {
        if (next_declared_total > m_Limits.max_total_uncompressed_bytes ||
            output_copies > (m_Limits.max_total_uncompressed_bytes - next_declared_total) /
                            uncompressed_bytes) {
            if (error) {
                *error = "total declared uncompressed size limit exceeded (" +
                         std::to_string(m_Limits.max_total_uncompressed_bytes) + " bytes)";
            }
            return false;
        }
        const uint64_t declared_increment = uncompressed_bytes * output_copies;
        next_declared_total += declared_increment;
    }

    m_EntryCount = next_entry_count;
    m_DeclaredTotalBytes = next_declared_total;
    m_CurrentDeclaredBytes = is_directory ? 0 : uncompressed_bytes;
    m_CurrentEntryBytes = 0;
    return true;
}

bool ZipExtractionResourceLimiter::AccountExtractedBytes(uint64_t bytes,
                                                         std::string *error)
{
    uint64_t next_entry_bytes = 0;
    if (!AddWithinLimit(m_CurrentEntryBytes, bytes, m_CurrentDeclaredBytes,
                        &next_entry_bytes)) {
        if (error) {
            *error = "actual extracted size exceeds declared uncompressed size (" +
                     std::to_string(m_CurrentDeclaredBytes) + " bytes)";
        }
        return false;
    }
    if (next_entry_bytes > m_Limits.max_file_uncompressed_bytes) {
        if (error) {
            *error = "actual single-file size limit exceeded (" +
                     std::to_string(m_Limits.max_file_uncompressed_bytes) + " bytes)";
        }
        return false;
    }

    uint64_t next_actual_total = 0;
    if (!AddWithinLimit(m_ActualTotalBytes, bytes,
                        m_Limits.max_total_uncompressed_bytes,
                        &next_actual_total)) {
        if (error) {
            *error = "actual total extracted size limit exceeded (" +
                     std::to_string(m_Limits.max_total_uncompressed_bytes) + " bytes)";
        }
        return false;
    }

    m_CurrentEntryBytes = next_entry_bytes;
    m_ActualTotalBytes = next_actual_total;
    return true;
}

bool ZipExtractionResourceLimiter::AccountAdditionalOutputBytes(uint64_t bytes,
                                                                std::string *error)
{
    uint64_t next_actual_total = 0;
    if (!AddWithinLimit(m_ActualTotalBytes, bytes,
                        m_Limits.max_total_uncompressed_bytes,
                        &next_actual_total)) {
        if (error) {
            *error = "actual total extracted size limit exceeded by filename alias (" +
                     std::to_string(m_Limits.max_total_uncompressed_bytes) + " bytes)";
        }
        return false;
    }
    m_ActualTotalBytes = next_actual_total;
    return true;
}

uint64_t ZipExtractionResourceLimiter::EntryCount() const
{
    return m_EntryCount;
}

uint64_t ZipExtractionResourceLimiter::DeclaredTotalBytes() const
{
    return m_DeclaredTotalBytes;
}

uint64_t ZipExtractionResourceLimiter::ActualTotalBytes() const
{
    return m_ActualTotalBytes;
}

uint64_t ZipExtractionResourceLimiter::CurrentEntryBytes() const
{
    return m_CurrentEntryBytes;
}
