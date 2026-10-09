// MIT License
// Lukas Veselovsky, lukve
// Upravené a optimalizované na základe debugovania

#include "gtk/gtk.h"
#include <time.h>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"

#define WIDTH 1920
#define HEIGHT 24

void init_menu_system(GtkWidget *container, GtkWidget *placeholder);
void setup_dbus_menu(void);

typedef struct
{
    int version;
    const char* backgroundcolor;
    const char* color;
} configuration;

typedef struct {
    GtkWidget *clock;
    GtkWidget *battery;
    GtkWidget *volume;
} PanelLabels;

const char *CSS_TEMPLATE = 
    "#my-panel-window {"
    "   background-color: %s;"
    "   color: %s;"
    "   border-color: black;"
    "   border-top-left-radius: 3px;"
    "   border-top-right-radius: 3px;"
    "   border-bottom: 1px solid rgba(0, 0, 0, 0.15);"
    "   transition: filter 0.2s ease;"
    "}"
    "#my-panel-window menu, #my-panel-window menubar, #my-panel-window menuitem {"
    "   background-color: %s;"
    "   color: %s;"
    "}"
    "#my-panel-window menuitem:hover {"
    "   background-color: grey;"
    "}"
    "#my-panel-window label {"
    "   padding: 0 2px;"
    "}";

static int handler(void* user, const char* section, const char* name,
                   const char* value)
{
    configuration* pconfig = (configuration*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
	if (MATCH("global", "version")) {
        pconfig->version = atoi(value);
    } else 
	if (MATCH("theme", "backgroundcolor")) {
        pconfig->backgroundcolor = strdup(value);
    } else 
	if (MATCH("theme", "color")) {
        pconfig->color = strdup(value);
    } else {
        return 0;
    }
    return 1;
}

static void get_battery_status(char *buffer, size_t max_len) {
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *f_stat = fopen("/sys/class/power_supply/BAT0/status", "r");
    
    if (!f_cap) {
        f_cap = fopen("/sys/class/power_supply/BAT1/capacity", "r");
        f_stat = fopen("/sys/class/power_supply/BAT1/status", "r");
    }

    if (f_cap && f_stat) {
        int capacity = 0;
        char status[32] = {0};
        
        if (fscanf(f_cap, "%d", &capacity) == 1 && fscanf(f_stat, "%31s", status) == 1) {
			const char *icon = "\xf0\x9f\x94\xb4"; // 🔋
            if (g_str_has_prefix(status, "Charg")) {
				icon = "\xe2\x9a\xa1"; // ⚡
            }
            snprintf(buffer, max_len, "%s %d%%", icon, capacity);
        } else {
            snprintf(buffer, max_len, "BAT N/A");
        }
        fclose(f_cap);
        fclose(f_stat);
    } else {
        if (f_cap) fclose(f_cap);
        if (f_stat) fclose(f_stat);
        snprintf(buffer, max_len, "");
    }
}

static void get_volume_status(char *buffer, size_t max_len) {
    gchar *stdout_buf = NULL;
    GError *error = NULL;

    // FIX: Prerobené na asynchrónne bezpečné volanie cez GLib na pozadí, prostredie neseká
    if (g_spawn_command_line_sync("amixer get Master", &stdout_buf, NULL, NULL, &error)) {
        if (stdout_buf) {
            gboolean is_muted = (strstr(stdout_buf, "[off]") != NULL);
            char volume[16] = "N/A";

            char *start = strchr(stdout_buf, '[');
            if (start) {
                char *end = strchr(start, '%');
                if (end) {
                    size_t len = end - (start + 1);
                    if (len < sizeof(volume) - 1) {
                        memcpy(volume, start + 1, len);
                        volume[len] = '%';
                        volume[len + 1] = '\0';
                    }
                }
            }

            if (is_muted) {
                snprintf(buffer, max_len, "\xf0\x9f\x94\xa0 Mute"); // 🔇
            } else {
                snprintf(buffer, max_len, "\xf0\x9f\x94\xa1 %s", volume); // 🔊
            }
            g_free(stdout_buf);
        }
    } else {
        snprintf(buffer, max_len, "🔊 N/A");
        if (error) g_error_free(error);
    }
}

static gboolean update_clock(gpointer user_data) {
    PanelLabels *labels = (PanelLabels *)user_data;

    time_t rawtime;
    struct tm *timeinfo;
    char time_buffer[40]; 

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%a %H:%M:%S", timeinfo);
    gtk_label_set_text(GTK_LABEL(labels->clock), time_buffer);

    char bat_buffer[32];
    get_battery_status(bat_buffer, sizeof(bat_buffer));
    gtk_label_set_text(GTK_LABEL(labels->battery), bat_buffer);

    char vol_buffer[32];
    get_volume_status(vol_buffer, sizeof(vol_buffer));
    gtk_label_set_text(GTK_LABEL(labels->volume), vol_buffer);

    return TRUE; 
}

void enable_alpha_channel(GtkWidget *window) {
    GdkScreen *gdk_screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gdk_screen);
    if (visual != NULL && gdk_screen_is_composited(gdk_screen)) {
        gtk_widget_set_visual(window, visual);
    }
}

char *CSS_STYLE = NULL;

void apply_css_style(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_STYLE, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static void suppress_dbusmenu_warnings(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
    // Pohlcuje interné varovania
}

int main(int argc, char *argv[]) {
    g_setenv("UBUNTU_MENUPROXY", "1", TRUE);
    g_setenv("QT_QPA_PLATFORMTHEME", "appmenu-qt5", TRUE);
    g_setenv("GTK_CSD", "0", TRUE); // Fix klientských dekorácií

    // FIX: Umlčanie interných Gtk chýb z asynchrónneho preberania zbernice menu tretích strán
    g_log_set_handler("LIBDBUSMENU-GLIB", G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL, suppress_dbusmenu_warnings, NULL);
    g_log_set_handler("Gtk", G_LOG_LEVEL_CRITICAL, suppress_dbusmenu_warnings, NULL);

	// Vynútenie skrytia lokálneho menu zápisom do dočasnej GTK3 konfigurácie panelu
	const char *home_dir = getenv("HOME");
	if (home_dir) {
    char gtk3_settings_path[512];
    snprintf(gtk3_settings_path, sizeof(gtk3_settings_path), "%s/.config/gtk-3.0/settings.ini", home_dir);
    
    FILE *settings_file = fopen(gtk3_settings_path, "w");
    if (settings_file) {
        fprintf(settings_file, "[Settings]\n");
        fprintf(settings_file, "gtk-shell-shows-menubar=1\n");
        fprintf(settings_file, "gtk-shell-shows-appmenu=1\n");
        fclose(settings_file);
    }
	}


    // FIX: Automatické vnútenie schovania menubarov do xsettings databázy pri každom starte panelu
    g_spawn_command_line_sync("xfconf-query -c xsettings -p /Gtk/ShellShowsMenubar -n -t bool -s true", NULL, NULL, NULL, NULL);
    g_spawn_command_line_sync("xfconf-query -c xsettings -p /Gtk/ShellShowsAppmenu -n -t bool -s true", NULL, NULL, NULL, NULL);
    g_spawn_command_line_sync("dbus-update-activation-environment --systemd --all", NULL, NULL, NULL, NULL);

    configuration config;
    config.version = 0;
    config.backgroundcolor = NULL;
    config.color = NULL;

    if (ini_parse("panel.ini", handler, &config) < 0) {
        printf("Can't load 'panel.ini'\n");
        return 1;
    }
    printf("Config loaded from 'panel.ini': version=%d, backgroundcolor=%s, color=%s\n",
        config.version, config.backgroundcolor, config.color);

    if (asprintf(&CSS_STYLE, CSS_TEMPLATE, 
             config.backgroundcolor, config.color, 
             config.backgroundcolor, config.color) == -1) {
	CSS_STYLE = NULL; 
    }

    if (config.backgroundcolor)
        free((void*)config.backgroundcolor);
    if (config.color)
        free((void*)config.color);

    gtk_init(&argc, &argv);
 
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    enable_alpha_channel(window);
    gtk_widget_set_name(window, "my-panel-window");
    gtk_window_set_title(GTK_WINDOW(window), "AppMenu Panel");
    
    gtk_window_move(GTK_WINDOW(window), 0, 0);
    apply_css_style();

    gtk_window_set_default_size(GTK_WINDOW(window), WIDTH, HEIGHT);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *menu_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), menu_container, FALSE, FALSE, 12); 

    GtkWidget *placeholder_label = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(menu_container), placeholder_label, FALSE, FALSE, 0);

    GtkWidget *clock_label = gtk_label_new("");
    GtkWidget *bat_label = gtk_label_new("");
    GtkWidget *vol_label = gtk_label_new("");
    
    gtk_box_pack_end(GTK_BOX(main_box), clock_label, FALSE, FALSE, 10); 
    gtk_box_pack_end(GTK_BOX(main_box), bat_label, FALSE, FALSE, 10); 
    gtk_box_pack_end(GTK_BOX(main_box), vol_label, FALSE, FALSE, 10); 

    PanelLabels *status_labels = g_new0(PanelLabels, 1);
    status_labels->clock = clock_label;
    status_labels->battery = bat_label;
    status_labels->volume = vol_label;

    update_clock(status_labels);
    g_timeout_add(1000, update_clock, status_labels);

    init_menu_system(menu_container, placeholder_label);
    setup_dbus_menu();

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    // FIX: Vyčistenie pamäte dynamického reťazca štýlu po vypnutí
    if (CSS_STYLE) free(CSS_STYLE);
    g_free(status_labels);
    return 0;
}

