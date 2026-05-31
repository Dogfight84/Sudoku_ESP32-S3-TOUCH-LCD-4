#include "ui.h"

#include <lvgl.h>
#include <cstdint>
#include <cstdio>

#include "ui_theme.h"
#include "storage.h"
#include "i18n.h"
#include "fonts_extra.h"

namespace ui {

static sudoku::GameSession *S = nullptr;

// forward
static void showSplash();
static void showMenu();
static void showGame();
static void showPause();
static void showWin();
static void showRestoreDialog();
static void flushSave();

// ===================== stato schermata di gioco =====================
static lv_obj_t *g_cell[81];
static lv_obj_t *g_cellLabel[81];
static lv_obj_t *g_numBtn[10];          // tasti tastierino 1..9 (indice = cifra)
static lv_obj_t *g_notesBtn = nullptr;  // toggle modalita' appunti
static bool      g_notesMode = false;   // true = i tasti scrivono appunti
static sudoku::Difficulty g_pendingDiff = sudoku::Difficulty::Medium; // livello scelto in attesa di conferma
static lv_obj_t *g_timerLabel = nullptr;
static lv_timer_t *g_tick = nullptr;
static lv_timer_t *g_flash = nullptr;
static lv_timer_t *g_saveTimer = nullptr;   // debounce dell'autosalvataggio NVS
static bool        g_saveDirty = false;
static uint32_t    g_cellSig[81];           // "firma" per cella: ridisegna solo se cambia
static int8_t      g_numDone[10];           // stato 'cifra completata' per tasto (evita invalidazioni)

static const int CELL = 38;       // lato cella (px)
static const int BLOCKGAP = 4;    // gap extra ai confini 3x3
static int cellX(int c) { return c * CELL + (c / 3) * BLOCKGAP; }
static int cellY(int r) { return r * CELL + (r / 3) * BLOCKGAP; }

static void fmtTime(uint32_t ms, char *buf, size_t n) {
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static const char *difName(sudoku::Difficulty d) {
    switch (d) {
        case sudoku::Difficulty::Easy:   return i18n::tr(i18n::K_EASY);
        case sudoku::Difficulty::Medium: return i18n::tr(i18n::K_MEDIUM);
        case sudoku::Difficulty::Hard:   return i18n::tr(i18n::K_HARD);
    }
    return i18n::tr(i18n::K_MEDIUM);
}

// --- Autosalvataggio "debounced" -------------------------------------------
// La scrittura NVS (flash) disabilita la cache e fa andare in underrun la DMA
// del pannello RGB -> screen drift. Quindi NON salviamo ad ogni mossa: marchiamo
// lo stato "sporco" e scriviamo ~1.5s dopo l'ultima mossa (a tocchi fermi) o
// alle transizioni di schermata (flushSave in killTimers).
static void writeSaveIfDirty() {
    if (g_saveDirty && S &&
        (S->state() == sudoku::GameState::Playing || S->state() == sudoku::GameState::Paused)) {
        storage::saveGame(S->snapshot());
    }
    g_saveDirty = false;
}

static void save_timer_cb(lv_timer_t *t) {
    writeSaveIfDirty();
    g_saveTimer = nullptr;
    lv_timer_del(t);
}

static void scheduleSave() {
    g_saveDirty = true;
    if (g_saveTimer) lv_timer_reset(g_saveTimer);     // rimanda: salva solo a tocchi fermi
    else g_saveTimer = lv_timer_create(save_timer_cb, 1500, nullptr);
}

static void flushSave() {
    if (g_saveTimer) { lv_timer_del(g_saveTimer); g_saveTimer = nullptr; }
    writeSaveIfDirty();
}

static void killTimers() {
    if (g_tick)  { lv_timer_del(g_tick);  g_tick = nullptr; }
    if (g_flash) { lv_timer_del(g_flash); g_flash = nullptr; }
    flushSave();   // persiste un eventuale salvataggio in sospeso prima di cambiare schermata
}

// stile comune: niente bordo/pad/scroll, raggio piccolo
static void plainStyle(lv_obj_t *o, lv_color_t bg, int radius) {
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// Costruisce la stringa 3x3 degli appunti ("1 2 3\n4 5 6\n7 8 9", assente = spazio).
static void buildNotesText(uint16_t mask, char *buf) {
    int p = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int d = r * 3 + c + 1;
            buf[p++] = (mask & (1u << (d - 1))) ? (char)('0' + d) : ' ';
            if (c < 2) buf[p++] = ' ';
        }
        if (r < 2) buf[p++] = '\n';
    }
    buf[p] = 0;
}

// ===================== refresh griglia =====================
static void refreshGame() {
    if (!S) return;
    int sel = S->selectedCell();
    int selR = sel >= 0 ? sel / 9 : -1;
    int selC = sel >= 0 ? sel % 9 : -1;
    uint8_t selVal = sel >= 0 ? S->board().value(sel) : 0;

    char t[2] = {0, 0};
    char notesBuf[20];   // "d d d\nd d d\nd d d\0" = 18 byte (16 era troppo corto -> stack smash)
    for (int i = 0; i < 81; i++) {
        uint8_t v = S->board().value(i);
        uint16_t notes = S->board().notes(i);
        bool given = S->board().isGiven(i);
        int r = i / 9, c = i % 9;

        uint8_t bgstate = 0;  // 0 normale, 1 peer, 2 stesso numero, 3 selezionata
        bool peer = (sel >= 0) &&
                    (r == selR || c == selC ||
                     ((r / 3) == (selR / 3) && (c / 3) == (selC / 3)));
        bool same = (selVal > 0 && v == selVal);
        if (peer) bgstate = 1;
        if (same) bgstate = 2;
        if (i == sel) bgstate = 3;

        // Firma della cella: se invariata, NON tocchiamo nulla (niente invalidazione
        // -> niente ridisegno). Cosi' toccando la stessa cella si ridisegna solo quella.
        uint32_t sig = (uint32_t)bgstate | ((uint32_t)given << 2) |
                       ((uint32_t)v << 3) | ((uint32_t)notes << 8);
        if (sig == g_cellSig[i]) continue;
        g_cellSig[i] = sig;

        if (v) {
            t[0] = (char)('0' + v);
            lv_label_set_text(g_cellLabel[i], t);
            lv_obj_set_style_text_font(g_cellLabel[i], &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(g_cellLabel[i],
                given ? theme::ink() : theme::userNum(), 0);
        } else if (notes) {
            buildNotesText(notes, notesBuf);
            lv_label_set_text(g_cellLabel[i], notesBuf);
            lv_obj_set_style_text_font(g_cellLabel[i], &lv_font_unscii_8, 0);
            lv_obj_set_style_text_line_space(g_cellLabel[i], 2, 0);
            lv_obj_set_style_text_letter_space(g_cellLabel[i], -3, 0);
            lv_obj_set_style_text_align(g_cellLabel[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(g_cellLabel[i], theme::noteInk(), 0);
        } else {
            lv_label_set_text(g_cellLabel[i], "");
        }

        lv_color_t bg = theme::cell();
        if (bgstate == 1) bg = theme::peer();
        else if (bgstate == 2) bg = theme::same();
        else if (bgstate == 3) bg = theme::cellSel();
        lv_obj_set_style_bg_color(g_cell[i], bg, 0);
    }

    // Tasti del tastierino: aggiorna solo quando lo stato "completato" cambia.
    for (int d = 1; d <= 9; d++) {
        if (!g_numBtn[d]) continue;
        int8_t done = S->board().isDigitComplete((uint8_t)d) ? 1 : 0;
        if (done == g_numDone[d]) continue;
        g_numDone[d] = done;
        lv_obj_set_style_opa(g_numBtn[d], done ? LV_OPA_40 : LV_OPA_COVER, 0);
        if (done) lv_obj_clear_flag(g_numBtn[d], LV_OBJ_FLAG_CLICKABLE);
        else      lv_obj_add_flag(g_numBtn[d], LV_OBJ_FLAG_CLICKABLE);
    }
}

// ===================== callbacks =====================
static void tick_cb(lv_timer_t *) {
    if (!g_timerLabel || !S) return;
    char b[8];
    fmtTime(S->elapsedMs(), b, sizeof(b));
    lv_label_set_text(g_timerLabel, b);
}

static void flash_clear_cb(lv_timer_t *t) {
    refreshGame();
    lv_timer_del(t);
    g_flash = nullptr;
}

static void cell_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    S->selectCell(idx);
    refreshGame();
}

static void notes_cb(lv_event_t *) {
    g_notesMode = !g_notesMode;
    if (g_notesBtn)
        lv_obj_set_style_bg_color(g_notesBtn,
            g_notesMode ? theme::accent() : theme::accent2(), 0);
}

static void num_cb(lv_event_t *e) {
    int d = (int)(intptr_t)lv_event_get_user_data(e);   // 0 = cancella

    if (g_notesMode) {
        int sel = S->selectedCell();
        if (sel >= 0) {
            if (d == 0) {
                // backspace in modalita' appunti: svuota gli appunti della cella
                uint16_t m = S->board().notes(sel);
                for (int n = 1; n <= 9; n++)
                    if (m & (1u << (n - 1))) S->toggleNote((uint8_t)n);
            } else {
                S->toggleNote((uint8_t)d);
            }
        }
        refreshGame();
        return;
    }

    S->enterValue((uint8_t)d);
    if (S->state() == sudoku::GameState::Won) { showWin(); return; }
    scheduleSave();   // autosalvataggio "debounced" (no scrittura flash ad ogni click -> no drift)
    refreshGame();
    // griglia piena ma non risolta -> lampeggio rosso sulle celle in conflitto
    if (S->board().isComplete()) {
        bool conf[81];
        S->board().conflicts(conf);
        for (int i = 0; i < 81; i++)
            if (conf[i]) {
                lv_obj_set_style_bg_color(g_cell[i], theme::danger(), 0);
                g_cellSig[i] = 0xFFFFFFFF;   // forza il ripristino al prossimo refreshGame
            }
        if (g_flash) lv_timer_del(g_flash);
        g_flash = lv_timer_create(flash_clear_cb, 1400, nullptr);
    }
}

static void pause_cb(lv_event_t *) {
    S->pause();
    // Non forziamo una scrittura flash ad ogni pausa (pausa-spam -> drift): se ci sono
    // mosse non ancora salvate, le persiste comunque flushSave() in killTimers().
    showPause();
}

static void new_cb(lv_event_t *) { showMenu(); }

static void resume_cb(lv_event_t *) {
    S->resume();
    showGame();
}

static void diff_cb(lv_event_t *e) {
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    // Se c'e' una partita salvata, chiedi se riprenderla o iniziarne una nuova.
    if (storage::hasSavedGame()) {
        g_pendingDiff = (sudoku::Difficulty) d;
        showRestoreDialog();
        return;
    }
    S->newGame((sudoku::Difficulty)d);
    showGame();
}

static void resumeSaved_cb(lv_event_t *) {
    sudoku::GameSession::Snapshot snap;
    if (storage::loadGame(snap) && S->restore(snap)) {
        S->resume();
        showGame();
    }
}

// "Nuova partita" dal dialogo di ripristino: scarta il salvataggio e parte
// dal livello scelto (memorizzato in g_pendingDiff).
static void newPending_cb(lv_event_t *) {
    storage::clearSavedGame();
    S->newGame(g_pendingDiff);
    showGame();
}

static void toMenu_cb(lv_event_t *) { showMenu(); }

// ===================== helper bottoni =====================
static lv_obj_t *makeButton(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                            lv_color_t bg, lv_event_cb_t cb, int udata) {
    lv_obj_t *b = lv_btn_create(parent);
    plainStyle(b, bg, 10);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)udata);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, theme::ink(), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    return b;
}

// ===================== MENU =====================
static void showMenu() {
    killTimers();
    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SUDOKU");
    lv_obj_set_style_text_color(title, theme::ink(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    static const sudoku::Difficulty diffs[3] = {
        sudoku::Difficulty::Easy, sudoku::Difficulty::Medium, sudoku::Difficulty::Hard };
    int y = 130;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = makeButton(scr, difName(diffs[i]), &lv_font_montserrat_28,
                                 theme::accent2(), diff_cb, (int)diffs[i]);
        lv_obj_set_size(b, 300, 56);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
        y += 66;
    }

    // Riprendi (solo se c'e' una partita salvata valida)
    if (storage::hasSavedGame()) {
        lv_obj_t *b = makeButton(scr, i18n::tr(i18n::K_RESUME), &lv_font_montserrat_24,
                                 theme::accent(), resumeSaved_cb, 0);
        lv_obj_set_size(b, 300, 50);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y + 6);
    }

    // Record per livello
    char rec[96];
    char e[8], m[8], h[8];
    uint32_t re = storage::bestTime(sudoku::Difficulty::Easy);
    uint32_t rm = storage::bestTime(sudoku::Difficulty::Medium);
    uint32_t rh = storage::bestTime(sudoku::Difficulty::Hard);
    fmtTime(re * 1000, e, sizeof(e));
    fmtTime(rm * 1000, m, sizeof(m));
    fmtTime(rh * 1000, h, sizeof(h));
    snprintf(rec, sizeof(rec), i18n::tr(i18n::K_RECORDS_FMT),
             re ? e : "--:--", rm ? m : "--:--", rh ? h : "--:--");
    lv_obj_t *recL = lv_label_create(scr);
    lv_label_set_text(recL, rec);
    lv_obj_set_style_text_color(recL, theme::muted(), 0);
    lv_obj_set_style_text_font(recL, &lv_font_montserrat_16, 0);
    lv_obj_align(recL, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 150, 0, true);
}

// ===================== DIALOGO RIPRISTINO =====================
// Mostrato quando si sceglie un livello ma esiste una partita salvata:
// "Riprendi" la riprende, "Nuova partita" la scarta e parte dal livello scelto.
static void showRestoreDialog() {
    killTimers();
    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, i18n::tr(i18n::K_SAVED_FOUND));
    lv_obj_set_style_text_color(t, theme::ink(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *bResume = makeButton(scr, i18n::tr(i18n::K_RESUME), &lv_font_montserrat_28,
                                   theme::accent(), resumeSaved_cb, 0);
    lv_obj_set_size(bResume, 300, 56);
    lv_obj_align(bResume, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *bNew = makeButton(scr, i18n::tr(i18n::K_NEW_GAME), &lv_font_montserrat_28,
                                theme::accent2(), newPending_cb, 0);
    lv_obj_set_size(bNew, 300, 56);
    lv_obj_align(bNew, LV_ALIGN_CENTER, 0, 76);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 120, 0, true);
}

// ===================== GIOCO =====================
static void showGame() {
    killTimers();
    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);

    // --- top bar ---
    lv_obj_t *bar = lv_obj_create(scr);
    plainStyle(bar, theme::panel(), 0);
    lv_obj_set_size(bar, 480, 50);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

    g_timerLabel = lv_label_create(bar);
    lv_label_set_text(g_timerLabel, "00:00");
    lv_obj_set_style_text_color(g_timerLabel, theme::ink(), 0);
    lv_obj_set_style_text_font(g_timerLabel, &lv_font_montserrat_28, 0);
    lv_obj_align(g_timerLabel, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *bNew = makeButton(bar, "MENU", &lv_font_montserrat_18,
                                theme::accent2(), new_cb, 0);
    lv_obj_set_size(bNew, 74, 38);
    lv_obj_align(bNew, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *bPause = makeButton(bar, LV_SYMBOL_PAUSE, &lv_font_montserrat_24,
                                  theme::accent2(), pause_cb, 0);
    lv_obj_set_size(bPause, 46, 38);
    lv_obj_align(bPause, LV_ALIGN_RIGHT_MID, -88, 0);

    // toggle appunti (matita): evidenziato quando attivo
    g_notesMode = false;
    g_notesBtn = makeButton(bar, LV_SYMBOL_EDIT, &lv_font_montserrat_24,
                            theme::accent2(), notes_cb, 0);
    lv_obj_set_size(g_notesBtn, 46, 38);
    lv_obj_align(g_notesBtn, LV_ALIGN_RIGHT_MID, -140, 0);

    // --- griglia ---
    int gw = cellX(8) + CELL;   // larghezza/altezza totale griglia
    lv_obj_t *grid = lv_obj_create(scr);
    plainStyle(grid, theme::thick(), 2);
    lv_obj_set_size(grid, gw, gw);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 56);

    for (int i = 0; i < 81; i++) {
        int r = i / 9, c = i % 9;
        lv_obj_t *cell = lv_obj_create(grid);
        plainStyle(cell, theme::cell(), 2);
        lv_obj_set_size(cell, CELL - 2, CELL - 2);
        lv_obj_set_pos(cell, cellX(c) + 1, cellY(r) + 1);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_cell[i] = cell;

        lv_obj_t *lab = lv_label_create(cell);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_28, 0);
        lv_obj_center(lab);
        g_cellLabel[i] = lab;
    }

    // --- tastierino ---
    int n = 10;            // 1..9 + cancella
    int bw = 44, gap = 4;
    int totalW = n * bw + (n - 1) * gap;
    int startX = (480 - totalW) / 2;
    int padY = 56 + gw + 10;            // sotto la griglia
    for (int i = 0; i < 10; i++) g_numBtn[i] = nullptr;
    for (int k = 0; k < n; k++) {
        char digit[2] = {0, 0};
        const char *txt = "";
        int udata = (k < 9) ? (k + 1) : 0;
        if (k < 9) { digit[0] = (char)('1' + k); txt = digit; }
        lv_obj_t *b = makeButton(scr, txt, &lv_font_montserrat_28,
                                 theme::accent2(), num_cb, udata);
        lv_obj_set_size(b, bw, 58);
        lv_obj_set_pos(b, startX + k * (bw + gap), padY);
        if (k < 9) {
            g_numBtn[k + 1] = b;
        } else {
            // tasto cancella: glifo mdi-eraser (font generato)
            lv_obj_t *lbl = lv_obj_get_child(b, 0);
            lv_label_set_text(lbl, SYM_ERASER);
            lv_obj_set_style_text_font(lbl, &font_eraser, 0);
            lv_obj_set_style_text_color(lbl, theme::ink(), 0);
        }
    }

    // invalida le cache: la griglia e' appena stata ricreata, va disegnata tutta
    for (int i = 0; i < 81; i++) g_cellSig[i] = 0xFFFFFFFF;
    for (int d = 0; d < 10; d++) g_numDone[d] = -1;
    refreshGame();
    g_tick = lv_timer_create(tick_cb, 250, nullptr);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 120, 0, true);
}

// ===================== PAUSA =====================
static void showPause() {
    killTimers();
    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, resume_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, i18n::tr(i18n::K_PAUSED));
    lv_obj_set_style_text_color(t, theme::ink(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_48, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *s = lv_label_create(scr);
    lv_label_set_text(s, i18n::tr(i18n::K_TAP_RESUME));
    lv_obj_set_style_text_color(s, theme::muted(), 0);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_18, 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 40);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 120, 0, true);
}

// ===================== VITTORIA =====================
static void showWin() {
    killTimers();
    uint32_t secs = S->elapsedMs() / 1000;
    sudoku::Difficulty d = S->difficulty();
    uint32_t best = storage::bestTime(d);
    bool record = (best == 0 || secs < best);
    if (record) storage::setBestTime(d, secs);
    storage::clearSavedGame();          // partita finita

    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);

    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, i18n::tr(i18n::K_YOU_WON));
    lv_obj_set_style_text_color(t, theme::good(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_48, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 80);

    char tbuf[8];
    fmtTime(secs * 1000, tbuf, sizeof(tbuf));
    lv_obj_t *time = lv_label_create(scr);
    lv_label_set_text_fmt(time, "%s  -  %s", difName(d), tbuf);
    lv_obj_set_style_text_color(time, theme::ink(), 0);
    lv_obj_set_style_text_font(time, &lv_font_montserrat_28, 0);
    lv_obj_align(time, LV_ALIGN_CENTER, 0, -10);

    if (record) {
        lv_obj_t *rec = lv_label_create(scr);
        lv_label_set_text(rec, i18n::tr(i18n::K_NEW_RECORD));
        lv_obj_set_style_text_color(rec, theme::good(), 0);
        lv_obj_set_style_text_font(rec, &lv_font_montserrat_24, 0);
        lv_obj_align(rec, LV_ALIGN_CENTER, 0, 40);
    }

    lv_obj_t *b = makeButton(scr, i18n::tr(i18n::K_MENU), &lv_font_montserrat_28,
                             theme::accent(), toMenu_cb, 0);
    lv_obj_set_size(b, 220, 56);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 150, 0, true);
}

// ===================== SPLASH / SCELTA LINGUA =====================
static void lang_cb(lv_event_t *e) {
    int l = (int)(intptr_t)lv_event_get_user_data(e);
    i18n::setLang((i18n::Lang) l);
    storage::setLanguage((uint8_t) l);
    showMenu();
}

static void showSplash() {
    killTimers();
    lv_obj_t *scr = lv_obj_create(nullptr);
    plainStyle(scr, theme::bg(), 0);

    // "sol levante": cerchio rosso con i kanji 数独 al centro
    lv_obj_t *sun = lv_obj_create(scr);
    plainStyle(sun, lv_color_hex(0xd83a3a), 0);
    lv_obj_set_size(sun, 180, 180);
    lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *k = lv_label_create(sun);
    lv_label_set_text(k, SYM_KANJI_SUDOKU);     // 数独
    lv_obj_set_style_text_font(k, &font_jp56, 0);
    lv_obj_set_style_text_color(k, theme::ink(), 0);
    lv_obj_center(k);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SUDOKU");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, theme::ink(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 232);

    // scelta lingua
    lv_obj_t *bEn = makeButton(scr, "English", &lv_font_montserrat_28,
                               theme::accent2(), lang_cb, (int) i18n::EN);
    lv_obj_set_size(bEn, 280, 54);
    lv_obj_align(bEn, LV_ALIGN_TOP_MID, 0, 318);

    lv_obj_t *bIt = makeButton(scr, "Italiano", &lv_font_montserrat_28,
                               theme::accent2(), lang_cb, (int) i18n::IT);
    lv_obj_set_size(bIt, 280, 54);
    lv_obj_align(bIt, LV_ALIGN_TOP_MID, 0, 384);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 200, 0, true);
}

// ===================== init =====================
void init(sudoku::GameSession *session) {
    S = session;
    // lingua salvata come default (verra' comunque riscelta nella splash)
    uint8_t saved = storage::language();
    if (saved == 0 || saved == 1) i18n::setLang((i18n::Lang) saved);
    showSplash();
}

} // namespace ui
