# Native Mod Keybinds

Every mod's keybinds in the game's own Key Bindings menu.

Rebind any mod key, even ones the mod never made rebindable, and set key combinations, all next to the vanilla keys.

Mods that add keys usually leave them in an input file, in Mod Settings, or both, and many can't be changed in game at all. Native Mod Keybinds adds a section per mod to Settings > Controls > Key Bindings with every keyboard action your installed mods add, so you can rebind them next to the vanilla keys. Nothing is hardcoded: the list is built from your installed mods each time the game starts.

## Limitations

> [!WARNING]
> - Keyboard and mouse only: controller keys are left out.
> - Cyber Engine Tweaks hotkeys are not supported. They stay in the CET overlay.
> - Native Settings UI keys appear from the second launch after you install the mod, and take a single key only (no combinations).
> - A new key combination, or going back to one, applies after a restart. A popup tells you. Single keys apply right away.
> - Combinations are modifier + key only. In game, press the key within half a second of the modifier.
> - A combination stays in this menu only: Mod Settings keeps its own single key.
> - The game crashes with more than about 30 key combinations in total (its own, other mods' and yours). Past that, an action keeps its key and the log says so.
> - A mod that registers its input file late through Input Loader's plugin interface may only appear from the second launch.
> - Unbinding an entry without a Mod Settings option resets it to the mod's default key when a save loads.

## Supported keybind sources

| Where the mod keeps its keys | Supported | Notes |
| --- | --- | --- |
| Input Loader input file (`overridableUI`) | Yes | Most mods, for example Let There Be Flight |
| Input Loader input file, single key or key combination without `overridableUI` | Yes | For example Enhanced Vehicle System |
| Mod Settings key option | Yes | Kept in sync both ways |
| Redscript Configuration Framework | Yes | Its keys are in Input Loader files and Mod Settings |
| Native Settings UI | Yes | From the second launch, single keys |
| Unified Mod Settings | Yes | Edits Native Settings UI and Mod Settings keys, which show up here |
| Cyber Engine Tweaks hotkeys (`registerHotkey`, `registerInput`) | No | Stay in the CET overlay |

## Screenshots

Each mod gets its own section below the vanilla keys:

![Key Bindings menu with sections for Auto Drive Enhanced, Custom Map Markers, Dvrcf, Fpp Speedometer and Immersive Timeskip](screenshots/key-bindings-mod-sections.jpg)

![The Let There Be Flight section with its flight controls](screenshots/key-bindings-let-there-be-flight.jpg)

![The Enhanced Vehicle System section with the key combinations Ctrl + K and F1 + 1](screenshots/key-bindings-key-combinations.jpg)

## Features

- One section per mod in Settings > Controls > Key Bindings, for example *"Mod: Let There Be Flight"* or *"Mod: Stash QoL"*, built at startup from the mods you have installed. Add or remove a mod and the sections follow.
- Section and action names come from the mod's files, for example *"Vehicle Flight Linear Brake"* or *"Transfer Max Quantity"*.
- Also lists keys the mod didn't mark as rebindable (for example Enhanced Vehicle System's F1 to F4 and T).
- Key combinations are shown as their keys (for example *"F2 + 2"*) and can be rebound to a single key. Defaults puts the combination back from the next launch.
- Record your own combination: hold a modifier (Shift, Ctrl or Alt) and press a key, for example *"Ctrl + K"*. A modifier pressed on its own is bound alone.
- A changed key applies right away, also for mods without a Mod Settings option.
- Keys stay in sync with Mod Settings: if a mod also has its key in Mod Settings, changing it in either menu changes both.
- Also lists the keys that Cyber Engine Tweaks mods add through Native Settings UI (for example Kiroshi Optics Night Vision or Much Better Netrunning). A key changed in either menu changes both, and the mod uses it right away.
- Actions already in the Key Bindings menu (vanilla, or added by mods like No More Hard-coded Keybinds) are not listed twice.
- Never changes the game's files or other mods' files, so mods that edit the settings menu keep working.

## Requirements

- Cyberpunk 2077 **2.5**
- [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380)
- [redscript](https://www.nexusmods.com/cyberpunk2077/mods/1511)
- [Input Loader](https://www.nexusmods.com/cyberpunk2077/mods/4575)
- [Codeware](https://www.nexusmods.com/cyberpunk2077/mods/7780)
- [Mod Settings](https://www.nexusmods.com/cyberpunk2077/mods/4885) (optional, for keeping keys in sync with mods that also offer them there)
- [Cyber Engine Tweaks](https://www.nexusmods.com/cyberpunk2077/mods/107) and [Native Settings UI](https://www.nexusmods.com/cyberpunk2077/mods/3518) (optional, for the keys of mods that use Native Settings UI)

## Installation

Extract the archive into the Cyberpunk 2077 game folder, or install it with Mod Organizer 2 or Vortex. The files go to:

```
red4ext/plugins/ModKeybindsMenu/ModKeybindsMenu.dll
red4ext/plugins/ModKeybindsMenu/scripts/
bin/x64/plugins/cyber_engine_tweaks/mods/ModKeybindsMenu/init.lua
```

## Uninstallation

Remove the `red4ext/plugins/ModKeybindsMenu` and `bin/x64/plugins/cyber_engine_tweaks/mods/ModKeybindsMenu` folders. Optionally delete `r6/cache/modkeybinds/`, where the mod keys are kept. Nothing is written to your save.

Updating from 1.0.0: start the game once with 1.0.1 before uninstalling, so your `UserSettings.json` gets cleaned up.

## For mod authors

Your action shows up if it has a `<button id="IK_..." overridableUI="yourActionName" />` in an input file loaded by Input Loader. A `Button` mapping with a single keyboard or mouse key or key combination (`buttonGroup`) and no `overridableUI` shows up too, under the mapping's name. Nothing else is needed.

For nicer names than the generated ones, ship `r6/config/modkeybinds/<YourMod>.json`:

```json
{
  "_section": "Your Mod",
  "yourActionName": "Open Menu"
}
```

`_section` names the section for every action in that file. Names can also be `LocKey#...` keys.

## How it works

- A RED4ext plugin compares Input Loader's merged `r6/cache/inputUserMappings.xml` with the game's `r6/config/inputUserMappings.xml`. Every `overridableUI` action a mod added becomes an entry, and so does every mod `Button` mapping with a single key and no `overridableUI`.
- The game reads a copy of the merged mappings with `overridableUI` added to those keys (`r6/cache/modkeybinds/inputUserMappings.xml`).
- A key combination's entry holds its `buttonGroup` id, or `IK_LControl+IK_K` for one picked in the menu; the scripts draw it as its keys. At startup a picked combination is written into the action's own mapping in that copy, as a `buttonGroup`, since the game only watches combinations that an action uses when it loads.
- The menu binds a key only once every key is released, so while an entry waits for a key the plugin checks the keyboard every 10 ms to see whether a modifier was held when the other key went down.
- When the game reads `r6/config/settings/platform/pc/options.json`, the plugin hands it a copy with one group per mod added, written to `r6/cache/modkeybinds/options.json`. The original file stays untouched.
- Each section is named after the mod's folder or input file name; each action after its `overridableUI` name, minus a prefix that repeats the mod's name or initials.
- The game resets every setting if `UserSettings.json` holds a group the menu doesn't define, which would happen after removing a mod with keys. So the mod sections never go into that file: the game reads a copy with them added (`r6/cache/modkeybinds/UserSettings.read.json`), and what it writes is split again, mod keys into `r6/cache/modkeybinds/keys.json`, everything else into `UserSettings.json`.
- A redscript system copies Mod Settings key options into the menu when a save loads, since Mod Settings applies its keys at startup, and copies menu changes back to Mod Settings.
- The game only applies key binding settings to its own actions. The plugin hooks the game's ApplyOverrides and sets the menu's key on every mapping that carries a listed action, at startup and when a key changes, the way Mod Settings does for its own options.
- The menu shows labels through the game's translation system, so `options.json` holds keys like `ModKeybindsMenu-itsHotkey` and the scripts register the label text for every language through Codeware.
- A small CET script (`bin/x64/plugins/cyber_engine_tweaks/mods/ModKeybindsMenu/init.lua`) reads the key bindings in Native Settings UI and hands them to the plugin, which lists them from the next launch (`r6/cache/modkeybinds/nativesettings.json`). A key changed in Key Bindings goes to the mod through Native Settings UI when changes are applied or the settings menu closes; a key changed in Native Settings UI, Unified Mod Settings or by the mod itself goes to Key Bindings within a second.
- Defaults resets an entry to the key the game knows for it, which it only knows for its own actions, so the scripts set mod entries to the mod's key.
- What was found is written to `red4ext/logs/modkeybindsmenu-*.log`.

## Building

Needs Visual Studio 2022 Build Tools (C++ workload), CMake and Ninja.

```
git clone --recursive https://github.com/kaporli/native-mod-keybinds
tools\package.ps1
```

The plugin is built to `package/` and the release zip to `dist/`.

## License

MIT, see [LICENSE](LICENSE). Uses [RED4ext.SDK](https://github.com/wopss/RED4ext.SDK), [pugixml](https://github.com/zeux/pugixml) and [nlohmann/json](https://github.com/nlohmann/json), all MIT. The input manager layouts and address hashes come from [Mod Settings](https://github.com/jackhumbert/mod_settings) (MIT).
