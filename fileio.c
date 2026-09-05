/* fileio.c: Simple file/directory access functions with error-handling.
 *
 * Copyright (C) 2001-2017 by Brian Raiter and Eric Schmidt, under the
 * GNU General Public License. No warranty. See COPYING for details.
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<errno.h>
#include	<dirent.h>
#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	"err.h"
#include	"fileio.h"

/* Determine the proper directory delimiter and mkdir() arguments.
 */
#ifdef WIN32
#define	DIRSEP_CHAR	'\\'
#define	createdir(name)	(mkdir(name) == 0)
#else
#define	DIRSEP_CHAR	'/'
#define	createdir(name)	(mkdir(name, 0755) == 0)
#endif

/* Determine a compile-time number to use as the maximum length of a
 * path. Use a value of 1023 if we can't get anything usable from the
 * header files.
 */
#include <limits.h>
#if !defined(PATH_MAX) || PATH_MAX <= 0
#  if defined(MAXPATHLEN) && MAXPATHLEN > 0
#    define PATH_MAX MAXPATHLEN
#  else
#    include <sys/param.h>
#    if !defined(PATH_MAX) || PATH_MAX <= 0
#      if defined(MAXPATHLEN) && MAXPATHLEN > 0
#        define PATH_MAX MAXPATHLEN
#      else
#        define PATH_MAX 1023
#      endif
#    endif
#  endif
#endif

/* The function used to display error messages relating to file I/O.
 */
int fileerr_(char const *cfile, unsigned long lineno,
	     fileinfo *file, char const *msg)
{
    if (msg) {
	err_cfile_ = cfile;
	err_lineno_ = lineno;
	errmsg_(file->name ? file->name : "file error",
		errno ? strerror(errno) : msg);
    }
    return FALSE;
}

/*
 * File-handling functions.
 */

/* Clear the fields of the fileinfo struct.
 */
void clearfileinfo(fileinfo *file)
{
    file->name = NULL;
    file->fp = NULL;
    file->alloc = FALSE;
}

/* The 'x' modifier (C11) in fopen() is not widely supported as of 2020.
 * This hack enables its use regardless of the underlying libc.
 */
static FILE *FOPEN(char const *name, char const *mode)
{
    FILE * file = NULL;
    if (!strcmp(mode, "wx")) {
	int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd != -1)
	    file = fdopen(fd, "w");
    }
    else
	file = fopen(name, mode);

    return file;
}

/* Open a file. If the fileinfo structure does not already have a
 * filename assigned to it, use name (after making an independent
 * copy).
 */
int fileopen(fileinfo *file, char const *name, char const *mode,
	     char const *msg)
{
    int	n;

    if (!file->name) {
	n = strlen(name) + 1;
	if ((file->name = malloc(n))) {
	    memcpy(file->name, name, n);
	    file->alloc = TRUE;
	} else {
	    file->name = (char*)name;
	    file->alloc = FALSE;
	}
    }
    errno = 0;
    file->fp = FOPEN(name, mode);
    if (file->fp)
	return TRUE;
    return fileerr(file, msg);
}

/* Close the file, clear the file pointer, and free the name buffer if
 * necessary.
 */
void fileclose(fileinfo *file, char const *msg)
{
    errno = 0;
    if (file->fp) {
	if (fclose(file->fp) == EOF)
	    fileerr(file, msg);
	file->fp = NULL;
    }
    if (file->alloc) {
	free(file->name);
	file->name = NULL;
	file->alloc = FALSE;
    }
}

/* rewind().
 */
int filerewind(fileinfo *file, char const *msg)
{
    (void)msg;
    rewind(file->fp);
    return TRUE;
}

/* fseek().
 */
int fileskip(fileinfo *file, int offset, char const *msg)
{
    errno = 0;
    if (!fseek(file->fp, offset, SEEK_CUR))
	return TRUE;
    return fileerr(file, msg);
}

/* feof().
 */
int filetestend(fileinfo *file)
{
    int	ch;

    if (feof(file->fp))
	return TRUE;
    ch = fgetc(file->fp);
    if (ch == EOF)
	return TRUE;
    ungetc(ch, file->fp);
    return FALSE;
}

/* read().
 */
int fileread(fileinfo *file, void *data, unsigned long size, char const *msg)
{
    if (!size)
	return TRUE;
    errno = 0;
    if (fread(data, size, 1, file->fp) == 1)
	return TRUE;
    return fileerr(file, msg);
}

/* Read size bytes from the given file into a newly allocated buffer.
 */
void *filereadbuf(fileinfo *file, unsigned long size, char const *msg)
{
    void       *buf;

    if (size == 0) {
        return NULL;
    }

    if (!(buf = malloc(size))) {
	fileerr(file, msg);
	return NULL;
    }
    errno = 0;
    if (fread(buf, size, 1, file->fp) != 1) {
	fileerr(file, msg);
	free(buf);
	return NULL;
    }
    return buf;
}

/* Read one full line from fp and store the first len characters,
 * including any trailing newline.
 */
int filegetline(fileinfo *file, char *buf, int *len, char const *msg)
{
    int	n, ch;

    if (!*len) {
	*buf = '\0';
	return TRUE;
    }
    errno = 0;
    if (!fgets(buf, *len, file->fp))
	return fileerr(file, msg);
    n = strlen(buf);
    /* MOD (Jeremy, jc-48): look at the LAST CHARACTER STORED, not at the
     * terminator. This read `buf[n] != '\n'` with n == strlen(buf), which
     * indexes the NUL -- never a newline -- so the whole condition collapsed to
     * "the buffer filled". A line that filled it EXACTLY, its newline included,
     * then took the discard-to-end-of-line branch below and swallowed the
     * entire next line.
     *
     * Measured on a .dac: a 254-byte comment line ate the `ruleset = ms` after
     * it, leaving series->ruleset at Ruleset_None, whereupon readseriesheader()
     * falls back to the .dat's own signature. A set could load under the wrong
     * ruleset from a configuration file that looks perfectly correct -- and
     * that silently invalidates every solution recorded against it.
     *
     * Behavior changes ONLY in that exact case; a line genuinely longer than
     * the buffer still discards its remainder, as it must. `n > 0` guards
     * buf[-1] for a *len of 1, which no caller passes today. Upstream's. */
    if (n == *len - 1 && n > 0 && buf[n - 1] != '\n') {
	do
	    ch = fgetc(file->fp);
	while (ch != EOF && ch != '\n');
    } else
	buf[n--] = '\0';
    *len = n;
    return TRUE;
}

/* write().
 */
int filewrite(fileinfo *file, void const *data, unsigned long size,
	      char const *msg)
{
    if (!size)
	return TRUE;
    errno = 0;
    if (fwrite(data, size, 1, file->fp) == 1)
	return TRUE;
    return fileerr(file, msg);
}

/* Read one byte as an unsigned integer value.
 */
int filereadint8(fileinfo *file, unsigned char *val8, char const *msg)
{
    int	byte;

    errno = 0;
    if ((byte = fgetc(file->fp)) == EOF)
	return fileerr(file, msg);
    *val8 = (unsigned char)byte;
    return TRUE;
}

/* Write one byte as an unsigned integer value.
 */
int filewriteint8(fileinfo *file, unsigned char val8, char const *msg)
{
    errno = 0;
    if (fputc(val8, file->fp) != EOF)
	return TRUE;
    return fileerr(file, msg);
}

/* Read two bytes as an unsigned integer value stored in little-endian.
 */
int filereadint16(fileinfo *file, unsigned short *val16, char const *msg)
{
    int	byte;

    errno = 0;
    if ((byte = fgetc(file->fp)) != EOF) {
	*val16 = (unsigned char)byte;
	if ((byte = fgetc(file->fp)) != EOF) {
	    *val16 |= (unsigned char)byte << 8;
	    return TRUE;
	}
    }
    return fileerr(file, msg);
}

/* Write two bytes as an unsigned integer value in little-endian.
 */
int filewriteint16(fileinfo *file, unsigned short val16, char const *msg)
{
    errno = 0;
    if (fputc(val16 & 0xFF, file->fp) != EOF
			&& fputc((val16 >> 8) & 0xFF, file->fp) != EOF)
	return TRUE;
    return fileerr(file, msg);
}

/* Read four bytes as an unsigned integer value stored in little-endian.
 */
int filereadint32(fileinfo *file, unsigned long *val32, char const *msg)
{
    int	byte;

    errno = 0;
    if ((byte = fgetc(file->fp)) != EOF) {
	*val32 = (unsigned int)byte;
	if ((byte = fgetc(file->fp)) != EOF) {
	    *val32 |= (unsigned int)byte << 8;
	    if ((byte = fgetc(file->fp)) != EOF) {
		*val32 |= (unsigned int)byte << 16;
		if ((byte = fgetc(file->fp)) != EOF) {
		    *val32 |= (unsigned int)byte << 24;
		    return TRUE;
		}
	    }
	}
    }
    return fileerr(file, msg);
}

/* Write four bytes as an unsigned integer value in little-endian.
 */
int filewriteint32(fileinfo *file, unsigned long val32, char const *msg)
{
    errno = 0;
    if (fputc(val32 & 0xFF, file->fp) != EOF
			&& fputc((val32 >> 8) & 0xFF, file->fp) != EOF
			&& fputc((val32 >> 16) & 0xFF, file->fp) != EOF
			&& fputc((val32 >> 24) & 0xFF, file->fp) != EOF)
	return TRUE;
    return fileerr(file, msg);
}

/*
 * Directory-handling functions.
 */

/* Return the size of a buffer big enough to hold a pathname.
 */
int getpathbufferlen(void)
{
    return PATH_MAX;
}

/* Return a buffer big enough to hold a pathname.
 */
char *getpathbuffer(void)
{
    char       *buf;

    if (!(buf = calloc(PATH_MAX + 1, 1)))
	memerrexit();
    return buf;
}

/* MOD (Jeremy, jc-48): TRUE if name is a Windows reserved device name.
 *
 * Win32 resolves CON, NUL, COM1, LPT1 and friends to the DEVICE whatever
 * directory prefix they are given, so "<datdir>\LPT1" opens a parallel port
 * rather than a file. NUL and CON merely fail to load and fall back harmlessly,
 * but opening a serial or parallel device can block the GUI thread on a machine
 * that has the driver.
 *
 * MOVED HERE FROM res.c, where it guarded tileset names from jc-42 and nothing
 * else. The same threat applies to any filename this program takes from a text
 * file somebody else wrote -- a .dac's `file=` reaches openfileindir() exactly
 * as a tileset name reaches gettilesetpath() -- so one fork had the guard and
 * one did not. Two copies of a check like this drift; one copy in the file that
 * owns file access does not. It also finally gets a unit test: test/series_test.c
 * compiles fileio.c, and res.c has no test at all.
 *
 * 🔴 THE POLARITY IS THE OPPOSITE OF THE res.c ORIGINAL, deliberately. That one
 * was named isreservedname() and returned FALSE when the name WAS reserved,
 * which reads backwards at every call site. This returns TRUE when the name is
 * reserved; res.c now negates it.
 *
 * The comparison ignores any extension, because "COM1.dat" resolves to the
 * device too.
 */
int isreservedfilename(char const *name)
{
    static char const *const reserved[] = {
	"CON", "PRN", "AUX", "NUL",
	"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
	"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    char	base[16];
    int		n, i;

    for (n = 0 ; name[n] && name[n] != '.' ; ++n) {
	if (n >= (int)(sizeof base - 1))
	    return FALSE;			/* too long to be a device name */
	base[n] = (char)toupper((unsigned char)name[n]);
    }
    base[n] = '\0';
    for (i = 0 ; i < (int)(sizeof reserved / sizeof *reserved) ; ++i)
	if (!strcmp(base, reserved[i]))
	    return TRUE;
    return FALSE;
}

/* Return TRUE if name contains a path but is not a directory itself.
 *
 * ⚠ READ THAT AGAIN BEFORE USING THIS AS A GUARD (MOD (Jeremy, jc-48), comment
 * only). What it actually answers is "is there an EXISTING FILE behind a path",
 * and it looks only for DIRSEP_CHAR -- a BACKSLASH on Windows. So it says FALSE
 * for a path that does not exist yet, and FALSE for `a/b.dat` on Windows even
 * though Windows accepts that perfectly well.
 *
 * readconfigfile() used it to enforce "levelset filename may not contain a
 * path" and was wrong on both counts; it now tests the separators directly.
 * Callers that want "does this contain a path" must not use this function.
 */
int haspathname(char const *name)
{
    struct stat	st;

    if (!strchr(name, DIRSEP_CHAR))
	return FALSE;
    if (stat(name, &st) || S_ISDIR(st.st_mode))
	return FALSE;
    return TRUE;
}

/* Return a pointer to the filename, skipping over any directories in
 * the front.
 */
char *skippathname(char const *name)
{
    char const *p;

    p = strrchr(name, DIRSEP_CHAR);
    return (char*)(p ? p + 1 : name);
}

/* Append the path and/or file contained in path to dir. If path is
 * an absolute path, the contents of dir are ignored.
 */
int combinepath(char *dest, char const *dir, char const *path)
{
    int	m, n;

    if (path[0] == DIRSEP_CHAR) {
	n = strlen(path);
	if (n > PATH_MAX) {
	    errno = ENAMETOOLONG;
	    return FALSE;
	}
	strcpy(dest, path);
	return TRUE;
    }
    n = strlen(dir);
    if (n >= PATH_MAX) {
	errno = ENAMETOOLONG;
	return FALSE;
    }
    if (dest != dir)
	memcpy(dest, dir, n);
    if (dest[n - 1] != DIRSEP_CHAR)
	dest[n++] = DIRSEP_CHAR;
    m = strlen(path);
    if (m + n + 1 > PATH_MAX) {
	errno = ENAMETOOLONG;
	return FALSE;
    }
    memcpy(dest + n, path, m + 1);
    return TRUE;
}

/* Create the directory dir if it doesn't already exist.
 */
int finddir(char const *dir)
{
    struct stat	st;

    return stat(dir, &st) ? createdir(dir) : S_ISDIR(st.st_mode);
}

/* Return the pathname for a directory and/or filename, using the
 * same algorithm to construct the path as openfileindir().
 */
char *getpathforfileindir(char const *dir, char const *filename)
{
    char       *path;
    int		m, n;

    m = strlen(filename);
    if (!dir || !*dir || strchr(filename, DIRSEP_CHAR)) {
	if (m > PATH_MAX) {
	    errno = ENAMETOOLONG;
	    return NULL;
	}
	path = getpathbuffer();
	strcpy(path, filename);
    } else {
	n = strlen(dir);
	if (m + n + 1 > PATH_MAX) {
	    errno = ENAMETOOLONG;
	    return NULL;
	}
	path = getpathbuffer();
	memcpy(path, dir, n);
	path[n++] = DIRSEP_CHAR;
	memcpy(path + n, filename, m + 1);
    }
    return path;
}

/* Open a file, using dir as the directory if filename is not a path.
 */
int openfileindir(fileinfo *file, char const *dir, char const *filename,
		  char const *mode, char const *msg)
{
    char	buf[PATH_MAX + 1];
    int		m, n;

    if (!dir || !*dir || strchr(filename, DIRSEP_CHAR))
	return fileopen(file, filename, mode, msg);

    n = strlen(dir);
    m = strlen(filename);
    if (m + n + 1 > PATH_MAX) {
	errno = ENAMETOOLONG;
	return fileerr(file, NULL);
    }
    memcpy(buf, dir, n);
    buf[n++] = DIRSEP_CHAR;
    memcpy(buf + n, filename, m + 1);
    return fileopen(file, buf, mode, msg);
}

/* Read the given directory and call filecallback once for each file
 * contained in it.
 */
int findfiles(char const *dir, void *data,
	      int (*filecallback)(char const*, void*))
{
    char	       *filename = NULL;
    DIR		       *dp;
    struct dirent      *dent;
    int			r;

    if (!(dp = opendir(dir))) {
	fileinfo tmp;
	tmp.name = (char*)dir;
	return fileerr(&tmp, "couldn't access directory");
    }

    while ((dent = readdir(dp))) {
	if (dent->d_name[0] == '.')
	    continue;
	x_alloc(filename, strlen(dent->d_name) + 1);
	strcpy(filename, dent->d_name);
	r = (*filecallback)(filename, data);
	if (r < 0)
	    break;
	else if (r > 0)
	    filename = NULL;
    }

    if (filename)
	free(filename);
    closedir(dp);
    return TRUE;
}
