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

#define G_LOG_DOMAIN "fsearch-privilege"

#include "fsearch_privilege.h"

#include <errno.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <pwd.h>
#include <unistd.h>

/* ── helper functions ── */

static bool
has_privileged_flag(int argc, char *argv[]) {
    for (int i = 0; i < argc; i++) {
        if (g_strcmp0(argv[i], "--privileged") == 0) {
            return true;
        }
    }
    return false;
}

static bool
ntfs_enabled_in_config(void) {
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    g_autofree char *config_path = g_build_filename(
        g_get_user_config_dir(), "fsearch", "fsearch.conf", NULL);

    if (!g_key_file_load_from_file(key_file, config_path, 0, NULL)) {
        return false;
    }

    return g_key_file_get_boolean(key_file, "NTFS",
                                   "ntfs_fast_scan_enabled", NULL);
}

/**
 * Get the original user UID from privilege elevation environment variables.
 * pkexec sets PKEXEC_UID, sudo sets SUDO_UID.
 * Returns the original user UID, or (uid_t)-1 if not found.
 */
static uid_t
get_original_uid(void) {
    const char *uid_str = g_getenv("PKEXEC_UID");
    if (uid_str && *uid_str) {
        return (uid_t)atoi(uid_str);
    }
    uid_str = g_getenv("SUDO_UID");
    if (uid_str && *uid_str) {
        return (uid_t)atoi(uid_str);
    }
    return (uid_t)-1;
}

/**
 * Get the elevation method.
 * Determines how the process obtained root privileges via environment variables.
 * Returns "pkexec" / "sudo" / "root" (running as root directly).
 */
static const char *
get_elevation_method(void) {
    if (g_getenv("PKEXEC_UID")) return "pkexec";
    if (g_getenv("SUDO_UID"))   return "sudo";
    return "root";
}

/* ── public API ── */

bool
privilege_is_root(void) {
    return geteuid() == 0;
}

void
privilege_request_if_needed(int argc, char *argv[]) {
    /* Already has --privileged flag, skip elevation check */
    if (has_privileged_flag(argc, argv)) {
        return;
    }

    /* Already root, no elevation needed */
    if (privilege_is_root()) {
        return;
    }

    /* NTFS fast scan not enabled in config, no elevation needed */
    if (!ntfs_enabled_in_config()) {
        return;
    }

    /* Get absolute path of the executable */
    g_autofree char *exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (!exe_path) {
        g_warning("[privilege] cannot determine executable path: %s",
                  g_strerror(errno));
        return;
    }

    /* Get current DISPLAY and pass it to the elevated process via CLI arg */
    const char *display = g_getenv("DISPLAY");

    g_debug("[privilege] requesting root via pkexec: %s (DISPLAY=%s)", exe_path, display ? display : "(null)");

    /* execlp variadic list ends at first NULL pointer.
     * When display is NULL, the first NULL terminates the list,
     * so --x-display and its value are simply omitted. */
    execlp("pkexec", "pkexec", exe_path, "--privileged",
           display ? "--x-display" : NULL,
           display ? display : NULL,
           (char *)NULL);

    /* execlp returned = authorization denied or pkexec not available */
    if (errno == ENOENT) {
        g_warning("[privilege] pkexec not found, NTFS fast scan disabled");
    } else {
        g_warning("[privilege] Polkit authorization denied, NTFS fast scan disabled");
    }
}

void
privilege_restore_xdg_paths(void) {
    /* Non-root process, nothing to do */
    if (!privilege_is_root()) {
        return;
    }

    uid_t original_uid = get_original_uid();
    if (original_uid == (uid_t)-1) {
        return;  /* Cannot determine original user, may be direct root login */
    }

    struct passwd *pw = getpwuid(original_uid);
    if (!pw) {
        g_warning("[privilege] cannot lookup home directory for uid %u", original_uid);
        return;
    }

    g_autofree char *config_home = g_build_filename(pw->pw_dir, ".config", NULL);
    g_autofree char *data_home = g_build_filename(pw->pw_dir, ".local", "share", NULL);

    g_setenv("XDG_CONFIG_HOME", config_home, true);
    g_setenv("XDG_DATA_HOME", data_home, true);

    g_debug("[privilege] restored XDG paths to %s / %s for user '%s'",
            config_home, data_home, pw->pw_name);
}

char *
privilege_get_status_text(void) {
    /* NTFS fast scan disabled in config */
    if (!ntfs_enabled_in_config()) {
        return g_strdup(_("MFT scan: disabled"));
    }

    /* Running as root — show elevation method */
    if (privilege_is_root()) {
        const char *method = get_elevation_method();
        return g_strdup_printf(_("MFT scan: enabled (%s)"), method);
    }

    /* Running as non-root with NTFS enabled — pkexec was attempted but failed */
    if (g_find_program_in_path("pkexec")) {
        return g_strdup(_("MFT scan: authorization denied"));
    }
    return g_strdup(_("MFT scan: pkexec not found"));
}
