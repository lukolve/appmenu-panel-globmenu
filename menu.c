// MIT License
// Lukas Veselovsky, lukve
// Created with magician help of the AI.
//
// Upravené pre zobrazenie Title namiesto globálneho menu.

#include "gtk/gtk.h"
#include <gdk/gdkx.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>

static GtkWidget *placeholder_label = NULL; 

static int safe_x_error_handler(Display *d, XErrorEvent *e) { return 0; }

static guint32 get_active_window_id(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkScreen *screen = gdk_display_get_default_screen(display);
    GdkWindow *root_window = gdk_screen_get_root_window(screen);
    GdkAtom active_atom = gdk_atom_intern("_NET_ACTIVE_WINDOW", FALSE);
    GdkAtom actual_type; gint actual_format, actual_length; guchar *data = NULL;
    guint32 window_id = 0;
    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(safe_x_error_handler);

    if (gdk_property_get(root_window, active_atom, GDK_SELECTION_TYPE_WINDOW, 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &data)) {
        if (data && actual_length >= sizeof(guint32)) window_id = *(guint32 *)data;
        g_free(data);
    }
    XSetErrorHandler(old_handler); 
    return window_id;
}

static void get_window_title(guint32 window_id, char *out_title, size_t max_len) {
    if (window_id == 0) { g_strlcpy(out_title, "Plocha", max_len); return; }
    GdkDisplay *gdk_disp = gdk_display_get_default();
    GdkWindow *gdk_win = gdk_x11_window_lookup_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) gdk_win = gdk_x11_window_foreign_new_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) { g_strlcpy(out_title, "Plocha", max_len); return; }

    GdkAtom net_wm_name = gdk_atom_intern("_NET_WM_NAME", FALSE);
    GdkAtom actual_type; gint actual_format, actual_length; guchar *data = NULL;
    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(safe_x_error_handler);
    gboolean success = FALSE;

    if (gdk_property_get(gdk_win, net_wm_name, gdk_atom_intern("UTF8_STRING", FALSE), 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &data)) {
        if (data && actual_length > 0) { 
            size_t len = (actual_length < max_len - 1) ? actual_length : max_len - 1; 
            memcpy(out_title, data, len); 
            out_title[len] = '\0'; 
            success = TRUE; 
        }
        if (data) g_free(data);
    }
    XSetErrorHandler(old_handler); 
    
    if (!success || strlen(out_title) == 0 || out_title[0] == '\0') { 
        g_strlcpy(out_title, "Plocha", max_len); 
    }
}

void refresh_application_title(void) {
    if (!placeholder_label) return;
    
    char title[512] = {0};
    guint32 active_win = get_active_window_id();
    get_window_title(active_win, title, sizeof(title));
    
    gtk_label_set_text(GTK_LABEL(placeholder_label), title);
}

static GdkFilterReturn x11_window_filter(GdkXEvent *xevent, GdkEvent *event, gpointer user_data) {
    XEvent *x11_event = (XEvent *)xevent;
    if (x11_event->type == PropertyNotify) {
        Atom active_atom = XInternAtom(x11_event->xproperty.display, "_NET_ACTIVE_WINDOW", False);
        Atom wm_name_atom = XInternAtom(x11_event->xproperty.display, "_NET_WM_NAME", False);
        if (x11_event->xproperty.atom == active_atom || x11_event->xproperty.atom == wm_name_atom) {
            refresh_application_title();
        }
    }
    return GDK_FILTER_CONTINUE;
}

void init_menu_system(GtkWidget *container, GtkWidget *placeholder) { 
    // container už technicky nepotrebujeme, stačí nám priamo placeholder_label
    placeholder_label = placeholder; 
}

void setup_dbus_menu(void) {
    GdkDisplay *display = gdk_display_get_default(); 
    GdkScreen *screen = gdk_display_get_default_screen(display); 
    GdkWindow *root_window = gdk_screen_get_root_window(screen);
    
    gdk_window_set_events(root_window, gdk_window_get_events(root_window) | GDK_PROPERTY_CHANGE_MASK); 
    gdk_window_add_filter(root_window, x11_window_filter, NULL);
    
    // Prvé úvodné načítanie názvu
    refresh_application_title();
}

