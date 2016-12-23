/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Colin Walters <walters@verbum.org>
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

#include <gio/gfiledescriptorbased.h>
#include <gio/gunixoutputstream.h>

#include "ostree-fetcher-util.h"
#include "otutil.h"

typedef struct
{
  GInputStream   *result_stream;
  gboolean         done;
  GError         **error;
}
  FetchUriSyncData;

static void
fetch_uri_sync_on_complete (GObject        *object,
                            GAsyncResult   *result,
                            gpointer        user_data)
{
  FetchUriSyncData *data = user_data;

  (void)_ostree_fetcher_request_finish ((OstreeFetcher*)object,
                                        result, NULL, &data->result_stream,
                                        data->error);
  data->done = TRUE;
}

gboolean
_ostree_fetcher_mirrored_request_to_membuf (OstreeFetcher  *fetcher,
                                            GPtrArray     *mirrorlist,
                                            const char     *filename,
                                            gboolean        add_nul,
                                            gboolean        allow_noent,
                                            GBytes         **out_contents,
                                            guint64        max_size,
                                            GCancellable   *cancellable,
                                            GError         **error)
{
  gboolean ret = FALSE;
  const guint8 nulchar = 0;
  g_autoptr(GMemoryOutputStream) buf = NULL;
  g_autoptr(GMainContext) mainctx = NULL;
  FetchUriSyncData data;
  g_assert (error != NULL);

  data.result_stream = NULL;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  mainctx = g_main_context_new ();
  g_main_context_push_thread_default (mainctx);

  data.done = FALSE;
  data.error = error;

  _ostree_fetcher_request_async (fetcher, mirrorlist, filename, 0, max_size,
                                 OSTREE_FETCHER_DEFAULT_PRIORITY, cancellable,
                                 fetch_uri_sync_on_complete, &data);
  while (!data.done)
    g_main_context_iteration (mainctx, TRUE);

  if (!data.result_stream)
    {
      if (allow_noent)
        {
          if (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              g_clear_error (error);
              ret = TRUE;
              *out_contents = NULL;
            }
        }
      goto out;
    }

  buf = (GMemoryOutputStream*)g_memory_output_stream_new (NULL, 0, g_realloc, g_free);
  if (g_output_stream_splice ((GOutputStream*)buf, data.result_stream,
                              G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
                              cancellable, error) < 0)
    goto out;

  if (add_nul)
    {
      if (!g_output_stream_write ((GOutputStream*)buf, &nulchar, 1, cancellable, error))
        goto out;
    }

  if (!g_output_stream_close ((GOutputStream*)buf, cancellable, error))
    goto out;

  ret = TRUE;
  *out_contents = g_memory_output_stream_steal_as_bytes (buf);
 out:
  if (mainctx)
    g_main_context_pop_thread_default (mainctx);
  g_clear_object (&(data.result_stream));
  return ret;
}

/* Helper for callers who just want to fetch single one-off URIs */
gboolean
_ostree_fetcher_request_uri_to_membuf (OstreeFetcher  *fetcher,
                                       OstreeFetcherURI *uri,
                                       gboolean        add_nul,
                                       gboolean        allow_noent,
                                       GBytes         **out_contents,
                                       guint64        max_size,
                                       GCancellable   *cancellable,
                                       GError         **error)
{
  g_autoptr(GPtrArray) mirrorlist = g_ptr_array_new ();
  g_ptr_array_add (mirrorlist, uri); /* no transfer */
  return _ostree_fetcher_mirrored_request_to_membuf (fetcher, mirrorlist, NULL,
                                                     add_nul, allow_noent,
                                                     out_contents, max_size,
                                                     cancellable, error);
}