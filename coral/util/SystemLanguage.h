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
#ifndef CORAL_PLAYBACK_UTIL_SYSTEM_LANGUAGE_H
#define CORAL_PLAYBACK_UTIL_SYSTEM_LANGUAGE_H

#include <string>
#include <vector>

namespace coral {

/**
 * The languages of the system, most preferred first.
 *
 * <p>Mirrors ExoPlayer's {@code Util.getSystemLanguageCodes()}: a track selection uses them to prefer
 * the audio of the language the user of the device reads, when the application expressed no
 * preference of its own.
 *
 * <p>The list is established once, from the locale of the process, and can be replaced by an
 * application that knows better - a platform without a usable locale, or an application that was told
 * the user's languages. An empty list means "no system language is known", and then no track is
 * preferred on that basis.
 */
namespace SystemLanguage {

/**
 * Returns the language codes of the system, most preferred first.
 *
 * <p>The codes are ISO 639 two letter codes in lower case, e.g. {@code "de"}, or the codes that were
 * set with {@link #setLanguageCodes}.
 */
const std::vector<std::string>& getLanguageCodes();

/**
 * Replaces the language codes of the system.
 *
 * @param languageCodes The codes, most preferred first. Empty clears the preference.
 */
void setLanguageCodes(std::vector<std::string> languageCodes);

} // namespace SystemLanguage

} // namespace coral

#endif // CORAL_PLAYBACK_UTIL_SYSTEM_LANGUAGE_H
