/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fsearch.h"
#include "fsearch_privilege.h"
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

/* Strip --privileged and handle --x-display/--x-authority parameters */
static void
strip_privileged_flag(int *argc, char *argv[]) {
    char **dst = argv;
    char **src = argv;
    while (*src) {
        if (g_strcmp0(*src, "--privileged") == 0) {
            src++;
        } else if (g_strcmp0(*src, "--x-display") == 0 && *(src + 1)) {
            /* Restore DISPLAY env var after pkexec elevation */
            g_setenv("DISPLAY", *(src + 1), true);
            src += 2;
        } else if (g_strcmp0(*src, "--x-authority") == 0 && *(src + 1)) {
            /* Restore XAUTHORITY env var after pkexec elevation */
            g_setenv("XAUTHORITY", *(src + 1), true);
            src += 2;
        } else {
            *(dst++) = *(src++);
        }
    }
    *dst = NULL;
    *argc = (int)(dst - argv);
}

int
main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    /* Privilege elevation check (before gtk_init) */
    privilege_request_if_needed(argc, argv);

    /* Restore original user's config paths (before gtk_init, harmless for non-elevated) */
    privilege_restore_xdg_paths();

    /* Strip --privileged and --x-display params so GTK doesn't complain */
    strip_privileged_flag(&argc, argv);

    g_set_application_name(_("FSearch"));
    g_set_prgname("io.github.cboxdoerfer.FSearch");

    return g_application_run(G_APPLICATION(fsearch_application_new()), argc, argv);
}