#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Keybinds.hpp"

// The game resets every setting when UserSettings.json has a group that options.json doesn't
// define. If the mod sections were saved in that file, removing a mod with keys (or this mod)
// would reset all settings on the next launch. So they never go into it: the game reads a copy
// with the sections added, and what it writes is split again, mod keys into a separate store
// ({"action": "IK_..."}).
namespace UserSettings
{
// The game's settings file: %LOCALAPPDATA%/CD Projekt Red/Cyberpunk 2077/UserSettings.json.
std::filesystem::path Path();

// Moves mod sections from aSource into aStore and writes the rest to aTarget (aSource may be
// aTarget). Returns false if aSource could not be read as settings. aDefaults (by action name) is
// stored with the keys: the game writes no defaults for the mod sections.
bool Split(const std::filesystem::path& aSource, const std::filesystem::path& aTarget,
           const std::filesystem::path& aStore, const Keybinds::LogFn& aLog,
           const std::map<std::string, std::string>& aDefaults = {});

// aSettings with a section for every action, keys from aStore, else the action's default.
// Empty if aSettings could not be read as settings.
std::string WithModSections(const std::filesystem::path& aSettings, const std::filesystem::path& aStore,
                            const std::vector<Keybinds::Action>& aActions);

// Keys in aStore, by action name. A key that is the action's default when it was saved is left
// out: the default can differ next session (another control scheme, another version). So is
// "None" (never set); IK_None (unbound by the player) stays.
std::map<std::string, std::string> SavedKeys(const std::filesystem::path& aStore);

// Stores from before defaults were saved (1.4.0): drops the keys of controller entries that follow
// the control scheme. Those entries had no effect then, and their keys would override the scheme.
void DropUnrecordedSchemeKeys(const std::filesystem::path& aStore, const std::vector<Keybinds::Action>& aActions,
                              const Keybinds::LogFn& aLog);

// Value of a list option in aSettings, else its default in aOptions (the game's
// r6/config/settings/options.json). Empty if neither has it.
std::string ListValue(const std::filesystem::path& aSettings, const std::filesystem::path& aOptions,
                      const std::string& aGroup, const std::string& aName);
} // namespace UserSettings
