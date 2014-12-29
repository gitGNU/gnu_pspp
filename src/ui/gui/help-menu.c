/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */


#include <config.h>

#include <gtk/gtk.h>

#include <libpspp/copyleft.h>
#include <libpspp/version.h>
#include "help-menu.h"
#include <libpspp/message.h>

#include "gl/configmake.h"
#include "gl/relocatable.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static const gchar *artists[] = { "Bastián Díaz", "Hugo Alejandro", NULL};

static void
about_new (GtkMenuItem *m, GtkWindow *parent)
{
  GtkWidget *about =  gtk_about_dialog_new ();

  gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about), "pspp");

  gtk_window_set_icon_name (GTK_WINDOW (about), "pspp");

  gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about), PACKAGE_URL);

  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about),
				bare_version);

  gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about),
				(const gchar **) authors);

  gtk_about_dialog_set_artists (GTK_ABOUT_DIALOG (about),
				artists);

  gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (about),
				copyleft);

  gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (about),
				 _("A program for the analysis of sampled data"));

  gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (about),
				  "Free Software Foundation");

  gtk_about_dialog_set_translator_credits 
    (
     GTK_ABOUT_DIALOG (about),
     /* TRANSLATORS: Do not translate this string.  Instead, put the names of the people
	who have helped in the translation. */
     _("translator-credits")
     );

  gtk_window_set_transient_for (GTK_WINDOW (about), parent);

  gtk_window_set_modal (GTK_WINDOW (about), TRUE);

  gtk_dialog_run (GTK_DIALOG (about));

  gtk_widget_hide (about);
}

/* Open the manual at PAGE */
void
online_help (const char *page)
{
  GError *err = NULL;
  gchar *cmd = NULL;

  gchar *argv[3] = { "yelp", 0, 0};

  if (page == NULL)
    argv[1] = g_strdup_printf ("file://%s", relocate (DOCDIR "/pspp.xml"));
  else
    argv[1] = g_strdup_printf ("file://%s#%s", relocate (DOCDIR "/pspp.xml"), page);

  if (! g_spawn_async (NULL, argv,
		       NULL, G_SPAWN_SEARCH_PATH,
		       NULL, NULL,   NULL,   &err))
    {
      msg (ME, _("Cannot open reference manual: %s.  The PSPP user manual is "
                 "also available at %s"),
                  err->message,
                  PACKAGE_URL "documentation.html");
    }

  g_free (cmd);
  g_clear_error (&err);
}

static void
reference_manual (GtkMenuItem *menu, gpointer data)
{
  online_help (NULL);
}



void
merge_help_menu (GtkUIManager *uim)
{
  GtkActionGroup *action_group = gtk_action_group_new ("help");

  static const GtkActionEntry entries[] =
    {
      {
	"help", NULL,                               /* name, stock id */
	N_("_Help"), NULL,                          /* label, accelerator */
	NULL,
	NULL,
      },
    
      {
	"help_reference", "help-reference-manual",            /* name, stock id */
	N_("_Reference Manual"), "F1",               /* label, accelerator */
	NULL,                                        /* tooltip */
	G_CALLBACK (reference_manual)
      },
    
      {
	"help_about", "help-about",
	NULL, NULL, NULL,
	G_CALLBACK (about_new)
      },
    };

  gtk_action_group_set_translation_domain (action_group, PACKAGE);

  gtk_ui_manager_add_ui_from_string   (uim, "\
      <menubar name=\"menubar\">\
        <menu action=\"help\">\
          <menuitem action=\"help_reference\"/>\
          <menuitem action=\"help_about\"/>\
        </menu>\
       </menubar>\
       ", -1, 0);

  gtk_action_group_add_actions (action_group, entries, G_N_ELEMENTS (entries), NULL);

  gtk_ui_manager_insert_action_group  (uim, action_group, 0);
}
