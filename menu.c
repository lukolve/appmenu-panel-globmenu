// MIT License
// Lukas Veselovsky, lukve
// Created with magician help of the AI.

#include "gtk/gtk.h"
#include <gdk/gdkx.h>
#include <gio/gio.h> 
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>

static GtkWidget *menu_container = NULL;
static GtkWidget *current_menu = NULL;
static GtkWidget *placeholder_label = NULL; 
static GDBusConnection *dbus_connection = NULL;

void update_menu_for_window(guint32 window_id);
void update_global_menu(const char *bus_name, const char *object_path, guint32 window_id);

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
    XSetErrorHandler(old_handler); return window_id;
}

static void get_window_title(guint32 window_id, char *out_title, size_t max_len) {
    if (window_id == 0) { g_strlcpy(out_title, "⌘", max_len); return; }
    GdkDisplay *gdk_disp = gdk_display_get_default();
    GdkWindow *gdk_win = gdk_x11_window_lookup_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) gdk_win = gdk_x11_window_foreign_new_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) { g_strlcpy(out_title, "⌘", max_len); return; }

    GdkAtom net_wm_name = gdk_atom_intern("_NET_WM_NAME", FALSE);
    GdkAtom actual_type; gint actual_format, actual_length; guchar *data = NULL;
    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(safe_x_error_handler);
    gboolean success = FALSE;

    if (gdk_property_get(gdk_win, net_wm_name, gdk_atom_intern("UTF8_STRING", FALSE), 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &data)) {
        if (data && actual_length > 0) { size_t len = (actual_length < max_len - 1) ? actual_length : max_len - 1; memcpy(out_title, data, len); out_title[len] = '\0'; success = TRUE; }
        if (data) g_free(data);
    }
    XSetErrorHandler(old_handler); 
    
    // BEZPEČNOSTNÁ POISTKA: Ak získanie zlyhalo, vlož symbol ⌘
    if (!success || strlen(out_title) == 0 || out_title[0] == '\0') { 
        g_strlcpy(out_title, "⌘", max_len); 
    }
}

static gboolean get_menu_paths_from_window_properties(guint32 window_id, char *out_bus, char *out_path) {
    if (window_id == 0) return FALSE;
    GdkDisplay *gdk_disp = gdk_display_get_default();
    GdkWindow *gdk_win = gdk_x11_window_lookup_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) gdk_win = gdk_x11_window_foreign_new_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) return FALSE;

    GdkAtom bus_atom = gdk_atom_intern("_GTK_UNIQUE_BUS_NAME", FALSE);
    GdkAtom path_atom = gdk_atom_intern("_GTK_MENUBAR_OBJECT_PATH", FALSE);
    GdkAtom actual_type; gint actual_format, actual_length; guchar *bus_data = NULL; guchar *path_data = NULL; gboolean success = FALSE;
    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(safe_x_error_handler);

    if (gdk_property_get(gdk_win, bus_atom, gdk_atom_intern("UTF8_STRING", FALSE), 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &bus_data)) {
        if (bus_data && actual_length > 0) {
            memcpy(out_bus, bus_data, actual_length); out_bus[actual_length] = '\0';
            if (gdk_property_get(gdk_win, path_atom, gdk_atom_intern("UTF8_STRING", FALSE), 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &path_data)) {
                if (path_data && actual_length > 0) { memcpy(out_path, path_data, actual_length); out_path[actual_length] = '\0'; success = TRUE; }
            }
        }
    }
    if (bus_data) g_free(bus_data); if (path_data) g_free(path_data);
    XSetErrorHandler(old_handler); return success;
}

static gboolean get_action_path_from_window(guint32 window_id, const char *property_atom_name, char *out_path, size_t max_len) {
    if (window_id == 0) return FALSE;
    GdkDisplay *gdk_disp = gdk_display_get_default();
    GdkWindow *gdk_win = gdk_x11_window_lookup_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) gdk_win = gdk_x11_window_foreign_new_for_display(gdk_disp, (Window)window_id);
    if (!gdk_win) return FALSE;

    GdkAtom atom = gdk_atom_intern(property_atom_name, FALSE);
    GdkAtom actual_type; gint actual_format, actual_length; guchar *data = NULL; gboolean success = FALSE;
    int (*old_handler)(Display *, XErrorEvent *) = XSetErrorHandler(safe_x_error_handler);

    if (gdk_property_get(gdk_win, atom, gdk_atom_intern("UTF8_STRING", FALSE), 0, 1024, FALSE, &actual_type, &actual_format, &actual_length, &data)) {
        if (data && actual_length > 0) {
            size_t len = (actual_length < max_len - 1) ? actual_length : max_len - 1;
            memcpy(out_path, data, len); out_path[len] = '\0'; success = TRUE;
        }
    }
    if (data) g_free(data);
    XSetErrorHandler(old_handler); return success;
}
void update_menu_for_window(guint32 window_id) {
    if (!menu_container) return;
    static char last_bus[256] = ""; static char last_path[256] = ""; static guint32 last_window_id = 0;
    char direct_bus[256] = {0}; char direct_path[256] = {0};

    if (get_menu_paths_from_window_properties(window_id, direct_bus, direct_path)) {
        if (window_id == last_window_id && strcmp(direct_bus, last_bus) == 0 && strcmp(direct_path, last_path) == 0) return;
        last_window_id = window_id; g_strlcpy(last_bus, direct_bus, sizeof(last_bus)); g_strlcpy(last_path, direct_path, sizeof(last_path));
        update_global_menu(direct_bus, direct_path, window_id); return;
    }
    if (dbus_connection && window_id != 0) {
        if (window_id == last_window_id && strlen(last_bus) == 0) return;
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            dbus_connection, "com.canonical.AppMenu.Registrar", "/com/canonical/AppMenu/Registrar",
            "com.canonical.AppMenu.Registrar", "GetMenuForWindow", g_variant_new("(u)", window_id),
            G_VARIANT_TYPE("(so)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        if (result) {
            const gchar *bus_name = NULL; const gchar *menu_path = NULL; g_variant_get(result, "(&s&o)", &bus_name, &menu_path);
            if (window_id == last_window_id && strcmp(bus_name, last_bus) == 0 && strcmp(menu_path, last_path) == 0) { g_variant_unref(result); return; }
            last_window_id = window_id; g_strlcpy(last_bus, bus_name, sizeof(last_bus)); g_strlcpy(last_path, menu_path, sizeof(last_path));
            update_global_menu(bus_name, menu_path, window_id); g_variant_unref(result); return;
        } else { g_clear_error(&error); }
    }
    last_window_id = window_id; last_bus[0] = '\0'; last_path[0] = '\0'; update_global_menu(NULL, NULL, window_id);
}

void update_global_menu(const char *bus_name, const char *object_path, guint32 window_id) {
    if (current_menu) { gtk_widget_destroy(current_menu); current_menu = NULL; }
    if (placeholder_label) gtk_widget_hide(placeholder_label);

    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); 
    current_menu = header_box;

    if (bus_name && object_path && strlen(bus_name) > 0 && strlen(object_path) > 0 && dbus_connection) {
        GDBusMenuModel *model = g_dbus_menu_model_get(dbus_connection, bus_name, object_path);
        if (model) {
            GtkWidget *menu_bar = gtk_menu_bar_new_from_model(G_MENU_MODEL(model));
            
            char app_path[256] = {0};
            char win_path[256] = {0};

            if (get_action_path_from_window(window_id, "_GTK_APPLICATION_OBJECT_PATH", app_path, sizeof(app_path))) {
                GDBusActionGroup *app_actions = g_dbus_action_group_get(dbus_connection, bus_name, app_path);
                if (app_actions) { 
                    gtk_widget_insert_action_group(GTK_WIDGET(menu_bar), "app", G_ACTION_GROUP(app_actions)); 
                    g_object_unref(app_actions); 
                }
            } else {
                GDBusActionGroup *app_actions = g_dbus_action_group_get(dbus_connection, bus_name, object_path);
                if (app_actions) { 
                    gtk_widget_insert_action_group(GTK_WIDGET(menu_bar), "app", G_ACTION_GROUP(app_actions)); 
                    g_object_unref(app_actions); // FIX: Pridané uvoľnenie pamäte pre else vetvu
                }
            }

            if (get_action_path_from_window(window_id, "_GTK_WINDOW_OBJECT_PATH", win_path, sizeof(win_path))) {
                GDBusActionGroup *win_actions = g_dbus_action_group_get(dbus_connection, bus_name, win_path);
                if (win_actions) { 
                    gtk_widget_insert_action_group(GTK_WIDGET(menu_bar), "win", G_ACTION_GROUP(win_actions)); 
                    g_object_unref(win_actions); // FIX: Pridané uvoľnenie pamäte pre okná
                }
            }

            gtk_box_pack_start(GTK_BOX(header_box), menu_bar, FALSE, FALSE, 0);
            g_object_unref(model);
        }
    } else {
        if (placeholder_label) {
            char title[256] = {0};
            get_window_title(window_id, title, sizeof(title));
            gtk_label_set_text(GTK_LABEL(placeholder_label), title);
            gtk_widget_show(placeholder_label);
        }
    }
    if (current_menu) { gtk_box_pack_start(GTK_BOX(menu_container), current_menu, FALSE, FALSE, 0); gtk_widget_show_all(current_menu); }
}

static GdkFilterReturn x11_window_filter(GdkXEvent *xevent, GdkEvent *event, gpointer user_data) {
    XEvent *x11_event = (XEvent *)xevent;
    if (x11_event->type == PropertyNotify) {
        Atom active_atom = XInternAtom(x11_event->xproperty.display, "_NET_ACTIVE_WINDOW", False);
        Atom wm_name_atom = XInternAtom(x11_event->xproperty.display, "_NET_WM_NAME", False);
        if (x11_event->xproperty.atom == active_atom || x11_event->xproperty.atom == wm_name_atom) {
            update_menu_for_window(get_active_window_id());
        }
    }
    return GDK_FILTER_CONTINUE;
}

void init_menu_system(GtkWidget *container, GtkWidget *placeholder) { menu_container = container; placeholder_label = placeholder; }
void setup_dbus_menu(void) {
    GError *error = NULL; dbus_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error); if (error) g_clear_error(&error);
    GdkDisplay *display = gdk_display_get_default(); GdkScreen *screen = gdk_display_get_default_screen(display); GdkWindow *root_window = gdk_screen_get_root_window(screen);
    gdk_window_set_events(root_window, gdk_window_get_events(root_window) | GDK_PROPERTY_CHANGE_MASK); gdk_window_add_filter(root_window, x11_window_filter, NULL);
    update_menu_for_window(get_active_window_id());
}

