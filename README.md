# Mod Keybinds Menu

Every mod's keybinds in the game's own Key Bindings menu.

Mods that add keys usually leave them in an input file, in Mod Settings, or both, and many can't be changed in game at all. Mod Keybinds Menu adds a **Mods** group to Settings > Controls > Key Bindings that lists every keyboard action your installed mods add, so you can rebind them next to the vanilla keys. Nothing is hardcoded: the list is built from your installed mods each time the game starts.

## Features

- A **Mods** group in Settings > Controls > Key Bindings, built at startup from the mods you have installed. Add or remove a mod and the list follows.
- Entries are labeled by mod and action, for example *"Let There Be Flight: Vehicle Flight Linear Brake"* or *"Stash QoL: Transfer Max Quantity"*.
- Keys stay in sync with Mod Settings: if a mod also has its key in Mod Settings, changing it in either menu changes both.
- Actions already in the Key Bindings menu (vanilla, or added by mods like No More Hard-coded Keybinds) are not listed twice.
- Never changes the game's files or other mods' files, so mods that edit the settings menu keep working.

## Requirements

- Cyberpunk 2077 **2.5**
- [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380)
- [redscript](https://www.nexusmods.com/cyberpunk2077/mods/1511)
- [Input Loader](https://www.nexusmods.com/cyberpunk2077/mods/4575)
- [Mod Settings](https://www.nexusmods.com/cyberpunk2077/mods/4885) (optional, for keeping keys in sync with mods that also offer them there)

## Installation

Extract the archive into the Cyberpunk 2077 game folder, or install it with Mod Organizer 2 or Vortex. The files go to:

```
red4ext/plugins/ModKeybindsMenu/ModKeybindsMenu.dll
red4ext/plugins/ModKeybindsMenu/scripts/
```

## Uninstallation

Remove the `red4ext/plugins/ModKeybindsMenu` folder. Optionally delete `r6/cache/modkeybinds/`. Nothing is written to your save.

## Known limitations

- Only mods that bind keys through the game's input system show up. Hotkeys registered with Cyber Engine Tweaks stay in the CET overlay.
- Gamepad-only actions are not listed: the Key Bindings menu is for keyboard and mouse.
- A mod that registers its input file late in startup through Input Loader's plugin interface may only appear from the second launch.
- Unbinding an entry that has no Mod Settings option resets it to the mod's default key the next time a save loads.

## For mod authors

Your action shows up if it has a `<button id="IK_..." overridableUI="yourActionName" />` in an input file loaded by Input Loader. Nothing else is needed.

For a nicer label than the generated one, ship `r6/config/modkeybinds/<YourMod>.json`:

```json
{
  "yourActionName": "Your Mod: Open Menu"
}
```

Labels can also be `LocKey#...` keys.

## How it works

- A RED4ext plugin compares Input Loader's merged `r6/cache/inputUserMappings.xml` with the game's `r6/config/inputUserMappings.xml`. Every `overridableUI` action a mod added becomes an entry.
- When the game reads `r6/config/settings/platform/pc/options.json`, the plugin hands it a copy with the Mods group added, written to `r6/cache/modkeybinds/options.json`. The original file stays untouched.
- Labels come from the mod's folder or file name and the action name. A prefix that repeats the mod's name or initials is dropped.
- A redscript system copies Mod Settings key options into the menu when a save loads, since Mod Settings applies its keys at startup, and copies menu changes back to Mod Settings.
- What was found is written to `red4ext/logs/modkeybindsmenu-*.log`.

## Building

Needs Visual Studio 2022 Build Tools (C++ workload), CMake and Ninja.

```
git clone --recursive https://github.com/kaporli/mod-keybinds-menu
tools\package.ps1
```

The plugin is built to `package/` and the release zip to `dist/`.

## License

MIT, see [LICENSE](LICENSE). Uses [RED4ext.SDK](https://github.com/wopss/RED4ext.SDK), [pugixml](https://github.com/zeux/pugixml) and [nlohmann/json](https://github.com/nlohmann/json), all MIT.
