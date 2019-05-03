/*
 * Copyright (C) 2017 Cjacker Huang <cjacker at foxmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <gdk/gdkkeysyms-compat.h>

/*taken from surf/st/mt*/
#include "arg.h"

#define SPAWN_TIMEOUT (30 * 1000 /* 30s */)
#define DEFAULT_TERM "xterm-256color"

static int trans_percent = 0;
static gboolean reverse = FALSE;
static gboolean shortcuts = TRUE;
static gboolean decorated = TRUE;
//-1 means unlimited scroll back histroy 
static int history = -1;
static gchar * font = "Monospace 11";

static gchar * colorscheme = "plain";

static gchar **command = NULL;
char *argv0;

//the shell pid.
static int child_pid = -1;


static gboolean
on_term_title_changed(VteTerminal *terminal, gpointer user_data)
{
    GtkWindow *window = user_data;
    gtk_window_set_title(window, vte_terminal_get_window_title(terminal));
    return TRUE;
}

static void
set_font_size(VteTerminal *terminal, gint delta)
{
    PangoFontDescription *descr;
    if ((descr = pango_font_description_copy(vte_terminal_get_font(terminal))) == NULL)
        return;

    gint current = pango_font_description_get_size(descr);
    pango_font_description_set_size(descr, current + delta * PANGO_SCALE);
    vte_terminal_set_font(terminal, descr);
    pango_font_description_free(descr);
}

static void
reset_font_size(VteTerminal *terminal)
{
    PangoFontDescription *descr;
    if ((descr = pango_font_description_from_string(font)) == NULL)
        return;
    vte_terminal_set_font(terminal, descr);
    pango_font_description_free(descr);
}


static gboolean
on_key_press(VteTerminal *terminal, 
        GdkEventKey *event, 
        gpointer user_data)
{
    const char *text = NULL;
    const guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    const guint keyval = gdk_keyval_to_lower(event->keyval);

    //after pressed "ctrl-shift-c" will cause slow exit under wayland with GDK_BACKEND=x11
    
    if (modifiers == (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) {
        switch (keyval) {
            /* copy/paste with Ctrl+Shift+c/Ctrl+Shift+v */
            case GDK_KEY_c:
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
                return TRUE;
            case GDK_KEY_v:
                vte_terminal_paste_clipboard(terminal);
                return TRUE;
            case GDK_KEY_y:
                vte_terminal_paste_primary(terminal);
                return TRUE;
            case GDK_KEY_plus:
                set_font_size(terminal, 1);
                return TRUE;
        }
    } else if (event->state & GDK_CONTROL_MASK) {
        switch (event->keyval) {
        /*case GDK_plus:
            set_font_size(terminal, 1);
            return TRUE;*/
        case GDK_KEY_minus:
            set_font_size(terminal, -1);
            return TRUE;
        case GDK_KEY_equal:
            reset_font_size(terminal);
            return TRUE;
        }
    }
    return FALSE;
} 

static gboolean
handle_selection_changed(VteTerminal *terminal, 
        gpointer data)
{
    if (vte_terminal_get_has_selection(terminal))
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
    return TRUE;
}

static const char* 
get_process_name_by_pid(const int pid)
{
    char* name = (char*)calloc(256,sizeof(char));
    if(name){
        sprintf(name, "/proc/%d/cmdline",pid);
        FILE* f = fopen(name,"r");
        if(f){
            size_t size;
            size = fread(name, sizeof(char), 256, f);
            if(size>0){
                if('\n'==name[size-1])
                    name[size-1]='\0';
            }
            fclose(f);
        }
    }
    return name;
}

static gboolean 
have_foreground_process(VteTerminal *terminal)
{
    VtePty *pty;
    int fd;
    int fgpid;

    pty = vte_terminal_get_pty (terminal);
    if (pty == NULL)
        return FALSE;

    fd = vte_pty_get_fd (pty);
    if (fd == -1)
        return FALSE;

    fgpid = tcgetpgrp (fd);

    //special treatment for tmux, NOT treat it as foreground process.
    gchar * process_name = g_path_get_basename(get_process_name_by_pid(fgpid));
    if(!g_strcmp0(process_name, "tmux")) {
        g_free(process_name);
        return FALSE;
    }
    g_free(process_name);

    //no foreground process 
    if (fgpid == -1 || fgpid == child_pid)
        return FALSE;

    return TRUE;
}

static void
confirm_close_response_cb (GtkWidget *dialog, 
        int response,
        gpointer user_data)
{
    gtk_widget_destroy (dialog);

    if (response != GTK_RESPONSE_ACCEPT)
        return;

    gtk_main_quit();
}


static gboolean 
on_window_delete (GtkWidget *window,
        GdkEvent  *event,
        VteTerminal *terminal)
{
    if(have_foreground_process(terminal)) {
        GtkWidget *dialog;
        dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_WARNING,
                                         GTK_BUTTONS_CANCEL,
                                         "Close");
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                "%s", "Process running in this terminal. "
                      "Closing the terminal will kill it.");
        gtk_window_set_title (GTK_WINDOW (dialog), "");

        gtk_dialog_add_button (GTK_DIALOG (dialog), "Close", GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

        g_signal_connect (dialog, "response",
                      G_CALLBACK (confirm_close_response_cb), window);
		
        gtk_window_present (GTK_WINDOW (dialog));

    } else
        gtk_main_quit();
}

static void 
on_child_exit (VteTerminal *terminal,
        gint status,
        gpointer user_data)
{
    //avoid slow exit when "ctrl-shift-c" pressed under wayland with 
    //GDK_BACKEND=x11
    gtk_widget_unrealize(user_data);
	gtk_main_quit();
	exit(status);
}

static void 
spawn_callback (VteTerminal *terminal,
        GPid pid,
        GError *error,
        gpointer user_data)
{
	child_pid = pid;
    //error
    if(child_pid == -1)
        exit(0);
}


static void usage(void) {
  printf("Usage: xt [-r] [-k] [-f font] [-t transparent] [-n history] [[-e] command [args ...]]\n\n"
         "Args:\n"
         " -c <string>: set color scheme: tango, solarized, monokai, wombat\n"
         " -r: reverse terminal color scheme to dark, default is light\n"
         " -k: disable default shortcuts\n"
         " -w: disable Gtk CSD, default for sway wm\n"
         " -f <string>: set font, such as \"Monospace 11\"\n"
         " -t <int>: background tranparency percent\n"
         " -n <int>: lines of history, default is unlimited\n"
         " -e <command [args ...]>: excute command with args, eat all remainning args.\n\n"
         "Shortcuts:\n"
         " Ctrl+Shift+c: copy to clipboard\n"
         " Ctrl+Shift+v: paste from clipboard\n"
         " Ctrl+Shift+y: paste from primary\n"
         " Ctrl+Shift+=: increase font size for session\n"
         " Ctrl+-: decrease font size for session\n"
         " Ctrl+=: reset font size to default value\n"
         );
  exit(0);
}


int main(int argc, char **argv)
{
    ARGBEGIN {
    case 'c':
      colorscheme = EARGF(usage());
      break;
    case 'r':
      reverse = TRUE;
      break;
    case 'k':
      shortcuts = FALSE;
      break;
    case 'w':
      decorated = FALSE;
      break;
    case 'e':
      if (argc > 0)
        --argc, ++argv;
      goto run;
    case 'f':
      font = EARGF(usage());
      break;
    case 'n':
      history = strtol(EARGF(usage()), NULL, 0);
      break;
    case 't':
      trans_percent = strtol(EARGF(usage()), NULL, 0);
      break;
    case 'v':
      printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
      exit(0);
    default:
      usage();
    }
    ARGEND;

run:
    if (argc > 0) {
        /* eat all remaining arguments */
        command = argv;
    }

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    //not show CSD when GDK_BACKEND=wayland
    //disable GSD for sway by default.
    if(getenv("SWAYSOCK"))
        decorated = FALSE;

    if(!decorated)
        gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    GtkWidget *terminal = vte_terminal_new();
    gtk_container_add(GTK_CONTAINER(window), terminal);

    g_object_set_data(G_OBJECT(window), "terminal", terminal);

    gtk_widget_set_visual(window, gdk_screen_get_rgba_visual(gtk_widget_get_screen(window)));

    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), VTE_TERMINAL(terminal));
    g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exit), GTK_WINDOW(window));
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(on_term_title_changed), GTK_WINDOW(window));
    g_signal_connect(terminal, "selection-changed", G_CALLBACK(handle_selection_changed), NULL);
    if(shortcuts)
        g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), GTK_WINDOW(window));

    vte_terminal_set_audible_bell(VTE_TERMINAL(terminal), FALSE);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(terminal), VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_allow_hyperlink(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), history);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(terminal), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_rewrap_on_resize(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_allow_bold(VTE_TERMINAL(terminal), TRUE);

    reset_font_size(VTE_TERMINAL(terminal));


    GdkRGBA fg;
    GdkRGBA bg;
    fg.alpha = 1.0;
    bg.alpha = (double)(100 - trans_percent)/100.0;


    if(!g_strcmp0(colorscheme, "tango")) {
        //tango color
        //bg:#eeeeec
        //fg:#2e3436
        bg.red = 238.0/255;
        bg.green = 238.0/255;
        bg.blue = 236.0/255;

        fg.red = 46.0/255;
        fg.green = 52.0/255;
        fg.blue = 54.0/255;
    } else if(!g_strcmp0(colorscheme, "solarized")) {
        //solarized color
        //bg:#eee8d5
        //fg:#073642
        bg.red = 238.0/255;
        bg.green = 232.0/255;
        bg.blue = 213.0/255;

        fg.red = 7.0/255;
        fg.green = 54.0/255;
        fg.blue = 66.0/255;
    } else if(!g_strcmp0(colorscheme, "wombat")) {
        //wombat color
        //bg:#f6f3e8
        //fg:#242424
        bg.red = 246.0/255;
        bg.green = 243.0/255;
        bg.blue = 232.0/255;

        fg.red = 36.0/255;
        fg.green = 36.0/255;
        fg.blue = 36.0/255;
    } else if(!g_strcmp0(colorscheme, "monokai")) {
        //monokai color
        //bg:#f8f8f2
        //fg:#272822
        bg.red = 248.0/255;
        bg.green = 248.0/255;
        bg.blue = 242.0/255;

        fg.red = 39.0/255;
        fg.green = 40.0/255;
        fg.blue = 34.0/255;
    } else {
        //black/white
        fg.red = fg.green = fg.blue = 0.0;
        bg.red = bg.green = bg.blue = 1.0;
    }


    if(reverse) {
       GdkRGBA swap = bg;
       bg = fg;
       fg = swap;  
    }

    vte_terminal_set_colors(VTE_TERMINAL(terminal), &fg, &bg, NULL, 0);

    VtePtyFlags pty_flags = VTE_PTY_DEFAULT;
    GSpawnFlags spawn_flags = G_SPAWN_SEARCH_PATH_FROM_ENVP |
                              VTE_SPAWN_NO_PARENT_ENVV;
    GCancellable *cancellable = NULL;

    
    gchar *working_dir = NULL;
    gchar *shell = NULL;

    //set env
    gchar ** env = g_get_environ();
    GHashTable *env_table;
    GHashTableIter iter;
    char *e, *v;
    GPtrArray *retval;
    guint i;

    env_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    if (env) {
        for (i = 0; env[i]; ++i)
          {
            v = strchr (env[i], '=');
            if (v)
               g_hash_table_replace (env_table, g_strndup (env[i], v - env[i]), g_strdup (v + 1));
             else
               g_hash_table_replace (env_table, g_strdup (env[i]), NULL);
          }
    }

    g_hash_table_remove (env_table, "TERM");
    g_hash_table_remove (env_table, "COLUMNS");
    g_hash_table_remove (env_table, "LINES");
    g_hash_table_remove (env_table, "TERMCAP");
    g_hash_table_remove (env_table, "GNOME_DESKTOP_ICON");

    retval = g_ptr_array_sized_new (g_hash_table_size (env_table));
    g_hash_table_iter_init (&iter, env_table);
    while (g_hash_table_iter_next (&iter, (gpointer *) &e, (gpointer *) &v))
      g_ptr_array_add (retval, g_strdup_printf ("%s=%s", e, v ? v : ""));
    g_ptr_array_add (retval, NULL);

    //get SHELL from env. 
    shell = g_strdup (g_hash_table_lookup (env_table, "SHELL"));
    //get currect working dir.
    working_dir = g_strdup (g_hash_table_lookup (env_table, "PWD"));

    g_hash_table_destroy (env_table);
    env =  (char **) g_ptr_array_free (retval, FALSE);

    setenv("TERM", DEFAULT_TERM, 1);

    //construct the command vte should run.
    
    if(!shell)
        shell = g_strdup("/bin/bash");

    if(!working_dir)
        working_dir = g_strdup(g_get_home_dir ());

    gchar ** run_command;
    if(command)
       run_command = command;
    else
       run_command = (gchar *[]){shell , NULL };

    //spawn command.
    vte_terminal_spawn_async (VTE_TERMINAL(terminal),
                          pty_flags,
                          working_dir,
						  run_command,
                          env,
                          spawn_flags,
                          NULL,
                          NULL,
                          NULL,
                          SPAWN_TIMEOUT,
                          cancellable,
						  spawn_callback,
                          NULL);
    g_free(shell);
    g_free(working_dir);

    gtk_widget_show_all(window);
    gtk_window_set_focus(GTK_WINDOW(window), terminal);

    gtk_main();
    return 0;
}
