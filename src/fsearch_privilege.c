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

#include <polkit/polkit.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

typedef struct {
    void (*callback)(bool authorized, gpointer user_data);
    gpointer user_data;
} PrivilegeRequestData;

static gboolean
privilege_invoke_on_idle(gpointer user_data) {
    PrivilegeRequestData *data = (PrivilegeRequestData *)user_data;
    data->callback(true, data->user_data);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void
on_authorize_finish(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    PrivilegeRequestData *data = (PrivilegeRequestData *)user_data;
    g_autoptr(GError) error = NULL;

    PolkitAuthority *authority = POLKIT_AUTHORITY(source_object);
    PolkitAuthorizationResult *res =
        polkit_authority_check_authorization_finish(authority, result, &error);

    if (!res) {
        g_debug("[privilege] authorization failed: %s",
                error ? error->message : "unknown error");
        data->callback(false, data->user_data);
        g_free(data);
        return;
    }

    bool authorized = polkit_authorization_result_get_is_authorized(res);
    g_debug("[privilege] authorization %s", authorized ? "granted" : "denied");
    data->callback(authorized, data->user_data);
    g_free(data);
}

bool
privilege_is_root(void) {
    return geteuid() == 0;
}

void
privilege_request_async(void (*callback)(bool authorized, gpointer user_data),
                        gpointer user_data) {
    /* Already root — no need to request */
    if (privilege_is_root()) {
        PrivilegeRequestData *data = g_new(PrivilegeRequestData, 1);
        data->callback = callback;
        data->user_data = user_data;
        g_idle_add(privilege_invoke_on_idle, data);
        return;
    }

    PrivilegeRequestData *data = g_new(PrivilegeRequestData, 1);
    data->callback = callback;
    data->user_data = user_data;

    g_autoptr(GError) error = NULL;
    g_autoptr(PolkitAuthority) authority =
        polkit_authority_get_sync(NULL, &error);

    if (!authority) {
        g_debug("[privilege] Polkit authority not available: %s",
                error ? error->message : "unknown error");
        data->callback(false, data->user_data);
        g_free(data);
        return;
    }

    g_autoptr(PolkitSubject) subject = polkit_unix_process_new(getpid());

    polkit_authority_check_authorization(authority,
                                         subject,
                                         FSEARCH_PRIVILEGE_ACTION_ID,
                                         NULL,
                                         POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                         NULL,
                                         (GAsyncReadyCallback)on_authorize_finish,
                                         data);
}

char *
privilege_get_status_text(bool is_root, bool is_authorized) {
    if (is_root) {
        return g_strdup(_("root: active"));
    }
    if (is_authorized) {
        return g_strdup(_("root: authorized"));
    }
    return g_strdup(_("root: not granted"));
}
