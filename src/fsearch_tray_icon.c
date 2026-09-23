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

#define G_LOG_DOMAIN "fsearch-tray-icon"

#include "fsearch_tray_icon.h"
#include "fsearch.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libappindicator/app-indicator.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _FsearchTrayIcon
{
  AppIndicator *indicator;
  GtkApplication      *app;
};

static void
on_show_window (GtkMenuItem *menu_item, gpointer user_data)
{
  FsearchTrayIcon *self = user_data;
  FsearchApplication *app = FSEARCH_APPLICATION (self->app);
  fsearch_application_present_window(app);
}

static void
on_quit (GtkMenuItem *menu_item, gpointer user_data)
{
  FsearchTrayIcon *self = user_data;
  g_application_quit (G_APPLICATION (self->app));
}

static GtkWidget *
create_menu (FsearchTrayIcon *self)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *show_item = gtk_menu_item_new_with_label (_("Show Window"));
  g_signal_connect (show_item, "activate", G_CALLBACK (on_show_window), self);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), show_item);

  GtkWidget *quit_item = gtk_menu_item_new_with_label (_("Quit"));
  g_signal_connect (quit_item, "activate", G_CALLBACK (on_quit), self);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), quit_item);

  gtk_widget_show_all (menu);

  return menu;
}

FsearchTrayIcon *
fsearch_tray_icon_new (GtkApplication *app)
{
  g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

  FsearchTrayIcon *self = g_new0 (FsearchTrayIcon, 1);
  self->app = app;

  self->indicator = app_indicator_new ("fsearch",
                                               "system-search",
                                               APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

  if (!self->indicator)
    {
      g_free (self);
      g_warning ("Failed to create AppIndicator");
      return NULL;
    }

  app_indicator_set_status (self->indicator, APP_INDICATOR_STATUS_ACTIVE);
  app_indicator_set_menu (self->indicator, GTK_MENU (create_menu (self)));

  return self;
}

void
fsearch_tray_icon_free (FsearchTrayIcon *self)
{
  if (!self)
    return;

  if (self->indicator)
    {
      g_object_unref (self->indicator);
      self->indicator = NULL;
    }

  g_free (self);
}
