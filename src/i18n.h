#ifndef I18N_H
#define I18N_H

void        i18n_init(void);
const char* i18n_translate(const char* key);
void        i18n_set_language(const char* lang_code);
const char* i18n_get_language(void);

#endif
