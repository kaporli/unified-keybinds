// Provided by ModKeybindsMenu.dll, built at startup from the installed mods' input files.

// overridableUI names of the actions in Controls > Key Bindings > Mods
public static native func ModKeybindsMenu_Names() -> array<CName>

// Key each action has in its mod's input file, same order as ModKeybindsMenu_Names
public static native func ModKeybindsMenu_Defaults() -> array<CName>
