// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace openstrata::gs {

// Every diagnostic message starts with its bracketed code so text consumers
// and machine consumers see one stable spelling: "[GSPLY-E003] ...". Each
// format bundle owns its own code namespace (GSPLY-****, GSPZ-****); this
// helper only fixes how a code and a message are joined, so the spelling
// cannot drift between bundles.
inline std::string FormatDiagnostic(const char* code, const std::string& message)
{
    std::string result;
    result.reserve(message.size() + 16);
    result += '[';
    result += code;
    result += "] ";
    result += message;
    return result;
}

} // namespace openstrata::gs
