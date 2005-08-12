/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * The GIMP Help plug-in
 * Copyright (C) 1999-2004 Sven Neumann <sven@gimp.org>
 *                         Michael Natterer <mitch@gimp.org>
 *                         Henrik Brix Andersen <brix@gimp.org>
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

#include <string.h>  /*  strlen  */

#include <glib.h>

#include "libgimp/gimp.h"

#include "domain.h"
#include "help.h"
#include "locales.h"

#include "libgimp/stdplugins-intl.h"


/*  defines  */

#define GIMP_HELP_EXT_PROC        "extension-gimp-help"
#define GIMP_HELP_TEMP_EXT_PROC   "extension-gimp-help-temp"


typedef struct
{
  gchar *procedure;
  gchar *help_domain;
  gchar *help_locales;
  gchar *help_id;
} IdleHelp;


/*  forward declarations  */

static void     query             (void);
static void     run               (const gchar      *name,
                                   gint              nparams,
                                   const GimpParam  *param,
                                   gint             *nreturn_vals,
                                   GimpParam       **return_vals);

static void     temp_proc_install (void);
static void     temp_proc_run     (const gchar      *name,
                                   gint              nparams,
                                   const GimpParam  *param,
                                   gint             *nreturn_vals,
                                   GimpParam       **return_vals);

static void     load_help         (const gchar      *procedure,
                                   const gchar      *help_domain,
                                   const gchar      *help_locales,
                                   const gchar      *help_id);
static gboolean load_help_idle    (gpointer          data);


/*  local variables  */

static GMainLoop *main_loop = NULL;

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


MAIN ()

void
help_exit (void)
{
  if (main_loop)
    g_main_loop_quit (main_loop);
}


static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,       "num-domain-names", "" },
    { GIMP_PDB_STRINGARRAY, "domain-names",     "" },
    { GIMP_PDB_INT32,       "num-domain-uris",  "" },
    { GIMP_PDB_STRINGARRAY, "domain-uris",      "" }
  };

  gimp_install_procedure (GIMP_HELP_EXT_PROC,
                          "", /* FIXME */
                          "", /* FIXME */
                          "Sven Neumann <sven@gimp.org>, "
			  "Michael Natterer <mitch@gimp.org>, "
                          "Henrik Brix Andersen <brix@gimp.org>",
                          "Sven Neumann, Michael Natterer & Henrik Brix Andersen",
                          "1999-2004",
                          NULL,
                          "",
                          GIMP_EXTENSION,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[1];
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  const gchar       *default_env_domain_uri;
  gchar             *default_domain_uri;

  INIT_I18N ();

  /*  set default values  */
  default_env_domain_uri = g_getenv (GIMP_HELP_ENV_URI);

  if (default_env_domain_uri)
    {
      default_domain_uri = g_strdup (default_env_domain_uri);
    }
  else
    {
      gchar *help_root = g_build_filename (gimp_data_directory (),
                                           GIMP_HELP_PREFIX,
                                           NULL);

      default_domain_uri = g_filename_to_uri (help_root, NULL, NULL);

      g_free (help_root);
    }

  /*  make sure all the arguments are there  */
  if (nparams == 4)
    {
      gint    num_domain_names = param[0].data.d_int32;
      gchar **domain_names     = param[1].data.d_stringarray;
      gint    num_domain_uris  = param[2].data.d_int32;
      gchar **domain_uris      = param[3].data.d_stringarray;

      if (num_domain_names == num_domain_uris)
        {
          gint i;

          domain_register (GIMP_HELP_DEFAULT_DOMAIN, default_domain_uri, NULL);

          for (i = 0; i < num_domain_names; i++)
            {
              domain_register (domain_names[i], domain_uris[i], NULL);
            }
        }
      else
        {
          g_printerr ("help: number of names doesn't match number of URIs.\n");

          status = GIMP_PDB_CALLING_ERROR;
        }
    }
  else
    {
      g_printerr ("help: wrong number of arguments in procedure call.\n");

      status = GIMP_PDB_CALLING_ERROR;
    }

  g_free (default_domain_uri);

  if (status == GIMP_PDB_SUCCESS)
    {
      main_loop = g_main_loop_new (NULL, FALSE);

      temp_proc_install ();

      gimp_extension_ack ();
      gimp_extension_enable ();

      g_main_loop_run (main_loop);

      g_main_loop_unref (main_loop);
      main_loop = NULL;

      gimp_uninstall_temp_proc (GIMP_HELP_TEMP_EXT_PROC);
    }

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;
}

static void
temp_proc_install (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_STRING, "procedure",    "The procedure of the browser to use" },
    { GIMP_PDB_STRING, "help-domain",  "Help domain to use" },
    { GIMP_PDB_STRING, "help-locales", "Language to use"    },
    { GIMP_PDB_STRING, "help-id",      "Help ID to open"    }
  };

  gimp_install_temp_proc (GIMP_HELP_TEMP_EXT_PROC,
                          "DON'T USE THIS ONE",
                          "(Temporary procedure)",
			  "Sven Neumann <sven@gimp.org>, "
			  "Michael Natterer <mitch@gimp.org>"
                          "Henrik Brix Andersen <brix@gimp.org",
			  "Sven Neumann, Michael Natterer & Henrik Brix Andersen",
			  "1999-2004",
                          NULL,
                          "",
                          GIMP_TEMPORARY,
                          G_N_ELEMENTS (args), 0,
                          args, NULL,
                          temp_proc_run);
}

static void
temp_proc_run (const gchar      *name,
               gint              nparams,
               const GimpParam  *param,
               gint             *nreturn_vals,
               GimpParam       **return_vals)
{
  static GimpParam   values[1];
  GimpPDBStatusType  status       = GIMP_PDB_SUCCESS;
  const gchar       *procedure    = NULL;
  const gchar       *help_domain  = GIMP_HELP_DEFAULT_DOMAIN;
  const gchar       *help_locales = NULL;
  const gchar       *help_id      = GIMP_HELP_DEFAULT_ID;

  *nreturn_vals = 1;
  *return_vals  = values;

  /*  make sure all the arguments are there  */
  if (nparams == 4)
    {
      if (param[0].data.d_string && strlen (param[0].data.d_string))
        procedure = param[0].data.d_string;

      if (param[1].data.d_string && strlen (param[1].data.d_string))
        help_domain = param[1].data.d_string;

      if (param[2].data.d_string && strlen (param[2].data.d_string))
        help_locales = param[2].data.d_string;

      if (param[3].data.d_string && strlen (param[3].data.d_string))
        help_id = param[3].data.d_string;
    }

  if (! procedure)
    status = GIMP_PDB_CALLING_ERROR;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  if (status == GIMP_PDB_SUCCESS)
    load_help (procedure, help_domain, help_locales, help_id);
}

static void
load_help (const gchar *procedure,
           const gchar *help_domain,
           const gchar *help_locales,
           const gchar *help_id)
{
  IdleHelp *idle_help;

  idle_help = g_new0 (IdleHelp, 1);

  idle_help->procedure    = g_strdup (procedure);
  idle_help->help_domain  = g_strdup (help_domain);
  idle_help->help_locales = g_strdup (help_locales);
  idle_help->help_id      = g_strdup (help_id);

  g_idle_add (load_help_idle, idle_help);
}

static gboolean
load_help_idle (gpointer data)
{
  IdleHelp   *idle_help = data;
  HelpDomain *domain;

  domain = domain_lookup (idle_help->help_domain);

  if (domain)
    {
      GList *locales = locales_parse (idle_help->help_locales);
      gchar *full_uri;

      full_uri = domain_map (domain, locales, idle_help->help_id);

      g_list_foreach (locales, (GFunc) g_free, NULL);
      g_list_free (locales);

      if (full_uri)
        {
          GimpParam *return_vals;
          gint       n_return_vals;

#ifdef GIMP_HELP_DEBUG
          g_printerr ("help: calling '%s' for '%s'\n",
                      idle_help->procedure, full_uri);
#endif

          return_vals = gimp_run_procedure (idle_help->procedure,
                                            &n_return_vals,
                                            GIMP_PDB_STRING, full_uri,
                                            GIMP_PDB_END);

          gimp_destroy_params (return_vals, n_return_vals);

          g_free (full_uri);
        }
    }

  g_free (idle_help->procedure);
  g_free (idle_help->help_domain);
  g_free (idle_help->help_locales);
  g_free (idle_help->help_id);
  g_free (idle_help);

  return FALSE;
}
