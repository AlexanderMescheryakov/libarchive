/* fchdir replacement.
   Copyright (C) 2006-2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include <unistd.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "dosname.h"
#include "filenamecat.h"

#ifndef REPLACE_OPEN_DIRECTORY
# define REPLACE_OPEN_DIRECTORY 0
#endif

/* This replacement assumes that a directory is not renamed while opened
   through a file descriptor.

   FIXME: On mingw, this would be possible to enforce if we were to
   also open a HANDLE to each directory currently visited by a file
   descriptor, since mingw refuses to rename any in-use file system
   object.  */

/* Array of file descriptors opened.  If REPLACE_OPEN_DIRECTORY or if it points
   to a directory, it stores info about this directory.  */
typedef struct
{
  char *name;       /* Absolute name of the directory, or NULL.  */
  /* FIXME - add a DIR* member to make dirfd possible on mingw?  */
} dir_info_t;
static dir_info_t *dirs;
static size_t dirs_allocated;

/* Specification.  */
#include "filenamecat.h"

#include <stdlib.h>
#include <string.h>

#include "dirname.h"

#define mempcpy(D, S, N) ((void *) ((char *) memcpy (D, S, N) + (N)))

#include <stdlib.h>
#include <string.h>

/* Return the address of the last file name component of NAME.  If
   NAME has no relative file name components because it is a file
   system root, return the empty string.  */

char *
last_component (char const *name)
{
  char const *base = name + FILE_SYSTEM_PREFIX_LEN (name);
  char const *p;
  bool saw_slash = false;

  while (ISSLASH (*base))
    base++;

  for (p = base; *p; p++)
    {
      if (ISSLASH (*p))
        saw_slash = true;
      else if (saw_slash)
        {
          base = p;
          saw_slash = false;
        }
    }

  return (char *) base;
}

/* Return the length of the basename NAME.  Typically NAME is the
   value returned by base_name or last_component.  Act like strlen
   (NAME), except omit all trailing slashes.  */

size_t
base_len (char const *name)
{
  size_t len;
  size_t prefix_len = FILE_SYSTEM_PREFIX_LEN (name);

  for (len = strlen (name);  1 < len && ISSLASH (name[len - 1]);  len--)
    continue;

  if (DOUBLE_SLASH_IS_DISTINCT_ROOT && len == 1
      && ISSLASH (name[0]) && ISSLASH (name[1]) && ! name[2])
    return 2;

  if (FILE_SYSTEM_DRIVE_PREFIX_CAN_BE_RELATIVE && prefix_len
      && len == prefix_len && ISSLASH (name[prefix_len]))
    return prefix_len + 1;

  return len;
}

/* Return the length of the prefix of FILE that will be used by
   dir_name.  If FILE is in the working directory, this returns zero
   even though 'dir_name (FILE)' will return ".".  Works properly even
   if there are trailing slashes (by effectively ignoring them).  */

size_t
dir_len (char const *file)
{
  size_t prefix_length = FILE_SYSTEM_PREFIX_LEN (file);
  size_t length;

  /* Advance prefix_length beyond important leading slashes.  */
  prefix_length += (prefix_length != 0
                    ? (FILE_SYSTEM_DRIVE_PREFIX_CAN_BE_RELATIVE
                       && ISSLASH (file[prefix_length]))
                    : (ISSLASH (file[0])
                       ? ((DOUBLE_SLASH_IS_DISTINCT_ROOT
                           && ISSLASH (file[1]) && ! ISSLASH (file[2])
                           ? 2 : 1))
                       : 0));

  /* Strip the basename and any redundant slashes before it.  */
  for (length = last_component (file) - file;
       prefix_length < length; length--)
    if (! ISSLASH (file[length - 1]))
      break;
  return length;
}


/* In general, we can't use the builtin 'dirname' function if available,
   since it has different meanings in different environments.
   In some environments the builtin 'dirname' modifies its argument.
   Return the leading directories part of FILE, allocated with malloc.
   Works properly even if there are trailing slashes (by effectively
   ignoring them).  Return NULL on failure.
   If lstat (FILE) would succeed, then { chdir (dir_name (FILE));
   lstat (base_name (FILE)); } will access the same file.  Likewise,
   if the sequence { chdir (dir_name (FILE));
   rename (base_name (FILE), "foo"); } succeeds, you have renamed FILE
   to "foo" in the same directory FILE was in.  */

char *
mdir_name (char const *file)
{
  size_t length = dir_len (file);
  bool append_dot = (length == 0
                     || (FILE_SYSTEM_DRIVE_PREFIX_CAN_BE_RELATIVE
                         && length == FILE_SYSTEM_PREFIX_LEN (file)
                         && file[2] != '\0' && ! ISSLASH (file[2])));
  char *dir = malloc (length + append_dot + 1);
  if (!dir)
    return NULL;
  memcpy (dir, file, length);
  if (append_dot)
    dir[length++] = '.';
  dir[length] = '\0';
  return dir;
}

/* Return the longest suffix of F that is a relative file name.
   If it has no such suffix, return the empty string.  */

static char const *
longest_relative_suffix (char const *f)
{
  for (f += FILE_SYSTEM_PREFIX_LEN (f); ISSLASH (*f); f++)
    continue;
  return f;
}

/* Concatenate two file name components, DIR and ABASE, in
   newly-allocated storage and return the result.
   The resulting file name F is such that the commands "ls F" and "(cd
   DIR; ls BASE)" refer to the same file, where BASE is ABASE with any
   file system prefixes and leading separators removed.
   Arrange for a directory separator if necessary between DIR and BASE
   in the result, removing any redundant separators.
   In any case, if BASE_IN_RESULT is non-NULL, set
   *BASE_IN_RESULT to point to the copy of ABASE in the returned
   concatenation.  However, if ABASE begins with more than one slash,
   set *BASE_IN_RESULT to point to the sole corresponding slash that
   is copied into the result buffer.
   Return NULL if malloc fails.  */

char *
mfile_name_concat (char const *dir, char const *abase, char **base_in_result)
{
  char const *dirbase = last_component (dir);
  size_t dirbaselen = base_len (dirbase);
  size_t dirlen = dirbase - dir + dirbaselen;
  size_t needs_separator = (dirbaselen && ! ISSLASH (dirbase[dirbaselen - 1]));

  char const *base = longest_relative_suffix (abase);
  size_t baselen = strlen (base);

  char *p_concat = malloc (dirlen + needs_separator + baselen + 1);
  char *p;

  if (p_concat == NULL)
    return NULL;

  p = mempcpy (p_concat, dir, dirlen);
  *p = DIRECTORY_SEPARATOR;
  p += needs_separator;

  if (base_in_result)
    *base_in_result = p - IS_ABSOLUTE_FILE_NAME (abase);

  p = mempcpy (p, base, baselen);
  *p = '\0';

  return p_concat;
}

/* Try to ensure dirs has enough room for a slot at index fd; free any
   contents already in that slot.  Return false and set errno to
   ENOMEM on allocation failure.  */
static bool
ensure_dirs_slot (size_t fd)
{
  if (fd < dirs_allocated)
    free (dirs[fd].name);
  else
    {
      size_t new_allocated;
      dir_info_t *new_dirs;

      new_allocated = 2 * dirs_allocated + 1;
      if (new_allocated <= fd)
        new_allocated = fd + 1;
      new_dirs =
        (dirs != NULL
         ? (dir_info_t *) realloc (dirs, new_allocated * sizeof *dirs)
         : (dir_info_t *) malloc (new_allocated * sizeof *dirs));
      if (new_dirs == NULL)
        return false;
      memset (new_dirs + dirs_allocated, 0,
              (new_allocated - dirs_allocated) * sizeof *dirs);
      dirs = new_dirs;
      dirs_allocated = new_allocated;
    }
  return true;
}

/* Return an absolute name of DIR in malloc'd storage.  */
static char *
get_name (char const *dir)
{
  char *cwd;
  char *result;
  int saved_errno;

  if (IS_ABSOLUTE_FILE_NAME (dir))
    return strdup (dir);

  /* We often encounter "."; treat it as a special case.  */
  cwd = getcwd (NULL, 0);
  if (!cwd || (dir[0] == '.' && dir[1] == '\0'))
    return cwd;

  result = mfile_name_concat (cwd, dir, NULL);
  saved_errno = errno;
  free (cwd);
  errno = saved_errno;
  return result;
}

/* Hook into the gnulib replacements for open() and close() to keep track
   of the open file descriptors.  */

/* Close FD, cleaning up any fd to name mapping if fd was visiting a
   directory.  */
void
_gl_unregister_fd (int fd)
{
  if (fd >= 0 && fd < dirs_allocated)
    {
      free (dirs[fd].name);
      dirs[fd].name = NULL;
    }
}

/* Mark FD as visiting FILENAME.  FD must be non-negative, and refer
   to an open file descriptor.  If REPLACE_OPEN_DIRECTORY is non-zero,
   this should only be called if FD is visiting a directory.  Close FD
   and return -1 if there is insufficient memory to track the
   directory name; otherwise return FD.  */
int
_gl_register_fd (int fd, const char *filename)
{
  struct stat statbuf;

  assert (0 <= fd);
  if (REPLACE_OPEN_DIRECTORY
      || (fstat (fd, &statbuf) == 0 && S_ISDIR (statbuf.st_mode)))
    {
      if (!ensure_dirs_slot (fd)
          || (dirs[fd].name = get_name (filename)) == NULL)
        {
          int saved_errno = errno;
          close (fd);
          errno = saved_errno;
          return -1;
        }
    }
  return fd;
}

/* Mark NEWFD as a duplicate of OLDFD; useful from dup, dup2, dup3,
   and fcntl.  Both arguments must be valid and distinct file
   descriptors.  Close NEWFD and return -1 if OLDFD is tracking a
   directory, but there is insufficient memory to track the same
   directory in NEWFD; otherwise return NEWFD.  */
int
_gl_register_dup (int oldfd, int newfd)
{
  assert (0 <= oldfd && 0 <= newfd && oldfd != newfd);
  if (oldfd < dirs_allocated && dirs[oldfd].name)
    {
      /* Duplicated a directory; must ensure newfd is allocated.  */
      if (!ensure_dirs_slot (newfd)
          || (dirs[newfd].name = strdup (dirs[oldfd].name)) == NULL)
        {
          int saved_errno = errno;
          close (newfd);
          errno = saved_errno;
          newfd = -1;
        }
    }
  else if (newfd < dirs_allocated)
    {
      /* Duplicated a non-directory; ensure newfd is cleared.  */
      free (dirs[newfd].name);
      dirs[newfd].name = NULL;
    }
  return newfd;
}

/* If FD is currently visiting a directory, then return the name of
   that directory.  Otherwise, return NULL and set errno.  */
const char *
_gl_directory_name (int fd)
{
  if (0 <= fd && fd < dirs_allocated && dirs[fd].name != NULL)
    return dirs[fd].name;
  /* At this point, fd is either invalid, or open but not a directory.
     If dup2 fails, errno is correctly EBADF.  */
  if (0 <= fd)
    {
      if (dup2 (fd, fd) == fd)
        errno = ENOTDIR;
    }
  else
    errno = EBADF;
  return NULL;
}


/* Implement fchdir() in terms of chdir().  */

int
fchdir (int fd)
{
  const char *name = _gl_directory_name (fd);
  return name ? chdir (name) : -1;
}
