#include "i18n.h"
#include "kernel.h"
#include "string.h"

static char current_lang[8] = "en_US";

void i18n_init(void) {
    terminal_printf("[I18N] Localization engine initialized. Language: %s\n", current_lang);
}

void i18n_set_language(const char* lang_code) {
    strncpy(current_lang, lang_code, 7);
    current_lang[7] = '\0';
    terminal_printf("[I18N] Language changed to: %s\n", current_lang);
}

const char* i18n_translate(const char* key) {
    /* Basic mock translation dictionary */
    if (strcmp(current_lang, "es_ES") == 0) {
        if (strcmp(key, "Welcome") == 0) return "Bienvenido";
        if (strcmp(key, "File") == 0) return "Archivo";
    }
    return key; /* Fallback to key itself (assumed English) */
}
