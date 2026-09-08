/* settings.cpp: Functions for managing settings.
 *
 * Copyright (C) 2014-2017 by Eric Schmidt, under the GNU General Public
 * License. No warranty. See COPYING for details.
 */

#include "settings.h"

#include "err.h"
#include "fileio.h"

#include <sys/stat.h>   /* MOD (Jeremy): tell "absent" apart from "exists but will not open" */

#include <cctype>       /* MOD (Jeremy, jc-37): tolower(), for settingoptedin() */
#include <cerrno>       /* MOD (Jeremy, jc-54): the rename() failure code, on non-Windows */
#include <cstdio>       /* MOD (Jeremy, jc-54): remove(), rename() -- the atomic write */
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

/* MOD (Jeremy, jc-54): MoveFileExA, for the atomic replace in savesettings().
 *
 * Included LAST and only here. WIN32_LEAN_AND_MEAN keeps it to the kernel
 * surface; NOMINMAX is deliberately NOT defined because MinGW's
 * bits/os_defines.h already defines it and redefining it warns. On every other
 * platform rename() is atomic on its own and none of this is compiled. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

extern char *savedir;
/* ⚠ NOT the program's own directory, whatever the name suggests. tworld.c:2146 sets appdir = root,
 * and root is $TWORLDDIR, or ROOTDIR on a system build, or "." -- the WORKING directory. Nothing in
 * this tree resolves the executable's own path: there is no GetModuleFileName, no
 * QCoreApplication::applicationDirPath and no chdir. See sfname below. */
extern char *appdir;

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
     * savesettings()). A trailing nullptr alone is a trap: fill every slot with real keys and the
     * terminator quietly disappears, and the loop walks into the next SectionSpec.
     *
     * MOD (Jeremy, jc-37): the bound is now the named constant SECTION_MAXKEYS rather than a bare
     * 8, because the prose above and the literal below had already drifted apart once. [Display]
     * uses EIGHT of the twelve slots as of jc-37 (the death counter added two). Raising this
     * constant is the ONLY edit needed to make room -- savesettings() derives its loop bound from
     * sizeof(), and the "unknown keys survive a round trip" guarantee comes from the [Other] pass,
     * not from this table, so growing it cannot lose a setting.
     *
     * MOD (Jeremy, jc-56): raised 12 -> 16, because the title switches took [Display] to twelve
     * keys and the terminator would have had nowhere to go. The jc-41 comment below had called
     * this exact shot ("the NEXT [Display] setting must raise SECTION_MAXKEYS") and it was right;
     * the margin is now four slots rather than one, so the next setting is not a landmine. */
    int const SECTION_MAXKEYS = 16;
    struct SectionSpec { char const *name; char const *keys[SECTION_MAXKEYS]; };
    SectionSpec const SECTIONS[] = {
        /* MOD (Jeremy, jc-41): lynxtileset/mstileset name the user's chosen tileset per ruleset.
         * MOD (Jeremy, jc-56): showlevelname/showlevelpack choose what the title bar says.
         * ⚠ This row now holds TWELVE keys plus the terminator = 13 of SECTION_MAXKEYS (16). */
        { "Display", { "bgcolor", "deathcount", "displayccx", "forceshowtimer", "legacyscores",
                       "lynxtileset", "mstileset",
                       "showbuildtag", "showdeathcounter", "showinitstate",
                       "showlevelname", "showlevelpack", nullptr } },
        { "Game",    { "ignorepasswords", "selectedruleset", "selectedseries", nullptr } },
        { "Sound",   { "volume", nullptr } },
    };
}

/* MOD (Jeremy): the settings file is "tw_settings.ini" in the WORKING DIRECTORY, not "settings"
 * inside the save directory.
 *
 * 🔴 "NEXT TO THE PROGRAM" IS WRONG AND THIS COMMENT USED TO SAY IT. Corrected after an independent
 * review found the claim still here, in README.txt twice, and on the extern above -- after ADR 0007
 * had already recorded that it was false and believed it fixed. It is true only in the sense that
 * double-clicking the executable makes its folder the working directory; launch the game from
 * anywhere else and it reads and writes a DIFFERENT settings file. See settingsdir() below and the
 * note on appdir at the top of this file.
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
 * ⚠ NOTE the file therefore lands wherever the game was LAUNCHED from, so a working directory the
 * user cannot write to cannot save settings AT ALL, and the failure is SILENT in the shipped build:
 * warn() goes to stderr, and the Windows executable is linked for the GUI subsystem with no console
 * attached, so nothing reaches the user. (Do not "fix" that by raising a dialog from savesettings()
 * -- it runs from an atexit handler during teardown.) The release is a portable zip and README.txt
 * says plainly where to put it; that is the mitigation.
 *
 * ⚠ The old wording said "an installation in Program Files cannot save settings", which is a FALSE
 * CONSEQUENCE of the false premise above: the install location is not what decides this. Shortcuts
 * commonly set a working directory of their own, and a shortcut pointing into Program Files with a
 * writable working directory saves fine. */
char const * sfname = "tw_settings.ini";

/* MOD (Jeremy): WHICH directory the settings file lives in.
 *
 * The working directory for the portable Windows release -- which for a double-clicked executable
 * is its own folder, and that is the point: the file ships in the zip and has to be findable and
 * editable next to the game a player just extracted.
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

/* MOD (Jeremy, jc-54): read key=value lines out of any stream.
 *
 * Split out of loadsettings() so the fuzz target can drive the real parser against an in-memory
 * stream (test/fuzz/fuzz_settings.cpp) rather than a file per execution. Nothing about the rules
 * changed when it moved; the comments below are the originals. */
static void parsesettings(std::istream &in, map<string, string> &newsettings)
{
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
         * so the file can be laid out and annotated for a human reader.
         *
         * ⚠ jc-54: THIS LINE IS NOW REDUNDANT, and is kept only as belt-and-braces. Adding '\r' to
         * the four trim sets below subsumes it completely -- a carriage return leading, before the
         * '=', after the '=', or at the end of the line is now handled there. Measured rather than
         * assumed: with this strip deleted the suite still passes, and an exhaustive differential
         * over 177,156 strings up to length five (drawn from k, v, =, CR, LF, space, tab, ;, #, [
         * and 0xEF) found ZERO inputs that parse differently with and without it. It is left in
         * because removing shipped parsing behavior buys nothing here -- but do not read it as
         * doing necessary work, and do not "restore" it if a future edit drops it. */
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        /* MOD (Jeremy, jc-54): '\r' belongs in the whitespace set, and leaving it out was a defect
         * that the new fuzz target found on its first real run.
         *
         * The strip above removes a carriage return only at the very END of the line, which covers
         * the ordinary CRLF file. But in a line like "key=value\r   " the CR is INTERIOR -- the line
         * ends in spaces -- so the strip does not fire, the " \t" trim then removes the spaces, and
         * the CR is left as the last byte of the VALUE. The writer emits that value followed by its
         * own newline, so the next load sees "key=value\r" with the CR at the end, strips it, and
         * gets a different value. One round trip through the program silently changed a setting.
         *
         * Reproducer, four bytes: EF 3D 0D 20. See test/fuzz/corpus/settings/cr-before-space and
         * the case in test/settings_test.c.
         *
         * A carriage return IS whitespace, and the comment on the value trim below already says the
         * point is invisible trailing junk -- "MO3.dat-ms.dac " simply fails to reopen the set with
         * no explanation, and "MO3.dat-ms.dac\r" fails exactly as silently. */
        size_t const first(line.find_first_not_of(" \t\r"));
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
            size_t const nameEnd(line.find_last_not_of(" \t\r", pos - 1));
            if (nameEnd == string::npos || nameEnd < first)
                continue;                               // "= value" with no name
            string const name(line.substr(first, nameEnd - first + 1));
            size_t valStart(line.find_first_not_of(" \t\r", pos + 1));
            if (valStart == string::npos)
                valStart = line.size();
            /* Trailing whitespace is trimmed too. It is invisible in an editor, and it is not
             * harmless: "MO3.dat-ms.dac " simply fails to reopen the set with no explanation.
             *
             * ⚠ Corrected jc-54: this comment used to say selectedseries "is used as a filename
             * verbatim". It is not, and the distinction matters to anyone assessing what a hostile
             * settings file can do. Its only use is the strcmp against the ENUMERATED set list at
             * tworld.c:1963 -- a match key, never a path handed to an open. A trailing space makes
             * that comparison fail, which is the symptom; nothing tries to open the padded name. */
            size_t valEnd(line.find_last_not_of(" \t\r"));
            newsettings[name] = (valEnd == string::npos || valEnd < valStart)
                              ? string() : line.substr(valStart, valEnd - valStart + 1);
        }
    }
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
    parsesettings(in, newsettings);

    /* MOD (Jeremy, jc-54): a read that stopped early now LATCHES, it does not merely complain.
     *
     * getline() ends the loop either at end of file -- which sets eofbit, including for a last line
     * with no newline -- or on a read error. So reaching here without eofbit means the file has
     * more in it than was read, and the map is a PARTIAL picture. savesettings() rewrites the whole
     * file from that map, so the old behavior was to warn and then commit the truncation at the
     * next save, permanently. That is the same class of loss settingsUnreadable was added to
     * prevent; it just covered "would not open" and not "opened and gave me less than is there".
     *
     * ⚠ This does not cover a file containing byte 0x1A. The stream is opened in text mode, where
     * Ctrl-Z ends the read CLEANLY -- eofbit set, no error -- so a settings file with one loses
     * every key after it and nothing here can tell. Measured. Closing that needs a binary read with
     * its own line splitting, which is a larger change than this release. */
    if (!in.eof())
    {
        warn("The settings file could not be read to the end;"
             " settings will not be saved this session");
        settingsUnreadable = true;
    }

    /* MOD (Jeremy, jc-54): a settings file that opens and yields NO KEYS is deliberately NOT
     * reported, and deliberately not treated as unreadable.
     *
     * It is the one damaged state this module cannot recognize -- a zero-length file opens, parses,
     * and yields nothing, exactly as a file of only comments does -- so the temptation is to warn
     * about it. That was written, and then removed, because it is indistinguishable from a
     * perfectly ordinary state: savesettings() itself writes a zero-byte file whenever the map is
     * empty, which is what a batch verification run (`tworld2 -v`) leaves behind every time. The
     * warning therefore fired on a NORMAL run and broke test/run-e2e.ps1's "batch verify writes no
     * unexpected diagnostics" case the first time it was exercised. A warning that fires when
     * nothing is wrong teaches people to ignore warnings.
     *
     * Nor may it refuse to write: a program that had just written a zero-byte file would then never
     * be able to save again. The real answer is upstream of here -- the atomic write in
     * savesettings() is what stops this program CREATING a truncated file in the first place. */
    settings = move(newsettings);
}

/* MOD (Jeremy, jc-54): render the whole file into a stream, composing nothing on disk.
 *
 * Split out of savesettings() so that the bytes exist in full BEFORE anything touches the
 * destination -- which is the entire point of the atomic write below -- and so that the fuzz target
 * can drive the writer against an in-memory stream (test/fuzz/fuzz_settings.cpp).
 *
 * 🔴 THE LAYOUT IS THE FILE FORMAT. Every line here is byte-compatible with what this program has
 * always written, and it is what re-reads correctly: the reader strips a UTF-8 BOM from line 1 only,
 * so a key line must never BE line 1. It cannot be, because a [Section] or [Other] header is always
 * emitted before any key -- do not "tidy" that away.
 *
 * Write the known keys grouped under their headings, then everything else. The "everything else"
 * pass is the important half -- it is what stops this fork from silently eating a setting it does
 * not recognize. */
static void rendersettings(std::ostream &out)
{
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
}

namespace
{
    /* MOD (Jeremy, jc-54): the staging file, removed on every path out of savesettings() that does
     * not hand it over to the replace. This is the language's `finally`: the alternative is
     * remove(tmp) repeated on four exit paths, which is how one of them gets missed. */
    struct TempFile
    {
        string name;
        bool   created;     /* the file was actually opened, so there is something to remove */
        bool   keep;        /* the replace consumed it, so there is nothing left to remove */

        explicit TempFile(string const &n) : name(n), created(false), keep(false) { }
        ~TempFile()
        {
            /* ⚠ The removal can FAIL, and this is the one path that could leak silently. Anything
             * holding the staging file open without FILE_SHARE_DELETE -- a scanner or a sync client
             * opening a newly created file, which is exactly what they do -- makes remove() return
             * -1 (measured: errno 13) and the temp survives. Say so rather than leaving a file in
             * the user's install directory with nothing recorded.
             *
             * `created` is what keeps that honest. Without it the destructor reports a failure to
             * remove a file that was never made, which is exactly what happens on the fallback path
             * below -- the staging open failed, so there is nothing to clean up and nothing to
             * report. Caught by running the fallback for the first time: it warned twice. */
            if (created && !keep && std::remove(name.c_str()) != 0)
                warn("Could not remove the settings staging file %s", name.c_str());
        }
    };
}

/* MOD (Jeremy, jc-54): put `src` where `dest` is, atomically, without ever leaving `dest` absent or
 * half-written.
 *
 * WINDOWS. rename() over an existing file simply FAILS here (measured: -1 from MSVCRT), so
 * MoveFileExA with MOVEFILE_REPLACE_EXISTING is not a refinement, it is the only working call.
 *
 * 🔴 THE RETRY IS THE LOAD-BEARING PART, NOT THE ATOMICITY. Measured against a synthetic scanner
 * holding the destination open with FILE_SHARE_READ|FILE_SHARE_WRITE: one attempt lost 19% of
 * writes; four attempts lost none. Without it this change would trade a rare torn file for a
 * frequent silently-dropped write, which is exactly the regression the sibling project measured at
 * 14-83% of writes lost. The delays are deliberately NOT multiples of each other -- a fixed-period
 * backoff phase-locks with a periodic locker, and a 5/15/40 pattern measured WORSE than no retry at
 * all because Windows rounds both to the same ~15.6 ms tick. Do not "regularize" them, and do not
 * call timeBeginPeriod to make them precise: that raises the system-wide timer rate.
 *
 * Everything else. rename() is atomic on POSIX and cannot fail for a lock, so there is nothing to
 * retry and no reason to compile any of it.
 *
 * ⚠ WHAT ACTUALLY BLOCKS THE REPLACE, measured across every share mode: ANY open handle on the
 * destination does, whatever sharing it was opened with -- even a metadata-only handle opened with
 * FILE_SHARE_READ|WRITE|DELETE. MoveFileEx failed 0 for 6. The share mode is irrelevant because
 * MoveFileEx opens the target restrictively for its own delete. So the window is not "a scanner
 * holding a write lock", it is "anybody has it open at all" -- including the sync client hashing
 * the file right after our PREVIOUS save, which makes consecutive saves a correlated race rather
 * than independent ones. That is the number to watch if this ever misbehaves in the field.
 *
 * NOT USED, and each for a measured reason:
 *   MOVEFILE_WRITE_THROUGH -- costs 34% per write to narrow a power-loss window.
 *   ReplaceFileA -- it is genuinely BETTER at the lock: it succeeds in 3 of those 6 cases, namely
 *     the ones where the other handle shares DELETE, which is what a well-behaved sync client uses.
 *     It is rejected anyway, and not because the retry covers it: it has a documented partial
 *     failure (ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) in which the DESTINATION no longer exists. This
 *     function's one promise is that a failure leaves the original untouched, and ReplaceFile
 *     cannot make that promise. It also cannot create an absent destination, so a first run would
 *     need MoveFileEx anyway. Losing three lock cases is the price of never destroying the file. */
/* MOD (Jeremy, jc-54): the staging file's name, "<destination>.tmp-<pid>-<seq>".
 *
 * The shape matches the sibling project's exactly (SuccPaths.java): the two programs live in one
 * folder, and somebody reading a directory listing should see one pattern rather than two.
 *
 * 🔴 IT IS A SIBLING OF THE TARGET, NOT SOMETHING IN A TEMP DIRECTORY, and that is not tidiness.
 * Same directory means same volume, which is what keeps the replace on its atomic path; a staging
 * file in %TEMP% degrades to copy-then-delete, which truncates the destination and reintroduces
 * exactly the torn file this design removes. Never "tidy" it elsewhere. */
static string stagingname(string const &dest, unsigned seq)
{
    ostringstream s;

    s << dest << ".tmp-";
#ifdef _WIN32
    s << (unsigned long)GetCurrentProcessId();
#else
    s << (unsigned long)getpid();
#endif
    s << '-' << seq;
    return s.str();
}

/* MOD (Jeremy, jc-56): how many attempts the last replacefile() made.
 *
 * 🔴 A TEST SEAM, AND IT REPLACED A TEST THAT FLAKED ON CI. settings_test.c has to witness that the
 * retry loop actually ran -- cutting it to one attempt otherwise leaves the whole suite green,
 * because every successful write in every other case succeeds on the first try. jc-54 witnessed it
 * with a wall-clock floor and a comment claiming "Sleep can only ever overshoot, so this cannot
 * flake in the fast direction." That was wrong twice over, and CI proved it by failing the RELEASE
 * job on a commit whose CI job had just passed:
 *
 *   - GetTickCount64 advances in ~15.6 ms steps, so the MEASUREMENT can undershoot the true
 *     elapsed time by nearly a tick however faithfully Sleep overshoots; and
 *   - how long Sleep(2) actually takes depends on the system timer resolution, which any OTHER
 *     process can change globally with timeBeginPeriod. The 71-80 ms measured on the maintainer's
 *     desktop was a coarse-timer machine; the CI runner sleeps the requested ~19 ms and reported
 *     16 ms after granularity.
 *
 * So the oracle is the count, which is exact, free, and says the thing actually meant. Time was
 * never the property under test -- the number of attempts was. Reset at entry, so a reader is
 * always the last call's.
 *
 * ⚠ NOT WRAPPED IN #ifdef. POSIX rename() cannot fail for a lock, so the count is 1 there and no
 * test asserts on it -- but a counter that exists on only one platform is a compile error waiting
 * for whoever writes the portable case. */
unsigned replaceattempts = 0;

static bool replacefile(char const *src, char const *dest, unsigned long *err)
{
    *err = 0;
    replaceattempts = 0;
#ifdef _WIN32
    /* 🔴 EXACTLY THE POLICY THAT WAS MEASURED: four attempts, sleeping 0/2/5/12 ms. That lost 0 of
     * 120 writes where a single attempt lost 19%. An earlier draft added a fifth 25 ms step, which
     * nothing measured and which doubles the worst-case stall -- and this runs on the one thread
     * the GUI uses, from a handler that fires on every death when the death counter is on. Do not
     * add steps that no measurement asked for. */
    static unsigned const backoffms[] = { 0, 2, 5, 12 };

    for (size_t i = 0; i < sizeof backoffms / sizeof backoffms[0]; ++i)
    {
        if (backoffms[i])
            Sleep(backoffms[i]);
        ++replaceattempts;
        if (MoveFileExA(src, dest, MOVEFILE_REPLACE_EXISTING))
            return true;
        /* The FIRST failure, not the last. If attempt 1 is ACCESS_DENIED and a later one is
         * FILE_NOT_FOUND because a scanner quarantined the staging file, the useful diagnostic is
         * the one that started it. */
        if (!*err)
            *err = GetLastError();
    }
    return false;
#else
    ++replaceattempts;
    if (rename(src, dest) == 0)
        return true;
    *err = (unsigned long)errno;
    return false;
#endif
}

/* MOD (Jeremy, jc-54): write the settings file by staging it and replacing it, never by truncating
 * it in place.
 *
 * WHAT WAS WRONG. This function used to open the live tw_settings.ini with a truncating ofstream and
 * refill it. Between those two moments the user's settings did not exist, and that window is entered
 * many times per session -- this is called from play.c, TWTheme.cpp, three places in TWMainWnd.cpp
 * and shutdownsystem(), because a setting is written the instant it changes rather than at exit.
 * Killing a process inside that window turns a 289-byte settings file into a 0-byte one; measured,
 * not supposed. The old error check made it worse by running BEFORE the stream's destructor flushed,
 * so a write that failed late reported nothing at all.
 *
 * ⚠ AND THE settingsUnreadable LATCH DOES NOT COVER THAT. A truncated file still OPENS, so
 * loadsettings() reads it as "no settings", and the next save writes defaults over the wreckage.
 * The latch tells absent from unreadable; it never saw this case.
 *
 * WHAT THIS DOES NOT FIX, so that nobody claims it later: two instances of the game each hold the
 * whole map and each rewrite the whole file, so the second one to exit still wins. Atomicity makes
 * that a clean overwrite instead of a torn one. It is not multi-instance safety.
 *
 * ⚠ A FAILURE HERE IS SILENT IN THE SHIPPED BUILD. warn() goes to stderr and the Windows executable
 * is linked for the GUI subsystem with no console (see sfname's note above), so nothing reaches the
 * user. That is acceptable ONLY because of what the failure now is: the file keeps its previous,
 * complete contents and one change did not stick. It was not acceptable before, when the same
 * silence covered a destroyed file. The OS error code is included for the console and Linux builds.
 */
void savesettings()
{
    /* Never write over a file we could not read -- see settingsUnreadable. */
    if (settingsUnreadable)
        return;

    char *fname = getpathforfileindir(settingsdir(), sfname);
    if (!fname)
    {
        /* loadsettings() warns for this identical condition, so this one did too or the two
         * disagreed about whether it was worth mentioning. Unreachable in practice: the same call
         * fails the same way at startup and latches settingsUnreadable, which returns above. */
        warn("Settings path is too long; settings were not saved");
        return;
    }
    string const dest(fname);
    free(fname);

    /* 🔴 The temp path is built in its OWN string. getpathbuffer() allocates getpathbufferlen() + 1
     * bytes (fileio.c:364) and getpathforfileindir() may return a path that fills it -- the one
     * spare byte is the NUL, nowhere near room for ".tmp-<pid>-<seq>" -- so appending a suffix to
     * that buffer would overflow the heap. */
    /* The sequence number makes two writes from ONE PROCESS distinct. It does not help against a
     * recycled PID -- it restarts at zero every run, so a new process's first save regenerates
     * exactly the name a crashed predecessor left behind. That is harmless and is handled below by
     * opening the staging file with truncation: a leftover temp is by definition garbage, because
     * the only way one exists is that a process died before its replace.
     *
     * POD on purpose: this function runs from an atexit handler, and a namespace-scope object with
     * a destructor would be a teardown-order bug waiting for a rainy day. */
    static unsigned seq = 0;
    string const tmpname = stagingname(dest, ++seq);

    /* ⚠ The staging path is ~13 characters longer than the destination, so there is a band of
     * install depths -- roughly 247 to 259 characters here -- where the destination fits and the
     * staging path does not. ANSI MAX_PATH allows 259 usable characters and getpathbufferlen() is
     * 260, so the length must be STRICTLY LESS than that to be usable; `<= 260` would admit a path
     * the API then rejects. Staging is IMPOSSIBLE at that length, which is one of the two cases
     * handled by the fallback below. */
    bool const pathfits = tmpname.size() < (size_t)getpathbufferlen();

    if (pathfits)
    {
        TempFile tmp(tmpname);

        /* 🔴 TEXT MODE, DELIBERATELY. The shipped file is CRLF, and it is CRLF because this stream
         * translates '\n' on Windows. Opening the staging file std::ios::binary -- the instinctive
         * move when the point is to write exact bytes -- would silently convert every existing
         * user's settings file to LF. The translation IS the format. */
        /* 🔴 A FAILED CREATE IS RETRIED UNDER A DIFFERENT NAME BEFORE IT COUNTS AS IMPOSSIBLE.
         *
         * The fallback below is only allowed to run when staging cannot work in this directory AT
         * ALL. A single failed create does not prove that: `seq` restarts at zero in every process,
         * so a crashed predecessor's leftover temp has exactly the name this process tries first,
         * and if a scanner or sync client is holding that leftover open the create fails for a
         * purely transient reason. Falling back on that would put the truncating write into the one
         * situation the design says must fail closed -- something has a handle open in this
         * directory right now, which is when a torn file is most likely.
         *
         * A SECOND, DIFFERENT name failing is real evidence about the directory. */
        ofstream out(tmp.name.c_str());
        if (!out)
        {
            string const retry = stagingname(dest, ++seq);
            if (retry.size() < (size_t)getpathbufferlen())
            {
                tmp.name = retry;
                out.clear();
                out.open(tmp.name.c_str());
            }
        }
        if (out)
        {
            tmp.created = true;
            rendersettings(out);
            out.close();

            /* Checked AFTER close(), which is the only point at which the buffer has been flushed
             * and a late failure -- a full disk, a disconnected volume -- has actually been
             * reported.
             *
             * 🔴 THIS FAILURE DOES **NOT** FALL BACK, and the distinction is the whole design. The
             * bytes could not be written to a NEW file, so writing them over the REAL one would
             * fail in the same way and destroy it on the way. Refusing here is what keeps the
             * user's settings. */
            if (!out)
            {
                warn("Could not write the settings staging file; settings were not saved");
                return;
            }

            unsigned long err = 0;
            if (!replacefile(tmp.name.c_str(), dest.c_str(), &err))
            {
                /* ⚠ NOR DOES THIS ONE. The destination is held open by something -- measured: ANY
                 * open handle blocks the replace, whatever its share mode. That is transient, the
                 * retry above has already spent its budget on it, and the in-place write that
                 * would "succeed" here is exactly the truncating write this release removed. The
                 * old code did land this write; we deliberately drop it instead, because the file
                 * keeps its previous complete contents and the next save re-persists the change
                 * from the in-memory map. */
                warn("Could not replace the settings file (error %lu);"
                     " it keeps its previous contents and this change was not saved", err);
                return;
            }
            tmp.keep = true;    /* the replace consumed it; nothing is left to remove */
            return;
        }
        warn("Could not create the settings staging file; falling back to a direct write");
    }

    /* 🔴 THE FALLBACK, AND WHY IT EXISTS AT ALL.
     *
     * Staging needs permissions the old writer never did: creating a file requires FILE_ADD_FILE on
     * the DIRECTORY, and the replace additionally requires FILE_DELETE_CHILD, where writing the
     * settings file in place needed only write access to the FILE. Measured: in a directory that
     * denies file creation but leaves tw_settings.ini fully writable, the staged write loses 100%
     * of saves, permanently and silently, and the old in-place write landed every one. The same
     * shape applies when the staging path is too long for the platform. On POSIX it is commoner
     * still -- rename() needs write permission on the directory too.
     *
     * That is a configuration where this release would be STRICTLY WORSE than what it replaced, and
     * "never worse than before" is the whole point of the change. So when staging is STRUCTURALLY
     * impossible -- not merely blocked -- the write falls back to what the program did for its
     * first fifty-two builds.
     *
     * ⚠ THE LINE BETWEEN "IMPOSSIBLE" AND "BLOCKED" IS LOAD-BEARING, so do not widen this. Blocked
     * means the atomic path works here in general and something is in the way right now: a locked
     * destination, a write that failed midway. Those fail closed above, because falling back would
     * reintroduce the torn file in precisely the situations where a torn file is most likely. Only
     * "there is no way to stage a file in this directory at all" reaches here, and in that case the
     * alternative to a truncating write is never saving anything, ever.
     *
     * This path carries the old defect: a process killed between the open and the flush leaves the
     * file truncated. That is accepted, because the configurations that reach it had no atomic
     * option to lose. */
    {
        ofstream out(dest.c_str());
        if (!out)
        {
            warn("Could not open the settings file; settings were not saved");
            return;
        }
        rendersettings(out);
        out.close();
        if (!out)
            warn("Could not write the settings file; it may be incomplete");
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

/* MOD (Jeremy, jc-37): ONE definition of "this opt-in switch is on", callable from portable C.
 *
 * This used to live only in TileWorldApp::SettingOptedIn(), whose comment promised it was the
 * single shared definition -- true only for as long as nothing outside Qt needed to ask. The death
 * counter is counted in tworld.c, which cannot see Qt at all, so the parse moved DOWN here and
 * TWApp.cpp now delegates. Writing a second parser up there instead would have made that promise
 * a lie the first time the two disagreed about, say, " TRUE ".
 *
 * Strictly opt-in: only "1" or "true" (any casing, surrounding whitespace ignored). Absent, blank,
 * "0", garbage and a missing settings file all mean OFF, so a setting can never switch itself on
 * by accident. Mirrors SuperCC's optedIn().
 *
 * A STRING read, not getintsetting(), on purpose: the file is meant to be hand-edited and
 * "showdeathcounter=true" is what someone reading the README will naturally type.
 * getintsetting() cannot parse that and would silently report -1. */

/* MOD (Jeremy, jc-56): the trimmed, lowercased value of a switch, or "" when the key is absent or
 * holds nothing but whitespace. Factored out when settingoptedout() arrived, so that the two
 * predicates cannot drift apart on what counts as " TRUE " -- the same reasoning that moved the
 * parse out of TileWorldApp::SettingOptedIn() in the first place.
 *
 * ⚠ ABSENT AND BLANK DELIBERATELY LOOK THE SAME, and both mean "no opinion, use the default".
 * That is why this returns a word rather than a tri-state: neither predicate has any use for the
 * distinction, and inventing one would make "showlevelname=" mean something different from
 * omitting the line, which nobody hand-editing an ini file would expect. */
namespace
{
    string switchword(char const * name)
    {
        map<string, string>::const_iterator loc(settings.find(name));
        if (loc == settings.end())
            return string();

        /* The whitespace set is QChar::isSpace()'s, matching the QString::trimmed() this replaced
         * -- vertical tab and form feed included, not because an ini file will ever contain them
         * but so that "equivalent to the old Qt implementation" is true without an asterisk. */
        static char const WS[] = " \t\n\v\f\r";
        string s(loc->second);
        string::size_type const first = s.find_first_not_of(WS);
        if (first == string::npos)
            return string();
        string::size_type const last = s.find_last_not_of(WS);
        s = s.substr(first, last - first + 1);

        for (string::size_type i = 0; i < s.size(); ++i)
            s[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
        return s;
    }
}

int settingoptedin(char const * name)
{
    string const s(switchword(name));
    return (s == "1" || s == "true") ? 1 : 0;
}

/* MOD (Jeremy, jc-56): the MIRROR of settingoptedin(), for a switch whose default is ON.
 * TRUE only when the value is explicitly "0" or "false"; absent, blank, "1", "true" and garbage
 * all mean "not opted out", i.e. the feature stays on.
 *
 * 🔴 WHY A SECOND FUNCTION RATHER THAN !settingoptedin(). Because they are not complements.
 * settingoptedin() answers FALSE for an absent key, so !settingoptedin() would answer TRUE for
 * one -- which is right here by luck, and wrong the moment the value is garbage: "showlevelname=yes"
 * would silently turn the level name OFF. A default-on switch must treat an unparseable value as
 * "leave it alone", and only an explicit off-word may turn it off. SuperCC learned this the same
 * way and its rule is written down: MATCH THE PREDICATE TO THE DEFAULT, and never share one
 * between two switches whose defaults differ.
 *
 * The polarity is deliberately left visible at the call site -- `if (!settingoptedout(...))` --
 * rather than hidden behind a "shown" wrapper. A reader skimming tworld.c can see which of the
 * two switches is which; two functions that both read `if (setting...(x))` could not be told
 * apart, and telling them apart is the entire hazard. */
int settingoptedout(char const * name)
{
    string const s(switchword(name));
    return (s == "0" || s == "false") ? 1 : 0;
}

/* MOD (Jeremy, jc-37): FALSE when the settings file exists but could not be read, i.e. when the map
 * is empty for a reason other than "there are no settings yet".
 *
 * Callers that merely read a setting do not need this -- a defaulted value is fine. It exists for
 * the death counter, whose whole point is a running total: in that state getintsetting("deathcount")
 * returns "absent", the counter would show a confident "Deaths: 0", count up from there, and then
 * discard it at exit because savesettings() refuses to write. Showing nothing is far better than
 * showing a wrong lifetime total. This is not hypothetical -- see the note on settingsUnreadable
 * above for the online-only-Dropbox case that produces it. */
int settingsarereadable(void)
{
    return settingsUnreadable ? 0 : 1;
}
