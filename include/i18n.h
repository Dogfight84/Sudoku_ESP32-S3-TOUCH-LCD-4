#pragma once

// Internazionalizzazione minimale: lingua corrente + tabella stringhe EN/IT.
namespace i18n {

enum Lang { EN = 0, IT = 1 };

enum Key {
    K_EASY = 0, K_MEDIUM, K_HARD,
    K_RESUME, K_PAUSED, K_TAP_RESUME,
    K_YOU_WON, K_NEW_RECORD, K_MENU,
    K_RECORDS_FMT,        // printf con 3 %s (record Facile/Medio/Difficile)
    K_CHOOSE_LANG,
    K_SAVED_FOUND, K_NEW_GAME,
    K_AUTOSAVING,
    K_SAVING, K_SAVED,
    K_COUNT
};

void        setLang(Lang l);
Lang        lang();
const char *tr(Key k);

} // namespace i18n
