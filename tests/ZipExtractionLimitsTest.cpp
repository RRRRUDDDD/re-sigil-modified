#include <iostream>
#include <limits>

#include "Misc/ZipExtractionLimits.h"

namespace {

int failures = 0;

void Check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        ++failures;
    }
}

ZipExtractionLimits Limits(uint64_t entries,
                           uint64_t file_bytes,
                           uint64_t total_bytes,
                           uint64_t ratio,
                           uint64_t ratio_grace)
{
    ZipExtractionLimits limits;
    limits.max_entries = entries;
    limits.max_file_uncompressed_bytes = file_bytes;
    limits.max_total_uncompressed_bytes = total_bytes;
    limits.max_compression_ratio = ratio;
    limits.compression_ratio_grace_bytes = ratio_grace;
    return limits;
}

void TestEntryCountBoundary()
{
    std::string error;
    ZipExtractionResourceLimiter limiter(Limits(2, 100, 100, 100, 0));
    Check(limiter.BeginEntry(0, 0, true, 0, &error),
          "directory at entry-count boundary should pass");
    Check(limiter.BeginEntry(1, 1, false, 1, &error),
          "file exactly at entry-count boundary should pass");
    Check(!limiter.BeginEntry(0, 0, true, 0, &error),
          "entry above entry-count boundary should fail");
}

void TestFileAndTotalBoundaries()
{
    std::string error;
    ZipExtractionResourceLimiter exact_file(Limits(10, 5, 20, 100, 0));
    Check(exact_file.BeginEntry(5, 5, false, 1, &error),
          "file exactly at size limit should pass");

    ZipExtractionResourceLimiter large_file(Limits(10, 5, 20, 100, 0));
    Check(!large_file.BeginEntry(6, 6, false, 1, &error),
          "file above size limit should fail");

    ZipExtractionResourceLimiter total(Limits(10, 10, 10, 100, 0));
    Check(total.BeginEntry(6, 6, false, 1, &error),
          "first file within total limit should pass");
    Check(total.BeginEntry(4, 4, false, 1, &error),
          "declared total exactly at limit should pass");
    Check(!total.BeginEntry(1, 1, false, 1, &error),
          "declared total above limit should fail");
}

void TestCompressionRatioBoundaries()
{
    std::string error;
    ZipExtractionResourceLimiter ratio(Limits(10, 1000, 1000, 10, 0));
    Check(ratio.BeginEntry(10, 100, false, 1, &error),
          "compression ratio exactly at limit should pass");

    ZipExtractionResourceLimiter high_ratio(Limits(10, 1000, 1000, 10, 0));
    Check(!high_ratio.BeginEntry(10, 101, false, 1, &error),
          "compression ratio above limit should fail");

    ZipExtractionResourceLimiter empty(Limits(10, 1000, 1000, 10, 64));
    Check(empty.BeginEntry(0, 0, false, 1, &error),
          "empty file with zero compressed size should pass");

    ZipExtractionResourceLimiter impossible(Limits(10, 1000, 1000, 10, 64));
    Check(!impossible.BeginEntry(0, 1, false, 1, &error),
          "non-empty file with zero compressed size should fail");

    ZipExtractionResourceLimiter small_file(Limits(10, 1000, 1000, 1, 64));
    Check(small_file.BeginEntry(1, 64, false, 1, &error),
          "small legitimate file inside ratio grace should pass");
}

void TestStreamingAndAliasAccounting()
{
    std::string error;
    ZipExtractionResourceLimiter stream(Limits(10, 5, 10, 100, 0));
    Check(stream.BeginEntry(5, 5, false, 1, &error),
          "streaming test metadata should pass");
    Check(stream.AccountExtractedBytes(5, &error),
          "actual stream exactly at declared size should pass");
    Check(!stream.AccountExtractedBytes(1, &error),
          "actual stream above declared size should fail");

    ZipExtractionResourceLimiter alias(Limits(10, 5, 10, 100, 0));
    Check(alias.BeginEntry(5, 5, false, 2, &error),
          "CP437 alias declared bytes exactly at total limit should pass");
    Check(alias.AccountExtractedBytes(5, &error),
          "primary alias output should count toward actual total");
    Check(alias.AccountAdditionalOutputBytes(5, &error),
          "alias copy exactly at actual total limit should pass");
    Check(!alias.AccountAdditionalOutputBytes(1, &error),
          "alias copy above actual total limit should fail");
}

void TestUnsignedOverflowBoundaries()
{
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    std::string error;

    ZipExtractionResourceLimiter ratio_math(Limits(10, maximum, maximum,
                                                    maximum, 0));
    Check(ratio_math.BeginEntry(maximum, maximum, false, 1, &error),
          "ratio comparison should not overflow compressed-by-ratio multiplication");

    ZipExtractionResourceLimiter alias_math(Limits(10, maximum, maximum,
                                                    maximum, maximum));
    Check(!alias_math.BeginEntry(maximum, maximum, false, 2, &error),
          "alias declared-byte multiplication beyond 64-bit maximum should fail");

    ZipExtractionResourceLimiter limiter(Limits(maximum, maximum, maximum,
                                                 maximum, maximum));
    Check(limiter.BeginEntry(maximum, maximum, false, 1, &error),
          "maximum 64-bit declared value should pass without overflow");
    Check(!limiter.BeginEntry(1, 1, false, 1, &error),
          "declared-total addition beyond 64-bit maximum should fail");
    Check(limiter.AccountExtractedBytes(maximum, &error),
          "maximum 64-bit actual value should pass without overflow");
    Check(!limiter.AccountAdditionalOutputBytes(1, &error),
          "actual-total addition beyond 64-bit maximum should fail");
}

} // namespace

int main()
{
    TestEntryCountBoundary();
    TestFileAndTotalBoundaries();
    TestCompressionRatioBoundaries();
    TestStreamingAndAliasAccounting();
    TestUnsignedOverflowBoundaries();

    if (failures == 0) {
        std::cout << "All ZIP extraction limiter tests passed." << std::endl;
    }
    return failures == 0 ? 0 : 1;
}
