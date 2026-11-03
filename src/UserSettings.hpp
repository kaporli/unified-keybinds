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
// aTarget). Returns false if aSource could not be read as settings.
bool Split(const std::filesystem::path& aSource, const std::filesystem::path& aTarget,
           const std::filesystem::path& aStore, const Keybinds::LogFn& aLog);

// aSettings with a section for every action, keys from aStore, else the action's default.
// Empty if aSettings could not be read as settings.
std::string WithModSections(const std::filesystem::path& aSettings, const std::filesystem::path& aStore,
                            const std::vector<Keybinds::Action>& aActions);

// Keys in aStore, by action name.
std::map<std::string, std::string> SavedKeys(const std::filesystem::path& aStore);
} // namespace UserSettings
