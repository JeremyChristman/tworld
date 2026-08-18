/* settings.cpp: Functions for managing settings.
 *
 * Copyright (C) 2014-2017 by Eric Schmidt, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#include "settings.h"

#include "err.h"
#include "fileio.h"

#include <sys/stat.h>   /* MOD (Jeremy): tell "absent" apart from "exists but will not open" */

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

extern char *savedir;
extern char *appdir;   /* MOD (Jeremy): the program's own directory -- see sfname below */

using std::free;
using std::getline;
using std::ifstream;
using std::istringstream;
using std::map;
using std::move;
using std::ofstream;
using std::ostringstream;
using std::string;

namespace
{
    map<string, string> settings;

    /* MOD (Jeremy): true when the settings file EXISTS but could not be opened. In that state
     * savesettings() writes NOTHING, because the alternative is silent destruction: loadsettings()
     * leaves the map empty on a failed open, and the writer rewrites the WHOLE file from that map
     * at exit -- so one unreadable moment at startup would permanently erase bgcolor, the
     * remembered level set, the ruleset and the volume.
     *
     * This is not hypothetical here. The install lives in a Dropbox folder, and the laptop keeps
     * it online-only: a placeholder that has not hydrated yet, or Dropbox not up when the game
     * launches, is exactly an "exists but will not open" file. SuperCC had this same defect and
     * the same fix (see its jc-4 notes) -- absent and unreadable are different things, and only
     * the first one may be overwritten. */
    bool settingsUnreadable = false;

    /* MOD (Jeremy): the file's layout. Keys are grouped under [Section] headers so the shipped
     * tw_settings.ini reads as documentation rather than as an unsorted dump -- the sections are
     * PRESENTATION ONLY, they are not part of a key's identity, and loadsettings() ignores them.
     * A key missing from this table is not lost: savesettings() writes whatever is left over under
     * a final [Other] heading, so a setting added by a future upstream release survives a
     * round trip through this fork untouched. */
    /* The keys array is BOTH sentinel-terminated and length-bounded when walked (see the loop in
     * savesettings()). A trailing nullptr alone is a trap: fill all eight slots with real keys and
     * the terminator quietly disappears, and the loop walks into the next SectionSpec. [Display]
     * already uses six. */
    struct SectionSpec { char const *name; char const *keys[8]; };
    SectionSpec const SECTIONS[] = {
        { "Display", { "bgcolor", "displayccx", "forceshowtimer", "legacyscores",
                       "showbuildtag", "showinitstate", nullptr } },
        { "Game",    { "ignorepasswords", "selectedruleset", "selectedseries", nullptr } },
        { "Sound",   { "volume", nullptr } },
    };
}

/* MOD (Jeremy): the settings file is "tw_settings.ini" NEXT TO THE PROGRAM, not "settings" inside
 * the save directory.
 *
 * Two reasons. It ships: the release zip carries a stock tw_settings.ini, so a downloader can see
 * and edit every setting without first having to run the game and hunt for a file with no
 * extension inside save\. And it matches its sibling project -- SuperCC lives in this same folder
 * and reads succ_settings.ini, so neither program claims a generic name.
 *
 * There is deliberately NO migration from the old location: an existing save\settings is left
 * exactly where it is and simply not read. Anyone upgrading copies their values across once, which
 * the README explains. The alternative -- reading the legacy file "just this once" -- means
 * carrying that path forever.
 *
 * ⚠ NOTE this puts the file next to the executable, so an installation in a directory the user
 * cannot write to (Program Files) cannot save settings AT ALL, and the failure is SILENT in the
 * shipped build: warn() goes to stderr, and the Windows executable is linked for the GUI subsystem
 * with no console attached, so nothing reaches the user. (Do not "fix" that by raising a dialog
 * from savesettings() -- it runs from an atexit handler during teardown.) The release is a
 * portable zip and README.txt says plainly where to put it; that is the mitigation. */
char const * sfname = "tw_settings.ini";

/* MOD (Jeremy): WHICH directory the settings file lives in.
 *
 * Beside the program for the portable Windows release -- that is the whole point, since the file
 * ships in the zip and has to be findable and editable.
 *
 * But NOT for a system-wide install. CMakeLists.txt defines ROOTDIR (to something like
 * <prefix>/share/tworld) for non-Windows release builds, and that directory is root-owned: putting
 * the settings file there would mean loadsettings() finds nothing on every launch and
 * savesettings() warns on every exit, so nothing would ever persist. Those builds keep using the
 * save directory, which is per-user and writable, exactly as they did before this change.
 *
 * Compile-time rather than a runtime writability probe on purpose: a probe would silently pick a
 * different file depending on where the game happened to be installed, which is a worse thing to
 * debug than a rule you can read here. */
static char const *settingsdir(void)
{
#if defined(ROOTDIR) && !defined(_WIN32)
    return savedir;
#else
    return appdir;
#endif
}

void loadsettings()
{
    char *fname = getpathforfileindir(settingsdir(), sfname);
    /* getpathforfileindir() returns NULL when the path would be too long; handing that to
     * ifstream is undefined behavior rather than an error. */
    if (!fname)
    {
        warn("Settings path is too long; settings will not be loaded or saved");
        settingsUnreadable = true;
        return;
    }
    ifstream in(fname);

    if (!in)
    {
        /* Absent is fine -- that is a first run, and the defaults are correct. Present but
         * unopenable is NOT fine: see settingsUnreadable above. */
        struct stat st;
        settingsUnreadable = (stat(fname, &st) == 0);
        if (settingsUnreadable)
            warn("Could not read the settings file; settings will not be saved this session");
        free(fname);
        return;
    }
    free(fname);
    settingsUnreadable = false;

    map<string, string> newsettings;
    string line;
    bool firstline = true;
    while (getline(in, line))
    {
        /* MOD (Jeremy): strip a UTF-8 BOM from the first line. Without this a BOM'd file turns its
         * first key into "<BOM>bgcolor", which is silently ignored and then preserved forever as a
         * phantom entry under [Other]. PowerShell 5.1's `>` and `Out-File -Encoding utf8` both
         * write a BOM, so a scripted edit of this file really does produce one. */
        if (firstline)
        {
            firstline = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF
                                 && (unsigned char)line[1] == 0xBB
                                 && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
        }
        /* MOD (Jeremy): tolerate the shapes a hand-edited ini file grows. A stray '\r' from a file
         * saved on Windows and read on another platform would otherwise become part of the value
         * (and "1\r" does not parse as an int); [Section] headers and ; or # comments are skipped
         * so the file can be laid out and annotated for a human reader. */
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        size_t const first(line.find_first_not_of(" \t"));
        if (first == string::npos)
            continue;                                   // blank
        if (line[first] == ';' || line[first] == '#' || line[first] == '[')
            continue;                                   // comment or section header

        size_t pos(line.find('='));
        /* pos == 0 is checked SEPARATELY, before the arithmetic below: "pos - 1" on 0 wraps to
         * SIZE_MAX, which is string::npos, and find_last_not_of(npos) searches the whole line and
         * happily reports a name. A line reading "=8" would then be stored under the key "=8" and
         * written back forever. */
        if (pos != string::npos && pos > 0)
        {
            /* Trim whitespace around the name so "volume = 8" and "volume=8" are the same key.
             * The VALUE keeps its interior spacing -- selectedseries holds a filename. */
            size_t const nameEnd(line.find_last_not_of(" \t", pos - 1));
            if (nameEnd == string::npos || nameEnd < first)
                continue;                               // "= value" with no name
            string const name(line.substr(first, nameEnd - first + 1));
            size_t valStart(line.find_first_not_of(" \t", pos + 1));
            if (valStart == string::npos)
                valStart = line.size();
            /* Trailing whitespace is trimmed too. It is invisible in an editor, and it is not
             * harmless: selectedseries is used as a filename verbatim, so "MO3.dat-ms.dac " simply
             * fails to reopen the set with no explanation. */
            size_t valEnd(line.find_last_not_of(" \t"));
            newsettings[name] = (valEnd == string::npos || valEnd < valStart)
                              ? string() : line.substr(valStart, valEnd - valStart + 1);
        }
    }

    if (!in.eof())
        warn("Error reading settings file");

    settings = move(newsettings);
}

void savesettings()
{
    /* Never write over a file we could not read -- see settingsUnreadable. */
    if (settingsUnreadable)
        return;

    char *fname = getpathforfileindir(settingsdir(), sfname);
    if (!fname)
        return;
    ofstream out(fname);
    free(fname);

    if (!out)
    {
        warn("Could not open settings file");
        return;
    }

    /* MOD (Jeremy): write the known keys grouped under their headings, then everything else. The
     * "everything else" pass is the important half -- it is what stops this fork from silently
     * eating a setting it does not recognize. */
    map<string, bool> written;
    bool first = true;
    for (SectionSpec const &section : SECTIONS)
    {
        bool any = false;
        size_t const maxkeys = sizeof(section.keys) / sizeof(section.keys[0]);
        for (size_t k = 0; k < maxkeys && section.keys[k]; ++k)
        {
            map<string, string>::const_iterator i(settings.find(section.keys[k]));
            if (i == settings.end())
                continue;
            if (!any)
            {
                if (!first)
                    out << '\n';
                out << '[' << section.name << "]\n";
                any = true;
                first = false;
            }
            out << i->first << '=' << i->second << '\n';
            written[i->first] = true;
        }
    }

    bool other = false;
    for (map<string,string>::const_iterator i(settings.begin());
         i != settings.end(); ++i)
    {
        if (written.find(i->first) != written.end())
            continue;
        if (!other)
        {
            if (!first)
                out << '\n';
            out << "[Other]\n";
            other = true;
            first = false;
        }
        out << i->first << '=' << i->second << '\n';
    }

    if (!out)
    {
        warn("Could not write settings");
    }
}

int getintsetting(char const * name)
{
    std::map<string, string>::const_iterator loc(settings.find(name));
    if (loc == settings.end())
        return -1;
    std::istringstream in(loc->second);
    int i;
    if (!(in >> i))
        return -1;
    return i;  
}

void setintsetting(char const * name, int val)
{
    std::ostringstream out;
    out << val;
    settings[name] = out.str();
}

char const * getstringsetting(char const * name)
{
    std::map<string, string>::const_iterator loc(settings.find(name));
    if (loc == settings.end())
	return nullptr;
    return loc->second.c_str();
}

void setstringsetting(char const * name, char const * val)
{
    settings[name] = val;
}
