/* settings.h: Functions for managing settings.
 *
 * Copyright (C) 2014-2017 by Eric Schmidt, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#ifndef HEADER_settings_h_
#define HEADER_settings_h_

#ifdef __cplusplus
extern "C" {
#endif

void loadsettings(void);

void savesettings(void);

/* Obtain integer setting. Returns -1 if setting doesn't exist or cannot be
 * parsed as an int. */
int getintsetting(char const * name);
void setintsetting(char const * name, int val);

/* Obtain a string setting. Returned pointer is good until the setting is
   modified. Returns NULL if the setting doesn't exist. */
char const * getstringsetting(char const * name);
void setstringsetting(char const * name, char const * val);

/* MOD (Jeremy, jc-37): TRUE when an opt-in switch is on. Strictly opt-in -- only "1" or "true"
 * (any casing, surrounding whitespace ignored) count; absent, blank, "0", garbage and a missing
 * settings file all mean off. This is the single shared definition, used by both the portable
 * core and TileWorldApp::SettingOptedIn(). */
int settingoptedin(char const * name);

/* MOD (Jeremy, jc-37): FALSE when the settings file exists but could not be read, in which case
 * the settings map is empty and savesettings() will refuse to write. Only callers that must tell
 * "no value yet" apart from "value unavailable" need this -- see the death counter. */
int settingsarereadable(void);

#ifdef __cplusplus
}
#endif

#endif
