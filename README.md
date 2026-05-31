# MiniHotkey

Lightweight Windows system tray app that shows a pill notification whenever you press a hotkey, so you remember what each key does.

<img width="349" alt="image" src="https://raw.githubusercontent.com/dayeggpi/minihotkey/refs/heads/main/mhk.png">


## Features

- Pill overlay in bottom-right corner on hotkey press, auto-dismisses after 2.5s
- `Ctrl+Shift+G` opens a scrollable shortlist of all configured hotkeys
- Configurable via `settings.ini` — no restart needed, use tray → **Reload config**
- Zero dependencies, single executable

## Config

Edit `settings.ini` next to the exe:

```ini
[Page1]
Button1= CTRL+ALT+s = "Save Tcmd settings"
Button2 = CTRL+ALT+d = "Backup tabsess"
Button3 = F23 = "Turn on light"
Button4 = F24 = "Turn off light"

[Page2]
Button1 = F15 = "Feature A"
Button2 = F14 = "Feature B"
Button3 = F22 = "Feature C"
Button4 = F21 = "Feature D"

; Change the shortlist trigger (default: Ctrl+Shift+G)
; Shortlist_Trigger = Ctrl+Alt+H
```

**Modifiers:** `Ctrl`, `Shift`, `Alt`, `Win`  
**Keys:** `A-Z`, `0-9`, `F1-F24`, `Space`, `Enter`, `Tab`, `Esc`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, arrow keys

## Usage

1. Run `minihotkey.exe` — appears in system tray
2. Press any configured hotkey to see its description
3. Press `Ctrl+Shift+G` to browse all shortcuts
4. Right-click tray icon → **Reload config** after editing `settings.ini`

## Build

```bat
build.bat
```

Requires GCC (MinGW).
