/*
 * Unix SMB/CIFS implementation.
 * Samba utility functions
 * Copyright (C) Andrew Tridgell 1992-1998
 * Copyright (C) Jeremy Allison 2001-2007
 * Copyright (C) Simo Sorce 2001
 * Copyright (C) Jim McDonough <jmcd@us.ibm.com> 2003
 * Copyright (C) James Peach 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "replace.h"
#include <talloc.h>
#include "lib/util/debug.h"
#include "lib/util/samba_util.h"
#include "lib/util_path.h"

struct loadparm_substitution;
struct share_params;
#include "source3/param/param_proto.h"

/**
 * @brief Returns an absolute path to a file concatenating the provided
 * @a rootpath and @a basename
 *
 * @param name Filename, relative to @a rootpath
 *
 * @retval Pointer to a string containing the full path.
 **/

static char *xx_path(TALLOC_CTX *mem_ctx,
		     const char *name,
		     const char *rootpath)
{
	char *fname = NULL;

	fname = talloc_strdup(mem_ctx, rootpath);
	if (!fname) {
		return NULL;
	}
	trim_string(fname,"","/");

	if (!directory_create_or_exist(fname, 0755)) {
		return NULL;
	}

	return talloc_asprintf_append(fname, "/%s", name);
}

/**
 * @brief Returns an absolute path to a file in the Samba lock directory.
 *
 * @param name File to find, relative to LOCKDIR.
 *
 * @retval Pointer to a talloc'ed string containing the full path.
 **/

char *lock_path(TALLOC_CTX *mem_ctx, const char *name)
{
	return xx_path(mem_ctx, name, lp_lock_directory());
}

/**
 * @brief Returns an absolute path to a file in the Samba state directory.
 *
 * @param name File to find, relative to STATEDIR.
 *
 * @retval Pointer to a talloc'ed string containing the full path.
 **/

char *state_path(TALLOC_CTX *mem_ctx, const char *name)
{
	return xx_path(mem_ctx, name, lp_state_directory());
}

/**
 * @brief Returns an absolute path to a file in the Samba cache directory.
 *
 * @param name File to find, relative to CACHEDIR.
 *
 * @retval Pointer to a talloc'ed string containing the full path.
 **/

char *cache_path(TALLOC_CTX *mem_ctx, const char *name)
{
	return xx_path(mem_ctx, name, lp_cache_directory());
}

/**
 * @brief Removes any invalid path components in an absolute POSIX path.
 *
 * @param ctx Talloc context to return string.
 *
 * @param abs_path Absolute path string to process.
 *
 * @retval Pointer to a talloc'ed string containing the absolute full path.
 **/

char *canonicalize_absolute_path(TALLOC_CTX *ctx, const char *pathname_in)
{
	/*
	 * Note we use +2 here so if pathname_in=="" then we
	 * have space to return "/".
	 */
	char *pathname = talloc_array(ctx, char, strlen(pathname_in)+2);
	const char *s = pathname_in;
	char *p = pathname;

	if (pathname == NULL) {
		return NULL;
	}

	/* Always start with a '/'. */
	*p++ = '/';

	while (*s) {
		/* Deal with '/' or multiples of '/'. */
		if (s[0] == '/') {
			while (s[0] == '/') {
				/* Eat trailing '/' */
				s++;
			}
			/* Update target with one '/' */
			if (p[-1] != '/') {
				*p++ = '/';
			}
			continue;
		}
		if (p[-1] == '/') {
			/* Deal with "./" or ".\0" */
			if (s[0] == '.' &&
					(s[1] == '/' || s[1] == '\0')) {
				/* Eat the dot. */
				s++;
				while (s[0] == '/') {
					/* Eat any trailing '/' */
					s++;
				}
				/* Don't write anything to target. */
				continue;
			}
			/* Deal with "../" or "..\0" */
			if (s[0] == '.' && s[1] == '.' &&
					(s[2] == '/' || s[2] == '\0')) {
				/* Eat the dot dot. */
				s += 2;
				while (s[0] == '/') {
					/* Eat any trailing '/' */
					s++;
				}
				/*
				 * As we're on the slash, we go back
				 * one character to point p at the
				 * slash we just saw.
				 */
				if (p > pathname) {
					p--;
				}
				/*
				 * Now go back to the slash
				 * before the one that p currently points to.
				 */
				while (p > pathname) {
					p--;
					if (p[0] == '/') {
						break;
					}
				}
				/*
				 * Step forward one to leave the
				 * last written '/' alone.
				 */
				p++;

				/* Don't write anything to target. */
				continue;
			}
		}
		/* Non-separator character, just copy. */
		*p++ = *s++;
	}
	if (p[-1] == '/') {
		/*
		 * We finished on a '/'.
		 * Remove the trailing '/', but not if it's
		 * the sole character in the path.
		 */
		if (p > pathname + 1) {
			p--;
		}
	}
	/* Terminate and we're done ! */
	*p++ = '\0';
	return pathname;
}

/*
 * Take two absolute paths, figure out if "subdir" is a proper
 * subdirectory of "parent". Return the component relative to the
 * "parent" without the potential "/". Take care of "parent"
 * possibly ending in "/".
 */
bool subdir_of(const char *parent,
	       size_t parent_len,
	       const char *subdir,
	       const char **_relative)
{
	const char *relative = NULL;
	bool matched;

	SMB_ASSERT(parent[0] == '/');
	SMB_ASSERT(subdir[0] == '/');

	if (parent_len == 1) {
		/*
		 * Everything is below "/"
		 */
		*_relative = subdir+1;
		return true;
	}

	if (parent[parent_len-1] == '/') {
		parent_len -= 1;
	}

	matched = (strncmp(subdir, parent, parent_len) == 0);
	if (!matched) {
		return false;
	}

	relative = &subdir[parent_len];

	if (relative[0] == '\0') {
		*_relative = relative; /* nothing left */
		return true;
	}

	if (relative[0] == '/') {
		/* End of parent must match a '/' in subdir. */
		*_relative = relative+1;
		return true;
	}

	return false;
}
