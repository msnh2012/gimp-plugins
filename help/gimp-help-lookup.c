/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimp-help-lookup - a standalone gimp-help ID to filename mapper
 * Copyright (C)  2004 Sven Neumann <sven@gimp.org>
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "domain.h"
#include "help.h"
#include "locales.h"


static gchar * lookup (const gchar *help_domain,
                       const gchar *help_locales,
                       const gchar *help_id);


gint
main (gint   argc,
      gchar *argv[])
{
  const gchar *help_root    = g_getenv (GIMP_HELP_ENV_URI);
  const gchar *help_locales = NULL;
  const gchar *help_id      = argc > 1 ? argv[1] : GIMP_HELP_DEFAULT_ID;
  gchar       *uri;

  if (help_root)
    uri = g_strdup (help_root);
  else
    uri = g_filename_to_uri (DATADIR G_DIR_SEPARATOR_S GIMP_HELP_PREFIX,
                             NULL, NULL);

  domain_register (GIMP_HELP_DEFAULT_DOMAIN, uri);
  g_free (uri);

  uri = lookup (GIMP_HELP_DEFAULT_DOMAIN, help_locales, help_id);

  if (uri)
    {
      g_print ("%s\n", uri);
      g_free (uri);
    }

  return uri ? EXIT_SUCCESS : EXIT_FAILURE;
}

static gchar *
lookup (const gchar *help_domain,
        const gchar *help_locales,
        const gchar *help_id)
{
  HelpDomain *domain = domain_lookup (help_domain);

  if (domain)
    {
      GList *locales  = locales_parse (help_locales ?
                                       help_locales : GIMP_HELP_DEFAULT_LOCALE);
      gchar *full_uri = domain_map (domain, locales, help_id);

      g_list_foreach (locales, (GFunc) g_free, NULL);
      g_list_free (locales);

      return full_uri;
    }

  return NULL;
}
