# game_girl

GAME GIRL is a custom ESP32-S3 handheld mini-arcade built around a monochrome OLED display, rotary encoder controls, button input, buzzer audio, and battery support. AKA a handheld controlable tamagotchi like digital pet that I made for my niece.

## Features
- Custom PCB designed in KiCad
- ESP32-S3 based handheld platform
- 128x64 I2C OLED display
- Rotary encoder + push switch input
- Main action button
- Active buzzer audio
- Soft sleep mode
- Original mini-games:
  - Dress Up
  - Pocket Pet

## Hardware
- ESP32-S3 module
- 128x64 OLED
- Rotary encoder
- Pushbutton
- Active buzzer with transistor driver
- Single-cell LiPo support

## Firmware
The firmware is written in Arduino/C++ and organized as a menu-driven state machine. The device includes a launcher menu, game states, UI rendering, sound feedback, and long-press sleep behavior for handheld use.

## Engineering Highlights
- Built a complete handheld embedded system from schematic to assembled PCB
- Designed a compact display-based UI for limited-resolution hardware
- Implemented encoder and button-driven navigation across multiple games
- Added battery-friendly soft sleep behavior
- Debugged hardware bring-up issues including buzzer driver behavior and real-world encoder handling

## Challenges and Fixes
One of the main bring-up issues was the buzzer driver stage. The buzzer itself tested correctly, but in-circuit debugging showed the low-side transistor stage was not pulling the buzzer node low enough. After tracing the behavior with meter checks and signal testing, the emitter path was corrected to achieve proper switching. I also iterated on encoder handling to make gameplay and menu control more usable with a mechanical rotary input.

## Status
Working prototype. Core hardware and firmware are functional, with several improvements identified for a future board revision.

DESIGNED & ENGINEERED BY BRANDON SHELLY