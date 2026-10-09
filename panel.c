// MIT License
// Lukas Veselovsky, lukve
//
// Hlavný súbor panela upravený pre zobrazovanie Title aplikácie.
// Hlavný súbor panela s pridaným indikátorom pre Wi-Fi.
//
// Kompletný panel: Sledovanie aplikácie, WiFi, Batéria, Hlasitosť a Hodiny.
//

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <arpa/inet.h>

// Deklarácia funkcií z menu.c
void init_menu_system(GtkWidget *container, GtkWidget *placeholder);
void setup_dbus_menu(void);
void refresh_application_title(void);


// Štruktúra na uloženie načítanej konfigurácie
typedef struct {
    char bg_color[16];
    char text_color[16];
} configuration;

// Handler pre INI parser (číta sekciu [theme])
static int ini_handler_callback(void* user, const char* section, const char* name, const char* value) {
    configuration* config = (configuration*)user;
    if (strcmp(section, "theme") == 0) {
        if (strcmp(name, "backgroundcolor") == 0) {
            strncpy(config->bg_color, value, sizeof(config->bg_color) - 1);
        } else if (strcmp(name, "color") == 0) {
            strncpy(config->text_color, value, sizeof(config->text_color) - 1);
        }
    }
    return 1;
}

// 1. INDIKÁTOR SIETE
static int check_network_status(void) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return 0;
    
    char line[256];
    int net_type = 0; 
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (fgets(line, sizeof(line), fp)) {}
    if (fgets(line, sizeof(line), fp)) {}
    
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        if (sscanf(line, " %31[^:]", iface) == 1) {
            if (strcmp(iface, "lo") == 0) continue;
            
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
            
            if (sock >= 0) {
                if (ioctl(sock, SIOCGIFADDR, &ifr) == 0) {
                    struct ifreq check_ifr;
                    memset(&check_ifr, 0, sizeof(check_ifr));
                    strncpy(check_ifr.ifr_name, iface, IFNAMSIZ - 1);
                    
                    if (ioctl(sock, SIOCGIFFLAGS, &check_ifr) == 0) {
                        short current_flags = check_ifr.ifr_ifru.ifru_flags;
                        if ((current_flags & IFF_UP) && (current_flags & IFF_RUNNING)) {
                            if (strncmp(iface, "wl", 2) == 0) {
                                net_type = 1;
                                break;
                            }
                            else if (strncmp(iface, "eth", 3) == 0 || strncmp(iface, "enp", 3) == 0 || strncmp(iface, "eno", 3) == 0) {
                                net_type = 2;
                            }
                        }
                    }
                }
            }
        }
    }
    if (sock >= 0) close(sock);
    fclose(fp);
    return net_type;
}

static gboolean update_network_ticker(gpointer label) {
    int status = check_network_status();
    if (status == 1) gtk_label_set_text(GTK_LABEL(label), "📶 WiFi");
    else if (status == 2) gtk_label_set_text(GTK_LABEL(label), "🌐 LAN");
    else gtk_label_set_text(GTK_LABEL(label), "❌ No WiFi");
    return TRUE;
}

// 2. INDIKÁTOR: Hlasitosť
static void get_volume_info(int *out_volume, int *out_muted) {
    long volume = 0, min = 0, max = 0;
    int muted = 0;
    snd_mixer_t *handle;
    snd_mixer_elem_t *elem;
    snd_mixer_selem_id_t *sid;
    const char *card = "default";
    const char *selem_name = "Master";

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, card);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    elem = snd_mixer_find_selem(handle, sid);

    if (elem) {
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &volume);
        snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_FRONT_LEFT, &muted);
        if (max - min > 0) *out_volume = (int)((volume * 100) / (max - min));
        else *out_volume = 0;
        *out_muted = !muted;
    } else {
        *out_volume = 0; *out_muted = 1;
    }
    snd_mixer_close(handle);
}

static gboolean update_volume_ticker(gpointer label) {
    int volume = 0, muted = 0;
    char buf[32];
    get_volume_info(&volume, &muted);
    if (muted) snprintf(buf, sizeof(buf), "🔇 Muted");
    else snprintf(buf, sizeof(buf), "🔊 %d%%", volume);
    gtk_label_set_text(GTK_LABEL(label), buf);
    return TRUE;
}

// 3. INDIKÁTOR: Batéria / Napájanie
static gboolean update_battery_ticker(gpointer label) {
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *f_stat = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!f_cap) f_cap = fopen("/sys/class/power_supply/BAT1/capacity", "r");
    if (!f_stat) f_stat = fopen("/sys/class/power_supply/BAT1/status", "r");

    int capacity = 0;
    char status[32] = "Unknown";
    char buf[32];

    if (f_cap) {
        if (fscanf(f_cap, "%d", &capacity) != 1) capacity = 0;
        fclose(f_cap);
    }
    if (f_stat) {
        if (fscanf(f_stat, "%31s", status) != 1) strcpy(status, "Unknown");
        fclose(f_stat);
    }

    const char *icon = "🔋";
    if (strcmp(status, "Charging") == 0) icon = "⚡🔋";
    if (f_cap) snprintf(buf, sizeof(buf), "%s %d%%", icon, capacity);
    else snprintf(buf, sizeof(buf), "🔌 AC");

    gtk_label_set_text(GTK_LABEL(label), buf);
    return TRUE;
}

// 4. INDIKÁTOR: Hodiny
static gboolean update_clock_ticker(gpointer label) {
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%H:%M:%S", timeinfo);
    gtk_label_set_text(GTK_LABEL(label), buffer);
    return TRUE;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Predvolené farby, ak by panel.ini chýbal
    configuration config;
    strcpy(config.bg_color, "#1e1e1e");
    strcpy(config.text_color, "#ffffff");

    // Načítanie panel.ini pomocou inih parsera
    if (ini_parse("panel.ini", ini_handler_callback, &config) < 0) {
        g_warning("Nepodarilo sa načítať 'panel.ini', používam predvolené farby.");
    }

    // Dynamické vygenerovanie CSS reťazca s načítanými farbami
    char dynamic_css[1024];
    snprintf(dynamic_css, sizeof(dynamic_css),
        ".panel-window {"
        "   background-color: %s;" // Farba z INI
        "   border-bottom: 1px solid rgba(255, 255, 255, 0.1);"
        "}"
        ".app-title {"
        "   color: %s;" // Farba textu z INI
        "   font-size: 13px;"
        "   font-weight: bold;"
        "   padding: 0 8px;"
        "}"
        ".indicator-item {"
        "   color: %s;"
        "   font-size: 12px;"
        "   font-weight: 500;"
        "   padding: 2px 8px;"
        "   background-color: rgba(255, 255, 255, 0.07);"
        "   border-radius: 4px;"
        "}"
        ".clock-item {"
        "   color: %s;"
        "   font-weight: bold;"
        "   background-color: rgba(255, 255, 255, 0.15);"
        "}",
        config.bg_color, config.text_color, config.text_color, config.text_color
    );

    // Aplikovanie vygenerovaného CSS
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, dynamic_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);

    // Hlavné okno panela
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "My Minimal Panel");
    gtk_window_set_default_size(GTK_WINDOW(window), 1920, 30);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "panel-window");

    // === KOMPATIBILITA PRE OPENBOX (DOCK TYPE) ===
    // Informuje Openbox, že toto okno je systémový panel (lišta).
    // Zabezpečí, aby okná aplikácií neprekrývali panel a Openbox preň vyhradí miesto.
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DOCK);

    GtkWidget *panel_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), panel_box);

    // ĽAVÁ STRANA: Názov aplikácie
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(left_box, 15); 
    GtkWidget *title_label = gtk_label_new("Plocha");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label), "app-title");

    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 30);
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);

    gtk_box_pack_start(GTK_BOX(left_box), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(panel_box), left_box, FALSE, FALSE, 0);

    // STRED: Spacer
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(panel_box), spacer, TRUE, TRUE, 0);

    // PRAVÁ STRANA: Indikátory
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); 
    gtk_widget_set_margin_end(right_box, 15);

    GtkWidget *net_label = gtk_label_new("📶 WiFi");
    GtkWidget *volume_label = gtk_label_new("🔊 --%");
    GtkWidget *battery_label = gtk_label_new("🔋 --%");
    GtkWidget *clock_label = gtk_label_new("--:--");

    gtk_style_context_add_class(gtk_widget_get_style_context(net_label), "indicator-item");
    gtk_style_context_add_class(gtk_widget_get_style_context(volume_label), "indicator-item");
    gtk_style_context_add_class(gtk_widget_get_style_context(battery_label), "indicator-item");
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_label), "indicator-item");
gtk_style_context_add_class(gtk_widget_get_style_context(clock_label), "clock-item");
gtk_box_pack_start(GTK_BOX(right_box), net_label, FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(right_box), volume_label, FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(right_box), battery_label, FALSE, FALSE, 0);
gtk_box_pack_start(GTK_BOX(right_box), clock_label, FALSE, FALSE, 0);
// Spustenie tickerov
update_clock_ticker(clock_label);
g_timeout_add_seconds(1, update_clock_ticker, clock_label);
update_volume_ticker(volume_label);
g_timeout_add_seconds(1, update_volume_ticker, volume_label);
update_battery_ticker(battery_label);
g_timeout_add_seconds(5, update_battery_ticker, battery_label);
update_network_ticker(net_label);
g_timeout_add_seconds(3, update_network_ticker, net_label);

gtk_box_pack_end(GTK_BOX(panel_box), right_box, FALSE, FALSE, 0);

// Inicializácia sledovania okien
init_menu_system(left_box, title_label);
setup_dbus_menu();

refresh_application_title();

g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
gtk_widget_show_all(window);

gtk_main();
return 0;
}


