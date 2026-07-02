#include "i18n.h"
#include "kernel.h"
#include "string.h"

static char current_lang[8] = "en_US";

typedef struct { const char* key; const char* value; } kv_t;

/* ── French ─────────────────────────────────────────────────────────── */
static const kv_t fr[] = {
    {"Welcome",        "Bienvenue"},
    {"File",           "Fichier"},
    {"Edit",           "Modifier"},
    {"View",           "Afficher"},
    {"Help",           "Aide"},
    {"Open",           "Ouvrir"},
    {"Save",           "Enregistrer"},
    {"Close",          "Fermer"},
    {"New",            "Nouveau"},
    {"Delete",         "Supprimer"},
    {"Copy",           "Copier"},
    {"Paste",          "Coller"},
    {"Cut",            "Couper"},
    {"Settings",       "Paramètres"},
    {"Search",         "Rechercher"},
    {"Cancel",         "Annuler"},
    {"OK",             "OK"},
    {"Yes",            "Oui"},
    {"No",             "Non"},
    {"Exit",           "Quitter"},
    {"Network",        "Réseau"},
    {"Sound",          "Son"},
    {"Display",        "Affichage"},
    {"Accounts",       "Comptes"},
    {"Privacy",        "Confidentialité"},
    {"Power",          "Alimentation"},
    {"Theme",          "Thème"},
    {"Install",        "Installer"},
    {"Update",         "Mettre à jour"},
    {"Calendar",       "Calendrier"},
    {NULL, NULL}
};

/* ── German ──────────────────────────────────────────────────────────── */
static const kv_t de[] = {
    {"Welcome",        "Willkommen"},
    {"File",           "Datei"},
    {"Edit",           "Bearbeiten"},
    {"View",           "Ansicht"},
    {"Help",           "Hilfe"},
    {"Open",           "Öffnen"},
    {"Save",           "Speichern"},
    {"Close",          "Schließen"},
    {"New",            "Neu"},
    {"Delete",         "Löschen"},
    {"Copy",           "Kopieren"},
    {"Paste",          "Einfügen"},
    {"Cut",            "Ausschneiden"},
    {"Settings",       "Einstellungen"},
    {"Search",         "Suchen"},
    {"Cancel",         "Abbrechen"},
    {"OK",             "OK"},
    {"Yes",            "Ja"},
    {"No",             "Nein"},
    {"Exit",           "Beenden"},
    {"Network",        "Netzwerk"},
    {"Sound",          "Ton"},
    {"Display",        "Anzeige"},
    {"Accounts",       "Konten"},
    {"Privacy",        "Datenschutz"},
    {"Power",          "Energie"},
    {"Theme",          "Design"},
    {"Install",        "Installieren"},
    {"Update",         "Aktualisieren"},
    {"Calendar",       "Kalender"},
    {NULL, NULL}
};

/* ── Spanish ─────────────────────────────────────────────────────────── */
static const kv_t es[] = {
    {"Welcome",        "Bienvenido"},
    {"File",           "Archivo"},
    {"Edit",           "Editar"},
    {"View",           "Ver"},
    {"Help",           "Ayuda"},
    {"Open",           "Abrir"},
    {"Save",           "Guardar"},
    {"Close",          "Cerrar"},
    {"New",            "Nuevo"},
    {"Delete",         "Eliminar"},
    {"Copy",           "Copiar"},
    {"Paste",          "Pegar"},
    {"Cut",            "Cortar"},
    {"Settings",       "Configuración"},
    {"Search",         "Buscar"},
    {"Cancel",         "Cancelar"},
    {"OK",             "Aceptar"},
    {"Yes",            "Sí"},
    {"No",             "No"},
    {"Exit",           "Salir"},
    {"Network",        "Red"},
    {"Sound",          "Sonido"},
    {"Display",        "Pantalla"},
    {"Accounts",       "Cuentas"},
    {"Privacy",        "Privacidad"},
    {"Power",          "Energía"},
    {"Theme",          "Tema"},
    {"Install",        "Instalar"},
    {"Update",         "Actualizar"},
    {"Calendar",       "Calendario"},
    {NULL, NULL}
};

/* ── Arabic (Latin-safe transliteration shown — real Arabic needs RTL) ── */
static const kv_t ar[] = {
    {"Welcome",        "Ahlan"},
    {"File",           "Malaf"},
    {"Edit",           "Tahrir"},
    {"View",           "Ard"},
    {"Help",           "Mosaada"},
    {"Open",           "Fateh"},
    {"Save",           "Hifz"},
    {"Close",          "Eglaq"},
    {"New",            "Jadid"},
    {"Delete",         "Hadhf"},
    {"Copy",           "Naskh"},
    {"Paste",          "Lasq"},
    {"Cut",            "Qass"},
    {"Settings",       "Eddadat"},
    {"Search",         "Bahth"},
    {"Cancel",         "Elgha"},
    {"OK",             "Muwafiq"},
    {"Yes",            "Na'am"},
    {"No",             "La"},
    {"Exit",           "Khorooj"},
    {"Network",        "Shabaka"},
    {"Sound",          "Sawt"},
    {"Display",        "Shashe"},
    {"Accounts",       "Hisabat"},
    {"Privacy",        "Khososia"},
    {"Power",          "Taqa"},
    {"Theme",          "Mawdoo"},
    {"Install",        "Tathbeet"},
    {"Update",         "Tahdith"},
    {"Calendar",       "Taqweem"},
    {NULL, NULL}
};

static const kv_t* get_table(void) {
    if (strcmp(current_lang, "fr_FR") == 0 || strcmp(current_lang, "fr") == 0) return fr;
    if (strcmp(current_lang, "de_DE") == 0 || strcmp(current_lang, "de") == 0) return de;
    if (strcmp(current_lang, "es_ES") == 0 || strcmp(current_lang, "es") == 0) return es;
    if (strcmp(current_lang, "ar_SA") == 0 || strcmp(current_lang, "ar") == 0) return ar;
    return NULL;
}

void i18n_init(void) {
    terminal_printf("[I18N] Localization ready. Languages: en, fr, de, es, ar. Current: %s\n",
                    current_lang);
}

void i18n_set_language(const char* lang_code) {
    strncpy(current_lang, lang_code, 7);
    current_lang[7] = '\0';
    terminal_printf("[I18N] Language → %s\n", current_lang);
}

const char* i18n_get_language(void) {
    return current_lang;
}

const char* i18n_translate(const char* key) {
    const kv_t* tbl = get_table();
    if (!tbl) return key;
    for (int i = 0; tbl[i].key; i++)
        if (strcmp(tbl[i].key, key) == 0) return tbl[i].value;
    return key;
}
