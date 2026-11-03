#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Keybinds
{
// One rebindable keyboard action that a mod added to the game's input mappings.
struct Action
{
    std::string name;       // overridableUI name, also the settings var name
    std::string defaultKey; // key from the mod's input file, e.g. IK_L
    std::string label;      // text shown in the key bindings menu
};

using LogFn = std::function<void(const std::string&)>;

// Mod-added actions: overridableUI names in Input Loader's merged mappings that the base
// mappings and the base options.json don't already have. Gamepad-only actions are skipped.
std::vector<Action> Collect(const std::filesystem::path& aRoot, const std::string& aBaseOptions, const LogFn& aLog);

// aBaseOptions with the Mods group added. Any Mods group already in it is replaced.
std::string MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions);

// "vehicleFlightLinearBrake" -> "Vehicle Flight Linear Brake"
std::string Humanize(const std::string& aText);
} // namespace Keybinds
