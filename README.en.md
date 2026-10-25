# Cardputer Chess

**[🇧🇷 Português](README.md) | [🇺🇸 English](README.en.md)**

A complete chess game for the **M5Stack Cardputer (original, not ADV)**, with its own engine, opening book and 5 difficulty levels.

![platform](https://img.shields.io/badge/platform-M5Stack%20Cardputer-informational)
![license](https://img.shields.io/badge/license-MIT-green)

## Features

- **Bilingual**: Portuguese and English, selectable from the main menu ("Lang" option).
- **Two modes**: play against the bot or with another person on the same device (hotseat).
- **5 bot levels** (Beginner to Master): opening book, alpha-beta search with a transposition table, evaluation with pawn structure, king safety and endgame awareness, and "human slips" at the lower levels (the bot doesn't always play perfectly).
- **Text input**: type moves in algebraic notation (`e4`, `Nf3`, `exd5`, `O-O`, `e8=Q`) or in coordinates (`e2e4`, `e7e8q`).
- **Full castling, en passant and promotion**, including check and checkmate detection.
- **Quick commands**: undo move, flip board, ask the bot for a hint, toggle sound.

## Requirements

- **Hardware**: Cardputer (not officially tested on the ADV, but should work).

### To compile from the .ino
- **Arduino IDE** with M5Stack board support installed.
- **`M5Cardputer` library** (installs `M5Unified` and `M5GFX` along with it).
- Board selected: `M5Stack > M5Cardputer`.

## Installation

### Option 1 — Pre-built firmware
Download the `.bin` from this repository's **[Releases](../../releases)** tab and flash it to your Cardputer:

- Using the [Bruce web flasher](https://bruce.computer/flasher) or any `esptool-js`/`esptool.py`-based flasher.
- Enter flashing mode: hold **G0**, press and release **Reset**, release G0.
- Flash the `.bin` at address `0x0`.

### Option 2 — Compile it yourself
1. Open `cardputer_xadrez.ino` in the Arduino IDE.
2. Select the `M5Stack > M5Cardputer` board.
3. Compile and upload (`Sketch > Upload`).

## How to play

In the main menu, use:

| Key | Action |
|---|---|
| `;` | up |
| `.` | down |
| `,` | left |
| `/` | right |
| `Enter` | confirm |

During a game, type your move and press **Enter**. Special commands (type and press Enter):

| Command | Shortcut | Action |
|---|---|---|
| `menu` | `m` | back to menu |
| `undo` | `u` | undo move |
| `flip` | `f` | flip board |
| `hint` | `h` | bot suggestion |
| `snd` | — | toggle sound |

## Bot levels

| Level | Depth | Opening book | Notes |
|---|---|---|---|
| 1 — Beginner | 1 ply | — | plays mostly at random |
| 2 — Easy | 2 plies | 6 plies | makes frequent mistakes |
| 3 — Medium | 3 plies | 12 plies | plays reasonably |
| 4 — Hard | 5 plies | 20 plies | few mistakes |
| 5 — Master | 8 plies | 24 plies | plays strong |

## License

Distributed under the MIT license — see [LICENSE](LICENSE).

## Contributing

Suggestions, issues and pull requests are welcome.
