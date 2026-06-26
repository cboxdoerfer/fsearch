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

#pragma once

#include <glib.h>
#include <stdbool.h>

G_BEGIN_DECLS

/**
 * privilege_is_root:
 *
 * Checks if the current process is running as root (EUID == 0).
 *
 * Returns: %TRUE if running as root, %FALSE otherwise
 */
bool
privilege_is_root(void);

/**
 * privilege_request_if_needed:
 * @argc: argument count
 * @argv: argument vector
 *
 * Called early in main(), before gtk_init().
 * If NTFS fast scan is enabled and the process is not root,
 * attempts pkexec re-exec. If pkexec succeeds, this function
 * does not return (process replaced). If pkexec fails or is
 * not needed, returns normally.
 */
void
privilege_request_if_needed(int argc, char *argv[]);

/**
 * privilege_restore_xdg_paths:
 *
 * Called after privilege_request_if_needed(), before gtk_init().
 * If the process was elevated (via pkexec or sudo), detects the
 * original user and sets XDG_CONFIG_HOME / XDG_DATA_HOME to
 * point to the original user's directories.
 *
 * Safe to call from a non-elevated process (no-op).
 */
void
privilege_restore_xdg_paths(void);

/**
 * privilege_get_status_text:
 *
 * Returns a human-readable status string for UI display.
 * Called once during widget initialization — startup snapshot,
 * not real-time. The elevation method (pkexec/sudo/direct root)
 * and pkexec availability do not change during process lifetime.
 *
 * Returns one of:
 *   "MFT scan: disabled"          — NTFS fast scan disabled in config
 *   "MFT scan: enabled (pkexec)"  — running as root via pkexec
 *   "MFT scan: enabled (sudo)"    — running as root via sudo
 *   "MFT scan: enabled (root)"    — running as root directly
 *   "MFT scan: authorization denied" — pkexec available but user denied
 *   "MFT scan: pkexec not found"  — pkexec not found on system
 *
 * Returns: a newly allocated string (free with g_free)
 */
char *
privilege_get_status_text(void);

G_END_DECLS
