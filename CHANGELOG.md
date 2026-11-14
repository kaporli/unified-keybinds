# Changelog

## 2.0.1 (2026-11-14)

- Fix: Sprint, Scan and other (Hold) actions reacted late on the keyboard, or not to a tap, when a mod or an Input Loader edit used the same key (since 2.0.0). The (Hold) action waited for a long press, and the mod's action lost its own hold. On the keyboard only the game's own key binding options share a key again, as in 1.8.2.

## 2.0.0 (2026-11-14)

One download, keyboard and controller on equal terms, and fewer restarts.

Updating from 1.x: the optional file Vanilla Controller Rebinding is part of the main file now. Remove it before you install 2.0.0 (in Mod Organizer 2 or Vortex, uninstall it), or its older script replaces the new one. Your bindings carry over.

Keyboard
- Vanilla keyboard keys take combinations too, like mod keys and controller buttons (the game's own entries take a single key). Your keys carry over, and a single key is kept in the game's settings, so removing the mod keeps them.
- A shared keyboard key of a (Hold) action works as soon as you bind it, like a shared controller button. Before, it needed a restart.

Controller
- Vanilla controller rebinding (Classic, Dynamic, Alternative, with Sprint (Hold), Crouch (Hold) and Scan (Toggle)) is part of the main file.
- A control scheme change applies at once: the controller entries, the Classic dodge and the Dynamic crouch follow the new scheme. Before, it needed a restart and showed a popup.
- Controller entries for mod actions that only had a keyboard entry: a mod mapping with a key and a controller button but no rebind name (for example Chipware Expansion's Crack Chip and Renew Chip, Enhanced Craft's Next, Disassemble As Looting Choice), or with one rebind name on both. Key and button rebind separately.

Conflicts
- Shared bindings: a key used by actions that are never active at the same time (Jump and a map's Track Waypoint on Space) is no longer reported as a conflict.
- The side panel lists real conflicts under Conflicts with (red) and shared keys under Shared with, each action with its section. The popup (Conflicting keys) lists only conflicts.

Fewer restarts
- No restart for a key that takes away another action's hold (for example Crouch (Hold) on the button of Call Vehicle) or for a control scheme change. Key combinations still apply after a restart.

Fixes
- Unbinding both an action's key and its controller button: the game dropped one of them until it restarted.
- A joystick key (`IK_Joy`) was taken by a keyboard entry and refused by a controller entry.

Under the hood
- Keyboard and controller go through the same code for recording combinations, key names, which keys fit an entry, sharing and conflicts.

## 1.8.2 (2026-11-13)

- Fix: heavy stutter and input delay while the game switched between controller and keyboard and mouse many times a second, for example with a gyro mapped to the mouse through Steam Input (since 1.8.0). A memory check ran on every switch; it now runs once.

## 1.8.1 (2026-11-11)

- Fix: the scripts did not compile without Mod Settings installed. Mod Settings is optional again, as the requirements say.
- Fix: a damaged `keys.json` could replace the good backup. The backup is now only taken from a file that can be read, and the file is never read while it is being written.
- Fix: after a game crash during a settings save, the next launch could use the wrong control scheme for the controller entries.
- Fix: the restart popup could name entries that had not changed.
- Fix: a second popup could close one that was still open.
- A quick tap of Sprint (Toggle) is recognised more reliably after long sessions.
- A button shared during play also counts for the prompt priority at once.
- Optional file: on Classic, the mod's double tap only takes over when Dodge/Dash is moved off its default button, and it uses the game's timing (each press up to 0.2 s). The default button is left to the game.
- Optional file: the quick scan on Alternative no longer fires during a breach protocol.
- The side panel's conflict line is worked out only when a key changed, and less work is done on every key change.

## 1.8.0 (2026-11-11)

- Conflict warning: when two actions that react at the same time share a key or button, a popup lists the keys and the side panel of each entry names what shares it, in red. The game only checks its own keyboard keys.
- A shared controller button counts at once: bind Sprint (Hold), Crouch (Hold) or Scan (Toggle) next to another action and tap/hold works without a restart. Before, it needed a full game restart (reloading a save was not enough), which looked like it worked only sometimes. Shared keyboard keys and combinations still apply after a restart, and the popup now names the entries that need it.
- A prompt on screen comes first: while the game shows an interaction on a shared key (talk, loot, pick up), holding the key does the interaction and the (Hold) action waits.
- Sprint (Toggle) can share a button with a (Hold) action: tap sprints, hold does the other.
- Optional file: controller keys are kept per control scheme (Classic, Dynamic, Alternative), so a layout made on one scheme no longer changes another. The scheme selector marks an edited scheme with a star.
- Optional file: Scan (Toggle) gets a controller entry, unbound by default. With Scan (Hold) on the same button, tap is a quick scan and hold switches scanning on or off, on every scheme.
- Optional file: on Classic, Dodge/Dash moved to another button dodges again (double tap, as the scheme defines it). Before, it never dodged.
- Optional file: on Dynamic, Crouch (Toggle) on a button of its own toggles on a tap. On the dodge's button it stays a hold, as in the game.
- Optional file: the Control Scheme screen also shows Sprint (Hold), Crouch (Hold) and Scan (Toggle) once they have a button, with the hold icon on a shared one.
- Safer storage: files are written through a temporary file, the previous keys are kept as `keys.json.bak`, and a damaged `keys.json` is set aside as `keys.json.bad` instead of being replaced by an empty one.
- Faster: less work on every file the game opens and closes, and the conflict check reads the input contexts once.

## 1.7.0 (2026-11-10)

- Controller combinations: on a controller entry, hold one button and press another, for example LB + A. A button pressed on its own is bound alone. Trigger entries take a single trigger.
- Keyboard combinations take any two keys, mouse buttons included (before: Shift, Ctrl or Alt and a key).
- An entry waiting for a key now says "Press a key or combination" ("button" on a controller), and the side panel says how to bind one. Vanilla entries keep the game's text.
- A (Hold) action can share its key with another action: tap for the other action, hold for this one. For example Sprint (Hold) and Dodge on B, or Crouch (Hold) and Dodge/Dash on Mouse 4. The hold only counts on the device where the key is shared, so the other device stays as it was. A shared key applies after a restart.
- An action that shares a key with a (Hold) action stops reacting to holding that key on that device, so the two don't both start (it still reacts to a tap).
- Optional file: Sprint (Hold) and Crouch (Hold) get controller entries, unbound by default, like the keyboard has.

## 1.6.0 (2026-11-09)

- Unbinding: point at an entry in Key Bindings and press X (Square on a controller) to unbind it. Works for vanilla and mod actions, keyboard and controller entries; the hints show the key. The game itself has no way to clear a key.
- With both an action's key and its controller button unbound, the game drops one of the two until it restarts, so binding that one again applies after a restart. A popup says so.
- The menu keeps its changes with actions left unbound. Allow Unbound Actions is no longer needed, and still works alongside.
- Fix: an unbound mod key went back to the mod's default when a save loaded, and an unbound controller entry kept its button. Unbound entries now stay unbound across save loads, new games and restarts, also with a Mod Settings option. Defaults brings the keys back, and an entry you never changed still gets the mod's key.
- Redscript Configuration Framework's hub key (F8) can be unbound too. RCF reads an empty hub key as F8, so an unbound one is kept as a key no keyboard sends.
- Fix: starting a new game could still crash the game when the world loaded. Keys are now held while a session starts or ends and applied once the game's key bindings are loaded, on the game's main thread.
- Optional file: the Control Scheme screen leaves out unbound actions.

## 1.5.1 (2026-11-06)

- Fix: starting a new game crashed the game a few seconds in. The game clears its key bindings for a moment when a new game starts, and the keys were applied right then. They are now applied once the bindings are loaded again. Loading a save was not affected.

## 1.5.0 (2026-11-05)

- Vanilla controller rebinding moved to an optional file, Vanilla Controller Rebinding. The main file keeps keyboard keys and the controller buttons of mod actions. Install the optional file as well to keep rebinding vanilla controller buttons.
- Optional file: Settings > Controls > Control Scheme draws your controller bindings instead of the defaults, for Xbox and PlayStation controllers.
- Optional file: entries follow the control scheme (Classic, Dynamic, Alternative). Crouch, Dodge and Quick Melee could be rebound before but kept their default button. After a scheme change, a popup says the entries follow it from the next launch.
- Optional file: a button an action uses in several places (on foot, in a vehicle) gets one entry per place, and buttons used only inside menus are no longer listed. Fix: Move Backward and Crouch (Hold) only changed minigame buttons.
- A controller entry takes only a button of its own kind: a trigger for a trigger action, a button for a button action. Mixing them made the game drop the button until a restart.
- Fix: a saved controller button equal to the default no longer overrides a different default later, for example after a control scheme change.

## 1.4.0 (2026-11-05)

- Renamed to Unified Keybinds (was Native Mod Keybinds). Files and folders keep their names, so updating works as before.
- Controller rebinding: use a controller in Key Bindings and the menu shows controller buttons, for vanilla actions and mod actions, and switches back when you use the keyboard. An action is listed when exactly one controller button triggers it.
- Redscript Configuration Framework mods (the DigitalVixen mods) now list their gamepad keys too, kept in sync with RCF's hub.

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
