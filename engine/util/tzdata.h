// The POSIX TZ rule for an IANA time zone name (what a phone's browser reports), e.g.
// "Asia/Jerusalem" -> "IST-2IDT,M3.4.4/26,M10.5.0". nullptr if unknown. Table: tools/tzdata.py.
#pragma once
#include <string>

namespace nestbox {

const char* posix_tz(const std::string& iana);

}  // namespace nestbox
