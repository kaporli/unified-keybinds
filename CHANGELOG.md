# Changelog

## 1.1.1 (2026-11-04)

- Key combinations are listed too, shown as their keys (for example Enhanced Vehicle System's F2 + 2). They can be rebound to a single key, and Defaults puts the combination back from the next launch.
- Record your own combination: hold a modifier (Shift, Ctrl or Alt) and press a key, for example Ctrl + K. A modifier pressed on its own is bound alone. A new combination applies from the next launch, and a popup says so.
- Fix: a key combination on an action that also has a Mod Settings option no longer reverts at startup.

## 1.1.0 (2026-11-03)

- Renamed to Native Mod Keybinds (was Mod Keybinds Menu). Files and folders keep their names, so updating works as before.
- Lists keys that mods didn't mark as rebindable: any mod `Button` mapping with a single keyboard or mouse key, such as Enhanced Vehicle System's. Key combinations stay out.
- Fix: a key changed in the menu now applies to the game, including the key shown in the mod's on-screen hints. Before, only keys that also have a Mod Settings option changed.
- Fix: keys saved in `r6/cache/modkeybinds/keys.json` were not read back, so keys without a Mod Settings option went back to their default at the next launch.

## 1.0.1 (2026-11-03)

- Fix: removing a mod that has keys, or removing Mod Keybinds Menu, made the game reset all settings ("User settings data appear to be corrupted"). The mod sections are no longer stored in `UserSettings.json`; their keys are kept in `r6/cache/modkeybinds/keys.json`.
- A `UserSettings.json` from 1.0.0 is cleaned up at the first launch with 1.0.1. On 1.0.0: update and start the game once before uninstalling.

## 1.0.0 (2026-11-03)

- First release for Cyberpunk 2077 2.5.
- Adds a section per mod to Controls > Key Bindings with every keyboard action that installed mods add through Input Loader.
- Keeps those keys in sync with Mod Settings key options.
- Labels are registered through Codeware's localization system.
- Optional name files for mod authors: `r6/config/modkeybinds/*.json`, with `_section` for the section name.
