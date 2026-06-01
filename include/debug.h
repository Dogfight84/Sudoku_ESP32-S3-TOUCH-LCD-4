#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include <stdarg.h>
#include "esp_heap_caps.h"

// Logging diagnostico: messaggio + snapshot memoria (heap interno, PSRAM, LVGL).
// Diagnostica disattivata in release. Per riattivarla: -DSUDOKU_DEBUG=1 nei build_flags
// (logga heartbeat memoria ogni 5s + ogni interazione, utile per il debug del drift/leak).
#ifndef SUDOKU_DEBUG
#define SUDOKU_DEBUG 0
#endif

#if SUDOKU_DEBUG
static inline void sudoku_dbg(const char *fmt, ...) {
    char buf[40];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    lv_mem_monitor_t m; lv_mem_monitor(&m);
    Serial.printf("[%8lu] %-16s | heap=%u min=%u big=%u | psram=%u pbig=%u | lv=%u frag=%u%%\n",
                  (unsigned long)millis(), buf,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                  (unsigned)m.free_size, (unsigned)m.frag_pct);
}
#define DBG(...) sudoku_dbg(__VA_ARGS__)
#else
#define DBG(...) do {} while (0)
#endif
