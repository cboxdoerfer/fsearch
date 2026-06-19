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

/* Polkit action ID for NTFS MFT scanning */
#define FSEARCH_PRIVILEGE_ACTION_ID "org.freedesktop.policykit.exec"

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
 * privilege_request_async:
 * @callback: callback to invoke when authorization completes
 * @user_data: user data to pass to the callback
 *
 * Requests elevated privileges via Polkit. If already running as root,
 * the callback is invoked immediately with %TRUE. If Polkit is not
 * available, the callback is invoked with %FALSE.
 *
 * The callback will be invoked with %TRUE if authorization is granted,
 * %FALSE otherwise.
 */
void
privilege_request_async(void (*callback)(bool authorized, gpointer user_data),
                        gpointer user_data);

/**
 * privilege_get_status_text:
 * @is_root: whether the process is running as root
 * @is_authorized: whether Polkit authorization has been granted
 *
 * Returns a human-readable status string for UI display.
 * Returns: a newly allocated string (free with g_free)
 */
char *
privilege_get_status_text(bool is_root, bool is_authorized);

/**
 * privilege_is_authorized:
 *
 * Returns whether Polkit authorization has been granted during
 * the current application lifetime.
 *
 * Returns: %TRUE if authorized, %FALSE otherwise
 */
bool
privilege_is_authorized(void);

G_END_DECLS
