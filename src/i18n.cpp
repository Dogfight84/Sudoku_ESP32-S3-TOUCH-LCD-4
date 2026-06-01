#include "i18n.h"

namespace i18n {

static Lang g_lang = EN;

void setLang(Lang l) { g_lang = l; }
Lang lang() { return g_lang; }

// [chiave][lingua] : 0 = EN, 1 = IT
static const char *T[K_COUNT][2] = {
    /* K_EASY        */ { "Easy",          "Facile" },
    /* K_MEDIUM      */ { "Medium",        "Medio" },
    /* K_HARD        */ { "Hard",          "Difficile" },
    /* K_RESUME      */ { "Resume game",   "Riprendi partita" },
    /* K_PAUSED      */ { "Paused",        "In pausa" },
    /* K_TAP_RESUME  */ { "tap to resume", "tocca per riprendere" },
    /* K_YOU_WON     */ { "You won!",      "Hai vinto!" },
    /* K_NEW_RECORD  */ { "New record!",   "Nuovo record!" },
    /* K_MENU        */ { "Menu",          "Menu" },
    /* K_RECORDS_FMT */ { "Best  E %s   M %s   H %s", "Record  F %s   M %s   D %s" },
    /* K_CHOOSE_LANG */ { "Choose language", "Scegli la lingua" },
    /* K_SAVED_FOUND */ { "Saved game found", "Partita salvata trovata" },
    /* K_NEW_GAME     */ { "New game",        "Nuova partita" },
    /* K_AUTOSAVING   */ { "Autosaving...",   "Salvataggio in corso..." },
    /* K_SAVING       */ { "Saving...",       "Salvataggio..." },
    /* K_SAVED        */ { "Saved",           "Salvato" },
};

const char *tr(Key k) {
    if (k < 0 || k >= K_COUNT) return "";
    return T[k][(int) g_lang];
}

} // namespace i18n
