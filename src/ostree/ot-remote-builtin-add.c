/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.0+
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "otutil.h"

#include "ot-main.h"
#include "ot-remote-builtins.h"

static char **opt_set;
static gboolean opt_no_gpg_verify;
static gboolean opt_if_not_exists;
static gboolean opt_force;
static char *opt_gpg_import;
static char *opt_contenturl;
static char *opt_collection_id;
static char *opt_sysroot;
static char *opt_repo;

/* ATTENTION:
 * Please remember to update the bash-completion script (bash/ostree) and
 * man page (man/ostree-remote.xml) when changing the option list.
 */

static GOptionEntry option_entries[] = {
  { "set", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_set, "Set config option KEY=VALUE for remote", "KEY=VALUE" },
  { "no-gpg-verify", 0, 0, G_OPTION_ARG_NONE, &opt_no_gpg_verify, "Disable GPG verification", NULL },
  { "if-not-exists", 0, 0, G_OPTION_ARG_NONE, &opt_if_not_exists, "Do nothing if the provided remote exists", NULL },
  { "force", 0, 0, G_OPTION_ARG_NONE, &opt_force, "Replace the provided remote if it exists", NULL },
  { "gpg-import", 0, 0, G_OPTION_ARG_FILENAME, &opt_gpg_import, "Import GPG key from FILE", "FILE" },
  { "contenturl", 0, 0, G_OPTION_ARG_STRING, &opt_contenturl, "Use URL when fetching content", "URL" },
  { "collection-id", 0, 0, G_OPTION_ARG_STRING, &opt_collection_id,
    "Globally unique ID for this repository as an collection of refs for redistribution to other repositories", "COLLECTION-ID" },
  { "repo", 0, 0, G_OPTION_ARG_FILENAME, &opt_repo, "Path to OSTree repository (defaults to /sysroot/ostree/repo)", "PATH" },
  { "sysroot", 0, 0, G_OPTION_ARG_FILENAME, &opt_sysroot, "Use sysroot at PATH (overrides --repo)", "PATH" },
  { NULL }
};

gboolean
ot_remote_builtin_add (int argc, char **argv, OstreeCommandInvocation *invocation, GCancellable *cancellable, GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  const char *remote_name;
  const char *remote_url;
  char **iter;
  g_autoptr(GVariantBuilder) optbuilder = NULL;
  g_autoptr(GVariant) options = NULL;
  gboolean ret = FALSE;

  context = g_option_context_new ("NAME [metalink=|mirrorlist=]URL [BRANCH...]");

  if (!ostree_option_context_parse (context, option_entries, &argc, &argv,
                                    invocation, NULL, cancellable, error))
    goto out;

  if (!ostree_parse_sysroot_or_repo_option (context, opt_sysroot, opt_repo,
                                            &sysroot, &repo,
                                            cancellable, error))
    goto out;

  if (argc < 3)
    {
      ot_util_usage_error (context, "NAME and URL must be specified", error);
      goto out;
    }

  if (opt_if_not_exists && opt_force)
    {
      ot_util_usage_error (context,
                           "Can only specify one of --if-not-exists and --force",
                           error);
      goto out;
    }

  remote_name = argv[1];
  remote_url  = argv[2];

  optbuilder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

  if (argc > 3)
    {
      g_autoptr(GPtrArray) branchesp = g_ptr_array_new ();
      int i;

      for (i = 3; i < argc; i++)
        g_ptr_array_add (branchesp, argv[i]);
      g_ptr_array_add (branchesp, NULL);

      g_variant_builder_add (optbuilder, "{s@v}",
                             "branches",
                             g_variant_new_variant (g_variant_new_strv ((const char*const*)branchesp->pdata, -1)));
    }

  /* We could just make users use --set instead for this since it's a string,
   * but e.g. when mirrorlist support is added, it'll be kinda awkward to type:
   *   --set=contenturl=mirrorlist=... */

  if (opt_contenturl != NULL)
    g_variant_builder_add (optbuilder, "{s@v}",
                           "contenturl", g_variant_new_variant (g_variant_new_string (opt_contenturl)));

  for (iter = opt_set; iter && *iter; iter++)
    {
      const char *keyvalue = *iter;
      g_autofree char *subkey = NULL;
      g_autofree char *subvalue = NULL;

      if (!ot_parse_keyvalue (keyvalue, &subkey, &subvalue, error))
        goto out;

      g_variant_builder_add (optbuilder, "{s@v}",
                             subkey, g_variant_new_variant (g_variant_new_string (subvalue)));
    }

  if (opt_no_gpg_verify)
    g_variant_builder_add (optbuilder, "{s@v}",
                           "gpg-verify",
                           g_variant_new_variant (g_variant_new_boolean (FALSE)));

  if (opt_collection_id != NULL)
    g_variant_builder_add (optbuilder, "{s@v}", "collection-id",
                           g_variant_new_variant (g_variant_new_take_string (g_steal_pointer (&opt_collection_id))));

  options = g_variant_ref_sink (g_variant_builder_end (optbuilder));

  OstreeRepoRemoteChange changeop;
  if (opt_if_not_exists)
    changeop = OSTREE_REPO_REMOTE_CHANGE_ADD_IF_NOT_EXISTS;
  else if (opt_force)
    changeop = OSTREE_REPO_REMOTE_CHANGE_REPLACE;
  else
    changeop = OSTREE_REPO_REMOTE_CHANGE_ADD;
  if (!ostree_repo_remote_change (repo, NULL, changeop,
                                  remote_name, remote_url,
                                  options,
                                  cancellable, error))
    goto out;

  /* This is just a convenience option and is not as flexible as the full
   * "ostree remote gpg-import" command.  It imports all keys from a file,
   * which is likely the most common case.
   *
   * XXX Not sure this interacts well with if-not-exists since we don't
   *     know whether the remote already existed.  We import regardless. */
  if (opt_gpg_import != NULL)
    {
      g_autoptr(GFile) file = NULL;
      g_autoptr(GInputStream) input_stream = NULL;
      guint imported = 0;

      file = g_file_new_for_path (opt_gpg_import);
      input_stream = (GInputStream *) g_file_read (file, cancellable, error);

      if (input_stream == NULL)
        goto out;

      if (!ostree_repo_remote_gpg_import (repo, remote_name, input_stream,
                                          NULL, &imported, cancellable, error))
        goto out;

      /* XXX If we ever add internationalization, use ngettext() here. */
      g_print ("Imported %u GPG key%s to remote \"%s\"\n",
               imported, (imported == 1) ? "" : "s", remote_name);
    }

  ret = TRUE;
 out:
  return ret;
}
