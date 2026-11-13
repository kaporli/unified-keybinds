// Provided by ModKeybindsMenu.dll, built at startup from the installed mods' input files.

// overridableUI names of the mod actions in Controls > Key Bindings
public static native func ModKeybindsMenu_Names() -> array<CName>

// Key each action has in its mod's input file, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Defaults() -> array<CName>

// Settings group (one per mod section) of each action, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Groups() -> array<CName>

// Key each action starts a session with: RCF's for keys RCF keeps, else the last one saved outside UserSettings.json ("None" if none), same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Saved() -> array<CName>

// Names of the controller entries, shown while a controller is in use
public static native func ModKeybindsMenu_PadNames() -> array<CName>

// Redscript Configuration Framework mod that keeps each action's key, else empty, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_RcfMods() -> array<String>

// Translation keys of the section headers and action labels, same order as ModKeybindsMenu_LabelTexts
public static native func ModKeybindsMenu_LabelKeys() -> array<String>

// Label text for each key in ModKeybindsMenu_LabelKeys
public static native func ModKeybindsMenu_LabelTexts() -> array<String>

// Applies these keys to the actions' input mappings (the game only does that for its own actions)
public static native func ModKeybindsMenu_SetKeys(names: array<CName>, keys: array<CName>) -> Void

// Keys of the key combination a mod action is bound to by default (value is its buttonGroup id), else empty
public static native func ModKeybindsMenu_ComboKeys(value: CName) -> array<CName>

// The entries whose change applies after a restart (a key combination, a key the game dropped while unbound, a shared key of a (Hold) action), when the player wasn't told about them yet; else empty
public static native func ModKeybindsMenu_RestartNotice(names: array<CName>, keys: array<CName>) -> array<CName>

// Controller scheme in use: Legacy, Agile or Alternative
public static native func ModKeybindsMenu_PadScheme() -> CName

// The control scheme changed (Legacy, Agile, Alternative): controller defaults and stored keys follow it. True when it differs from the one in use; the scripts then load its keys
public static native func ModKeybindsMenu_SetScheme(scheme: String) -> Bool


// Starts watching the keyboard and mouse, or the controller (pad: XInput), while an entry waits for a key
public static native func ModKeybindsMenu_Watch(pad: Bool) -> Void

// The two keys pressed together while the entry waited ("IK_Q+IK_E", "IK_Pad_LeftShoulder+IK_Pad_A_CROSS"), or empty; ends the watch
public static native func ModKeybindsMenu_Combo() -> String

// Entries that may share their key with another action: the keyboard and controller entries of the (Hold) bindings
public static native func ModKeybindsMenu_HoldShareNames() -> array<CName>

// For each entry (its settings group path, at its default key), the names of the other entries on
// the same key that react at the same time, comma separated ("" for none)
public static native func ModKeybindsMenu_Conflicts(names: array<CName>, keys: array<CName>, groups: array<CName>, atDefault: array<Bool>) -> array<String>

// True when a control scheme's controller keys (Legacy, Agile, Alternative) differ from its defaults
public static native func ModKeybindsMenu_SchemeEdited(scheme: String) -> Bool

// True when the conflicts (as the popup lists them, after a first letter for the device view: K or P) are not empty and changed since the last popup for that view
public static native func ModKeybindsMenu_ConflictNotice(text: String) -> Bool

// Hold-only actions in use: "action|keyboard|controller|keyboardShared|controllerShared" (1: waits for the hold on
// that device; shared: comma list of the actions on its shared key, - for none)
public static native func ModKeybindsMenu_HoldRules() -> array<String>

// Hold-only actions whose hold doesn't count while a prompt on their key shows
public static native func ModKeybindsMenu_SetHoldBlocked(actions: array<CName>) -> Void

// The device in use changed (true: a controller). Hold-only controller buttons follow it
public static native func ModKeybindsMenu_SetDevice(pad: Bool) -> Void

// False while a session starts or ends: keys set then are applied once it is true again
public static native func ModKeybindsMenu_SetStable(stable: Bool) -> Void

// Short text of a key, keyboard or controller: "LB", "F3", a combination as "LB + A"
public static native func ModKeybindsMenu_KeyText(key: CName) -> String

// True when the entry takes a key combination (a button mapping, not a key Native Settings UI or RCF reads, not a trigger)
public static native func ModKeybindsMenu_TakesCombo(name: CName) -> Bool

// True when the key fits the entry: a key of its device, a trigger for a trigger entry and a button for a
// button entry. A game entry the plugin doesn't list takes keyboard and mouse keys.
public static native func ModKeybindsMenu_Fits(name: CName, key: CName) -> Bool
