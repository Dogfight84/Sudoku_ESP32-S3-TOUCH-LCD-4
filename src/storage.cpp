#include "storage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "config.h"

namespace storage {

using Snapshot = sudoku::GameSession::Snapshot;

static Preferences prefs;

static const char *KEY_GAME       = "game_blob";
static const char *KEY_BEST_EASY  = "best_easy";
static const char *KEY_BEST_MED   = "best_medium";
static const char *KEY_BEST_HARD  = "best_hard";

// Task dedicato per le scritture NVS: NON si scrive flash dal thread di rendering
// (LVGL), perche' la scrittura blocca cache/bus e fa andare in underrun la DMA del
// pannello -> screen drift. Le richieste arrivano via coda; il task scrive in
// background su core 0, a bassa priorita'.
static QueueHandle_t s_saveQueue = nullptr;

static void nvsTask(void *) {
    static Snapshot s;   // 'static': fuori dallo stack del task
    for (;;) {
        if (xQueueReceive(s_saveQueue, &s, portMAX_DELAY) == pdTRUE) {
            prefs.putBytes(KEY_GAME, &s, sizeof(Snapshot));
        }
    }
}

void begin() {
    // RW. Il namespace viene creato al primo accesso.
    prefs.begin(NVS_NAMESPACE, false);
    s_saveQueue = xQueueCreate(2, sizeof(Snapshot));
    // Core 0 (LVGL/Arduino girano su core 1), priorita' bassa (1).
    xTaskCreatePinnedToCore(nvsTask, "nvsTask", 4096, nullptr, 1, nullptr, 0);
}

void saveGameAsync(const Snapshot &s) {
    if (!s_saveQueue) { saveGame(s); return; }
    // Non bloccante: se la coda e' piena, salta (il prossimo autosave coprira').
    xQueueSend(s_saveQueue, &s, 0);
}

// --- Validazione del payload letto da NVS ---------------------------------
// Snapshot::valid e' solo un flag del produttore: un blob corrotto potrebbe
// averlo a true. Validiamo qui il contenuto prima di fidarci.
static bool isValidSnapshot(const Snapshot &s) {
    if (!s.valid) return false;
    if (s.difficulty > (uint8_t) sudoku::Difficulty::Hard) return false;
    for (int i = 0; i < sudoku::CELLS; i++) {
        if (s.solution[i] < 1 || s.solution[i] > 9) return false;  // soluzione piena
        if (s.value[i] > 9) return false;                          // 0 = vuota
        if (s.given[i] && s.value[i] != s.solution[i]) return false; // given coerenti
    }
    return true;
}

bool hasSavedGame() {
    Snapshot tmp;
    return loadGame(tmp);
}

void saveGame(const Snapshot &s) {
    prefs.putBytes(KEY_GAME, &s, sizeof(Snapshot));
}

bool loadGame(Snapshot &out) {
    if (!prefs.isKey(KEY_GAME)) return false;
    if (prefs.getBytesLength(KEY_GAME) != sizeof(Snapshot)) return false;
    size_t n = prefs.getBytes(KEY_GAME, &out, sizeof(Snapshot));
    if (n != sizeof(Snapshot)) return false;
    return isValidSnapshot(out);
}

void clearSavedGame() {
    if (prefs.isKey(KEY_GAME)) prefs.remove(KEY_GAME);
}

// --- Record ----------------------------------------------------------------
static const char *bestKey(sudoku::Difficulty d) {
    switch (d) {
        case sudoku::Difficulty::Easy:   return KEY_BEST_EASY;
        case sudoku::Difficulty::Medium: return KEY_BEST_MED;
        case sudoku::Difficulty::Hard:   return KEY_BEST_HARD;
    }
    return KEY_BEST_MED;
}

uint32_t bestTime(sudoku::Difficulty d) {
    return prefs.getUInt(bestKey(d), 0);
}

void setBestTime(sudoku::Difficulty d, uint32_t seconds) {
    prefs.putUInt(bestKey(d), seconds);
}

// --- Lingua -----------------------------------------------------------------
static const char *KEY_LANG = "lang";

uint8_t language() {
    return prefs.getUChar(KEY_LANG, 0xFF);
}

void setLanguage(uint8_t lang) {
    prefs.putUChar(KEY_LANG, lang);
}

} // namespace storage
