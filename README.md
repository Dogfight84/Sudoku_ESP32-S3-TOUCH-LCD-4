# Sudoku Panel

[English](README.en.md) · **Italiano**

Gioco del **Sudoku** per il pannello touch **Waveshare ESP32-S3-Touch-LCD-4** (480×480).
Schemi casuali generati **on-device** a soluzione unica, cronometro con pausa, salvataggio
automatico della partita, appunti a matita e tempi record per livello — il tutto in un
firmware standalone, senza WiFi né dipendenze cloud.

## Schermate

<table>
  <tr>
    <td align="center"><img src="docs/images/it/splash.svg" width="230" alt="Splash"><br><sub><b>Splash</b> — intro kanji e scelta lingua (EN/IT)</sub></td>
    <td align="center"><img src="docs/images/it/menu.svg" width="230" alt="Menu"><br><sub><b>Menu</b> — livello, riprendi, record</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/it/game.svg" width="230" alt="Gioco"><br><sub><b>Gioco</b> — griglia 9×9, tastierino, appunti, cronometro</sub></td>
    <td align="center"><img src="docs/images/it/pause.svg" width="230" alt="Pausa"><br><sub><b>Pausa</b> — griglia oscurata, tempo fermo</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/images/it/win.svg" width="230" alt="Vittoria"><br><sub><b>Vittoria</b> — tempo finale e record</sub></td>
    <td></td>
  </tr>
</table>

<sub>Mockup rappresentativi dell'interfaccia (tema scuro, layout C★). Vedi
<a href="#rigenerare-le-schermate">Rigenerare le schermate</a>.</sub>

---

## Caratteristiche

- 🎲 **Schemi infiniti on-device** — generati a runtime con **soluzione unica garantita** (backtracking + verifica di unicità). Nessun puzzle pre-caricato, nessuna ripetizione.
- 🎚️ **Tre livelli** — Facile / Medio / Difficile (calibrati sul numero di indizi).
- ✏️ **Appunti a matita** — un interruttore nella barra in alto attiva la modalità appunti; tocca `1`–`9` per aggiungere/rimuovere piccoli candidati nella cella selezionata, come nelle vere app di Sudoku.
- 🔢 **Tastierino intelligente** — quando una cifra è piazzata correttamente in tutte e nove le celle, il suo tasto si spegne e smette di rispondere.
- ⏱️ **Cronometro** — conta solo il tempo di gioco; si ferma in pausa e alla vittoria.
- ⏸️ **Pausa** — oscura la griglia e ferma il tempo ("tocca per riprendere"), così non si può sbirciare a cronometro fermo.
- 💾 **Salvataggio automatico e ripresa** — la partita viene salvata in NVS dopo ogni mossa, così uno spegnimento imprevisto non fa perdere i progressi; al riavvio, scegliendo un livello puoi **riprendere** la partita salvata o iniziarne una **nuova**.
- 🏆 **Tempi record** — miglior tempo memorizzato per ogni livello.
- ↩️ **Annulla** ed **evidenziazione** della cella selezionata (riga / colonna / blocco) e dei numeri uguali, con colori distinti.
- 🌐 **Interfaccia bilingue (EN/IT)** — lingua scelta nella schermata splash e salvata in NVS.
- 🌙 **Tema scuro**, layout ottimizzato per il touch capacitivo (griglia massimizzata + tastierino fisso).

## Hardware

| Componente | Dettaglio |
|---|---|
| Board | Waveshare **ESP32-S3-Touch-LCD-4** (ESP32-S3 N16R8) |
| Display | 480×480 IPS, controller **ST7701** (bus RGB) |
| Touch | **GT911** capacitivo (I²C) |
| IO expander | **TCA9554** (reset LCD/touch, backlight) |
| Memoria | 16 MB Flash QIO · 8 MB PSRAM OPI |

## Architettura

Separazione netta tra **logica pura** (C++ portabile, testata su PC) e **firmware**
(Arduino/LVGL):

```
lib/sudoku/            motore + sessione (C++ puro, NIENTE Arduino/LVGL)
  sudoku_solver        risolutore backtracking + conteggio soluzioni (unicità)
  sudoku_generator     generazione griglia piena + scavo a soluzione unica
  sudoku_board         stato griglia: dati iniziali, valori, appunti, undo, conflitti, vittoria
  game_session         macchina a stati + cronometro + input + snapshot save/restore
src/
  main.cpp             bring-up hardware (IO expander, backlight, LVGL) + loop
  ui.cpp               UI LVGL: splash, menu, gioco, pausa, vittoria
  storage.cpp          persistenza NVS (partita + record + lingua), con validazione del payload
  i18n.cpp             tabella stringhe EN/IT minimale
  lvgl_port_v8.cpp     porting LVGL ↔ display RGB (da Espressif/Waveshare)
  font_eraser.c        font LVGL generato: glifo mdi-eraser
  font_jp56.c          font LVGL generato: kanji 数独 (splash)
include/               header (config, tema, i18n, font, board ESP_Panel, lv_conf)
test/                  test unitari Unity del motore e della sessione (ambiente nativo)
```

La logica di gioco non dipende dall'hardware: gira sia sul PC (per i test) sia sul device.

## Build & flash

Richiede [PlatformIO](https://platformio.org/). La build del device usa il fork
**pioarduino** (Arduino-ESP32 3.0.7 / ESP-IDF v5.1), richiesto da `ESP32_Display_Panel`.

```bash
# Compila il firmware
pio run -e esp32-s3-touch-lcd-4

# Compila e flasha sul pannello (USB-C)
pio run -e esp32-s3-touch-lcd-4 -t upload

# Monitor seriale
pio device monitor -b 115200
```

> La prima build scarica la toolchain (~200 MB) e può richiedere parecchi minuti;
> le successive sono nell'ordine dei ~2 minuti.

## Test (su PC, senza hardware)

Il motore e la sessione sono coperti da test unitari **Unity** eseguibili nativamente:

```bash
pio test -e native
```

Serve un compilatore C++ host nel PATH (es. MinGW-w64 / `g++`).

## Comandi

| Azione | Tocco |
|---|---|
| Scegliere il livello | bottoni nel menu |
| Selezionare una cella | tocca la cella |
| Inserire un numero | tasti `1`–`9` del tastierino |
| Cancellare | tasto gomma (⌫) |
| Attivare gli appunti | bottone matita nella barra in alto |
| Pausa | ⏸ nella barra in alto |
| Tornare al menu | **MENU** nella barra in alto |
| Riprendere dopo la pausa | tocca lo schermo |

## Struttura del progetto

- `docs/design/` — documento di design
- `docs/plans/` — piani di implementazione (motore, sessione, firmware)
- `docs/mockups/` — mockup HTML dei layout esplorati in fase di design (layout scelto: **C★**)
- `docs/images/` — mockup SVG delle schermate (vedi [Schermate](#schermate)), generati da `scripts/gen_mockups.py`
- `scripts/gen_mockups.py` — rigenera i mockup delle schermate (vedi sotto)
- `scripts/gen_fonts.md` — come vengono prodotti i font LVGL generati (`font_eraser`, `font_jp56`) con `lv_font_conv`
- `scripts/sermon.py` — cattura "bounded" del monitor seriale (utile per debug)

## Rigenerare le schermate

Le schermate in [Schermate](#schermate) sono **mockup vettoriali**, non catture dal
device. Sono prodotte da un piccolo script Python che rispecchia la palette reale
([`include/ui_theme.h`](include/ui_theme.h)) e il layout ([`src/ui.cpp`](src/ui.cpp)):

```bash
python scripts/gen_mockups.py
```

Lo script riscrive entrambe le lingue: `docs/images/it/{splash,menu,game,pause,win}.svg`
e `docs/images/en/...`. Le stringhe per lingua sono nella tabella `STR` in cima allo
script (da tenere allineata a [`src/i18n.cpp`](src/i18n.cpp)); quando la UI cambia (colori,
layout, etichette) aggiorna le costanti di palette/layout/stringhe e rilancialo. (Per
catture reali dal device, fai uno screenshot dal pannello e metti i PNG in
`docs/images/it/` e `docs/images/en/`, aggiornando i link qui sopra.)

## Licenza

Codice del progetto rilasciato sotto licenza **MIT** — vedi [LICENSE](LICENSE).

Il layer di porting del display (`src/lvgl_port_v8.cpp`, `include/lvgl_port_v8.h`,
`include/ESP_Panel_*.h`) deriva dagli esempi Espressif/Waveshare ed è soggetto alle
rispettive licenze (CC0-1.0 / Apache-2.0) indicate nelle intestazioni dei file.
[LVGL](https://lvgl.io) è distribuito sotto licenza MIT. I font generati derivano da
Material Design Icons (Apache-2.0) e da un font giapponese (es. Noto Sans JP, SIL OFL).
