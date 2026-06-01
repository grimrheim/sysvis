#include "gui.h"
#include <gtk/gtk.h>

static EventBuf *g_buf;
static GtkListBox *event_list;
static GtkScrolledWindow *scroll_win;

static gboolean poll_events(gpointer user_data) {
    (void)user_data;
    SyscallEvent ev;
    while (eventbuf_pop(g_buf, &ev)) {
        char text[512];
        snprintf(text, sizeof(text), "%5d  %-16s  %s  = %ld", 
                ev.pid, ev.name, ev.str_arg[0] ? ev.str_arg : "", ev.retval);
        GtkWidget *row = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(row), 0.0);
        gtk_list_box_append(event_list, row);
        GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(scroll_win);
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
    }
    return G_SOURCE_CONTINUE;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "sysvis");
    gtk_window_set_default_size(GTK_WINDOW(win), 1280, 720);

    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *list   = gtk_list_box_new();
    event_list        = GTK_LIST_BOX(list);
    scroll_win        = GTK_SCROLLED_WINDOW(scroll);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_window_set_child(GTK_WINDOW(win), scroll);
    gtk_widget_set_visible(win, TRUE);

    g_timeout_add(100, poll_events, NULL);
}

void gui_init(EventBuf *events) {
    g_buf = events;
    GtkApplication *app = gtk_application_new("org.sysvis", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
}