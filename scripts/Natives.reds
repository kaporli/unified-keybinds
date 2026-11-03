// Provided by ModKeybindsMenu.dll, built at startup from the installed mods' input files.

// overridableUI names of the mod actions in Controls > Key Bindings
public static native func ModKeybindsMenu_Names() -> array<CName>

// Key each action has in its mod's input file, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Defaults() -> array<CName>

// Settings group (one per mod section) of each action, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Groups() -> array<CName>

// Translation keys of the section headers and action labels, same order as ModKeybindsMenu_LabelTexts
public static native func ModKeybindsMenu_LabelKeys() -> array<String>

// Label text for each key in ModKeybindsMenu_LabelKeys
public static native func ModKeybindsMenu_LabelTexts() -> array<String>
