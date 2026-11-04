# Changelog

## 1.3.0 (2026-11-05)

- Lists the keys of mods that keep them in Redscript Configuration Framework (RCF), such as the DigitalVixen mods (Shattered Chrome, Chrome Plating, Injuries and others), with the mod's own names, one section per mod. A key changed in Key Bindings goes to RCF and its hub; a key changed in RCF's hub shows up in Key Bindings within 2 seconds. Keys appear from the second launch after the mod is installed. Gamepad keys are not supported yet and stay in RCF's hub.
- RCF's hub key (F8) is kept in sync with its Options page the same way.
- Fix: with this mod installed, DigitalVixen hotkeys set in RCF's hub did nothing, and Key Bindings listed every DigitalVixen key, of installed and missing mods alike, with no key set. Keys that a mod leaves empty for another mod to fill in are no longer listed unless RCF has them.

## 1.2.0 (2026-11-04)

- Lists the keys that Cyber Engine Tweaks mods add through Native Settings UI, one section per mod, from the second launch with them installed. A key changed in Key Bindings goes to the mod, which saves and uses it right away; a key changed in Native Settings UI, Unified Mod Settings or by the mod itself shows up in Key Bindings. Needs Cyber Engine Tweaks and Native Settings UI; the archive now includes a small CET script.
- Fix: Defaults in Key Bindings left every mod entry empty. It now sets them to the mod's own key.

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
