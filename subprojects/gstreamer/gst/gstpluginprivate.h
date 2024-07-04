/* GStreamer
 * Copyright (C) Fluendo S.A. <engineering@fluendo.com>
 * Copyright (C) Tim-Philipp Müller <tim@centricular.net>
 * Copyright (C) Alexander Slobodeniuk <aslobodeniuk@fluendo.com>
 * Copyright (C) Fabian Orccon <forccon@fluendo.com>
 *
 * gstpluginprivate.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_PLUGIN_PRIVATE_H__
#define __GST_PLUGIN_PRIVATE_H__

#include "gst_private.h"

G_BEGIN_DECLS

static const int GST_HASH_INIT = 5381;

static guint
gst_str_hash_append (gconstpointer v, const guint *accumulated_hash)
{
  const signed char *p;
  guint h = *accumulated_hash;

  for (p = v; *p != '\0'; p++)
    h = (h << 5) + h + *p;

  return h;
}

/* Scenarios:
 * ENV + xyz     where ENV can contain multiple values separated by SEPARATOR
 *               xyz may be "" (if ENV contains path to file rather than dir)
 * ENV + *xyz   same as above, but xyz acts as suffix filter
 * ENV + xyz*   same as above, but xyz acts as prefix filter (is this needed?)
 * ENV + *xyz*  same as above, but xyz acts as strstr filter (is this needed?)
 *
 * same as above, with additional paths hard-coded at compile-time:
 *   - only check paths + ... if ENV is not set or yields not paths
 *   - always check paths + ... in addition to ENV
 *
 * When user specifies set of environment variables, he/she may also use e.g.
 * "HOME/.mystuff/plugins", and we'll expand the content of $HOME with the
 * remainder
 */

/* we store in registry:
 *  sets of:
 *   {
 *     - environment variables (array of strings)
 *     - last hash of env variable contents (uint) (so we can avoid doing stats
 *       if one of the env vars has changed; premature optimisation galore)
 *     - hard-coded paths (array of strings)
 *     - xyz filename/suffix/prefix strings (array of strings)
 *     - flags (int)
 *     - last hash of file/dir stats (int)
 *   }
 *   (= struct GstPluginDep)
 */

static guint
gst_plugin_ext_dep_get_env_vars_hash (GstPlugin * plugin, GstPluginDep * dep)
{
  gchar **e;
  guint hash = GST_HASH_INIT;

  /* there's no deeper logic to what we do here; all we want to know (when
   * checking if the plugin needs to be rescanned) is whether the content of
   * one of the environment variables in the list is different from when it
   * was last scanned */
  for (e = dep->env_vars; e != NULL && *e != NULL; ++e) {
    const gchar *val;
    gchar env_var[256];

    /* want environment variable at beginning of string */
    if (!g_ascii_isalnum (**e)) {
      GST_WARNING_OBJECT (plugin, "string prefix is not a valid environment "
          "variable string: %s", *e);
      continue;
    }

    /* user is allowed to specify e.g. "HOME/.pitivi/plugins" */
    g_strlcpy (env_var, *e, sizeof (env_var));
    g_strdelimit (env_var, "/\\", '\0');

    if ((val = g_getenv (env_var))) {
      hash = gst_str_hash_append (":", &hash);
      hash = gst_str_hash_append (env_var, &hash);
      hash = gst_str_hash_append ("=", &hash);
      hash = gst_str_hash_append (val, &hash);
    }
  }

  return hash;
}

G_END_DECLS

#endif /* __GST_PLUGIN_PRIVATE_H__ */
