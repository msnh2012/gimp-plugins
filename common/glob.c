/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* The idea is taken from a plug-in written by George Hartz; the code isn't.
 */

#include "config.h"

#include <string.h>

#include "libgimp/gimp.h"


#define PROCEDURE_NAME "file_glob"

static void      query        (void);
static void      run          (const gchar      *name,
                               gint              nparams,
                               const GimpParam  *param,
                               gint             *nreturn_vals,
                               GimpParam       **return_vals);

static gboolean  glob_match   (const gchar      *pattern,
                               gint             *num_matches,
                               gchar          ***matches);
static gboolean  glob_fnmatch (const gchar      *pattern,
                               const gchar      *string);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run,
};

MAIN ()

static void
query (void)
{
  static GimpParamDef glob_args[] =
  {
    { GIMP_PDB_STRING,      "pattern",   "The glob pattern" }
  };

  static GimpParamDef glob_return_vals[] =
  {
    { GIMP_PDB_INT32,       "num_files", "The number of returned names" },
    { GIMP_PDB_STRINGARRAY, "files",     "The list of matching names"   }
  };


  gimp_install_procedure (PROCEDURE_NAME,
                          "Returns a list of matching filenames",
                          "This can be useful in scripts and other plugins "
                          "(e.g., batch-conversion). See the glob(7) manpage "
                          "for more info.",
                          "Sven Neumann",
                          "Sven Neumann",
                          "2004",
                          NULL,
			  NULL,
                          GIMP_PLUGIN,
			  G_N_ELEMENTS (glob_args),
			  G_N_ELEMENTS (glob_return_vals),
			  glob_args,
			  glob_return_vals);
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam values[3];

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_CALLING_ERROR;

  if (strcmp (name, PROCEDURE_NAME) == 0)
    {
      gchar **matches;
      gint    num_matches;

      if (! glob_match (param[0].data.d_string, &num_matches, &matches))
        {
          values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;
          return;
	}

      *nreturn_vals = 3;

      values[0].type               = GIMP_PDB_STATUS;
      values[0].data.d_status      = GIMP_PDB_SUCCESS;

      values[1].type               = GIMP_PDB_INT32;
      values[1].data.d_int32       = num_matches;

      values[2].type               = GIMP_PDB_STRINGARRAY;
      values[2].data.d_stringarray = matches;
    }
}

static gboolean
glob_match (const gchar   *pattern,
            gint          *num_matches,
            gchar       ***matches)
{
  g_return_val_if_fail (pattern != NULL, FALSE);
  g_return_val_if_fail (num_matches != NULL, FALSE);
  g_return_val_if_fail (matches != NULL, FALSE);

  *num_matches = 0;
  *matches     = NULL;

  return TRUE;
}


/*
 * The following code is borrowed from GTK+.
 *
 * GTK+ used to use a old version of GNU fnmatch() that was buggy
 * in various ways and didn't handle UTF-8. The following is
 * converted to UTF-8. To simplify the process of making it
 * correct, this is special-cased to the combinations of flags
 * that gtkfilesel.c uses.
 *
 *   FNM_FILE_NAME   - always set
 *   FNM_LEADING_DIR - never set
 *   FNM_NOESCAPE    - set only on windows
 *   FNM_CASEFOLD    - set only on windows
 */

/* We need to make sure that all constants are defined
 * to properly compile this file
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

static gunichar
get_char (const char **str)
{
  gunichar c = g_utf8_get_char (*str);
  *str = g_utf8_next_char (*str);

#ifdef G_PLATFORM_WIN32
  c = g_unichar_tolower (c);
#endif

  return c;
}

#if defined(G_OS_WIN32) || defined(G_WITH_CYGWIN)
#define DO_ESCAPE 0
#else
#define DO_ESCAPE 1
#endif

static gunichar
get_unescaped_char (const char **str,
		    gboolean    *was_escaped)
{
  gunichar c = get_char (str);

  *was_escaped = DO_ESCAPE && c == '\\';
  if (*was_escaped)
    c = get_char (str);

  return c;
}

/* Match STRING against the filename pattern PATTERN, returning zero if
   it matches, nonzero if not.  */

static gboolean
fnmatch_intern (const gchar *pattern,
                const gchar *string,
                gboolean     component_start,
                gboolean     no_leading_period)
{
  const char *p = pattern, *n = string;

  while (*p)
    {
      const char *last_n = n;

      gunichar c = get_char (&p);
      gunichar nc = get_char (&n);

      switch (c)
	{
   	case '?':
	  if (nc == '\0')
	    return FALSE;
	  else if (nc == G_DIR_SEPARATOR)
	    return FALSE;
	  else if (nc == '.' && component_start && no_leading_period)
	    return FALSE;
	  break;
	case '\\':
	  if (DO_ESCAPE)
	    c = get_char (&p);
	  if (nc != c)
	    return FALSE;
	  break;
	case '*':
	  if (nc == '.' && component_start && no_leading_period)
	    return FALSE;

	  {
	    const char *last_p = p;

	    for (last_p = p, c = get_char (&p);
		 c == '?' || c == '*';
		 last_p = p, c = get_char (&p))
	      {
		if (c == '?')
		  {
		    if (nc == '\0')
		      return FALSE;
		    else if (nc == G_DIR_SEPARATOR)
		      return FALSE;
		    else
		      {
			last_n = n; nc = get_char (&n);
		      }
		  }
	      }

	    /* If the pattern ends with wildcards, we have a
	     * guaranteed match unless there is a dir separator
	     * in the remainder of the string.
	     */
	    if (c == '\0')
	      {
		if (strchr (last_n, G_DIR_SEPARATOR) != NULL)
		  return FALSE;
		else
		  return TRUE;
	      }

	    if (DO_ESCAPE && c == '\\')
	      c = get_char (&p);

	    for (p = last_p; nc != '\0';)
	      {
		if ((c == '[' || nc == c) &&
		    fnmatch_intern (p, last_n,
                                    component_start, no_leading_period))
		  return TRUE;

		component_start = (nc == G_DIR_SEPARATOR);
		last_n = n;
		nc = get_char (&n);
	      }

	    return FALSE;
	  }

	case '[':
	  {
	    /* Nonzero if the sense of the character class is inverted.  */
	    gboolean not;
	    gboolean was_escaped;

	    if (nc == '\0' || nc == G_DIR_SEPARATOR)
	      return FALSE;

	    if (nc == '.' && component_start && no_leading_period)
	      return FALSE;

	    not = (*p == '!' || *p == '^');
	    if (not)
	      ++p;

	    c = get_unescaped_char (&p, &was_escaped);
	    for (;;)
	      {
		register gunichar cstart = c, cend = c;
		if (c == '\0')
		  /* [ (unterminated) loses.  */
		  return FALSE;

		c = get_unescaped_char (&p, &was_escaped);

		if (!was_escaped && c == '-' && *p != ']')
		  {
		    cend = get_unescaped_char (&p, &was_escaped);
		    if (cend == '\0')
		      return FALSE;

		    c = get_char (&p);
		  }

		if (nc >= cstart && nc <= cend)
		  goto matched;

		if (!was_escaped && c == ']')
		  break;
	      }
	    if (!not)
	      return FALSE;
	    break;

	  matched:;
	    /* Skip the rest of the [...] that already matched.  */
	    /* XXX 1003.2d11 is unclear if was_escaped is right.  */
	    while (was_escaped || c != ']')
	      {
		if (c == '\0')
		  /* [... (unterminated) loses.  */
		  return FALSE;

		c = get_unescaped_char (&p, &was_escaped);
	      }
	    if (not)
	      return FALSE;
	  }
	  break;

	default:
	  if (c != nc)
	    return FALSE;
	}

      component_start = (nc == G_DIR_SEPARATOR);
    }

  if (*n == '\0')
    return TRUE;

  return FALSE;
}

gboolean
glob_fnmatch (const gchar *pattern,
              const gchar *string)
{
  return fnmatch_intern (pattern, string, TRUE, TRUE);
}
