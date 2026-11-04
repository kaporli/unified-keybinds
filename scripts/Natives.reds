// Provided by ModKeybindsMenu.dll, built at startup from the installed mods' input files.

// overridableUI names of the mod actions in Controls > Key Bindings
public static native func ModKeybindsMenu_Names() -> array<CName>

// Key each action has in its mod's input file, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Defaults() -> array<CName>

// Settings group (one per mod section) of each action, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Groups() -> array<CName>

// Key each action starts a session with: RCF's for keys RCF keeps, else the last one saved outside UserSettings.json ("None" if none), same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Saved() -> array<CName>

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

// True when these keys change a key combination compared to startup (applies after a restart) and the player wasn't told yet
public static native func ModKeybindsMenu_RestartNotice(names: array<CName>, keys: array<CName>) -> Bool

// Starts watching the keyboard while an entry waits for a key
public static native func ModKeybindsMenu_WatchModifier() -> Void

// The modifier key (Shift, Ctrl, Alt) held when the bound key went down, or None; ends the watch
public static native func ModKeybindsMenu_HeldModifier() -> CName
