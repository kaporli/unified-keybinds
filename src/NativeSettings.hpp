#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Keybinds.hpp"

// Key bindings that CET mods add through Native Settings UI. They only exist in CET's Lua state,
// which runs after the menu sections are built, so the Lua companion (cet/init.lua) reports them
// each launch and they are listed from the next one.
namespace NativeSettings
{
// Prefix of their settings var names, also how the scripts tell them apart.
inline constexpr const char* kNamePrefix = "NSUI_";

struct Entry
{
    std::string name;    // settings var name, kNamePrefix + path + label
    std::string section; // tab label
    std::string label;   // option label
    std::string key;     // key the mod has now
    std::string defaultKey;
};

// Entries from the last launch, as actions. Empty unless CET runs the Lua companion and
// Native Settings UI is installed.
std::vector<Keybinds::Action> Read(const std::filesystem::path& aRoot);

// Stores what the Lua companion reported.
bool Write(const std::filesystem::path& aRoot, const std::vector<Entry>& aEntries);
} // namespace NativeSettings
