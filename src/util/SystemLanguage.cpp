/*
 * Copyright (C) 2026 The Coral Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <coral/util/SystemLanguage.h>

#include <algorithm>
#include <cctype>
#include <locale>
#include <utility>

namespace coral {
namespace SystemLanguage {
namespace {

/**
 * Extracts the language of a locale name.
 *
 * <p>A locale name looks like {@code "de_DE.UTF-8"} on a POSIX system and like {@code
 * "German_Germany.1252"} on Windows, so the language is the leading run of letters, truncated to the
 * two letters of an ISO 639 code: the names of the C++ locales are the names of the platform, and the
 * two letter prefix is the only part that both spellings agree on.
 *
 * @param localeName The name of the locale, or an empty string when it is unknown.
 */
std::string toLanguageCode(const std::string& localeName) {
  auto code = std::string();
  for (const auto character : localeName) {
    if (code.size() == 2) {
      break;
    }
    if (std::isalpha(static_cast<unsigned char>(character)) == 0) {
      break;
    }
    code.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  // A name that starts with a separator carries no language at all.
  return code.size() == 2 ? code : std::string();
}

std::vector<std::string>& languageCodes() {
  // Established once: the locale of a process does not change while it runs, and a track selection
  // asks for this on every format it scores.
  static auto codes = [] {
    auto result = std::vector<std::string>();
    try {
      const auto localeName = std::locale("").name();
      // The C locale, and the neutral POSIX locale, name no language.
      if (localeName != "C" && localeName != "POSIX") {
        auto code = toLanguageCode(localeName);
        if (!code.empty()) {
          result.push_back(std::move(code));
        }
      }
    } catch (const std::exception&) {
      // The locale of the process cannot be established, so no language is preferred.
    }
    return result;
  }();
  return codes;
}

} // namespace anonymous

const std::vector<std::string>& getLanguageCodes() {
  return languageCodes();
}

void setLanguageCodes(std::vector<std::string> newLanguageCodes) {
  languageCodes() = std::move(newLanguageCodes);
}

} // namespace SystemLanguage
} // namespace coral
