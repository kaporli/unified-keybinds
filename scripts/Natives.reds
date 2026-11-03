// Provided by ModKeybindsMenu.dll, built at startup from the installed mods' input files.

// overridableUI names of the mod actions in Controls > Key Bindings
public static native func ModKeybindsMenu_Names() -> array<CName>

// Key each action has in its mod's input file, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Defaults() -> array<CName>

// Settings group (one per mod section) of each action, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Groups() -> array<CName>

// Last key saved for each action outside UserSettings.json ("None" if none), same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Saved() -> array<CName>

// Translation keys of the section headers and action labels, same order as ModKeybindsMenu_LabelTexts
public static native func ModKeybindsMenu_LabelKeys() -> array<String>

// Label text for each key in ModKeybindsMenu_LabelKeys
public static native func ModKeybindsMenu_LabelTexts() -> array<String>

// Applies these keys to the actions' input mappings (the game only does that for its own actions)
public static native func ModKeybindsMenu_SetKeys(names: array<CName>, keys: array<CName>) -> Void
