#pragma once

#include <cstddef>

/// @cond INTERNAL
/**
 * One registered third-party component, as written by cmake/ThirdPartyNotices.cmake into the
 * generated third_party_notices.cpp. Components with byte-identical license files point at the
 * same text.
 */
struct NoticeRecord {
    const char *name;
    const char *spdx;
    const char *version;
    const char *url;
    const char *text;
    size_t      textLength;
};

// Defined in the generated third_party_notices.cpp.
// NOLINTBEGIN(readability-identifier-naming)
extern const NoticeRecord LUMI_NOTICE_RECORDS[];
extern const size_t       LUMI_NOTICE_RECORD_COUNT;
// NOLINTEND(readability-identifier-naming)
/// @endcond
