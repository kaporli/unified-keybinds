#pragma once

#include <filesystem>
#include <vector>

#include "Keybinds.hpp"

// Key bindings that mods keep in Redscript Configuration Framework (RCF), like the DigitalVixen
// mods. Their input files bind every action to a placeholder (an empty buttonGroup) and RCF puts
// the real key in place while the game runs. RCF stores each mod's settings page and values in
// r6/storages/RedscriptConfigFramework, which is read here; the scripts keep both sides in sync
// while the game runs (Rcf.reds).
namespace Rcf
{
// Mod id of RCF's own settings, which hold the key that opens its hub.
inline constexpr const char* kFrameworkId = "_DVRCF_Framework";

// Gives the actions RCF lists RCF's section, label, default and current key, in RCF's order.
// Leaves out the gamepad keys RCF lists (RCF applies those) and placeholder actions RCF doesn't
// list (keys of a mod that isn't installed, or that RCF has not seen yet).
void Apply(const std::filesystem::path& aRoot, std::vector<Keybinds::Action>& aActions, const Keybinds::LogFn& aLog);
} // namespace Rcf
