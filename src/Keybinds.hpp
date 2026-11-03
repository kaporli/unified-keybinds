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
    std::string section;    // mod name, one menu section per mod
    std::string label;      // action text shown in the menu
    std::vector<std::string> mappings; // input mappings that carry the action
};

// The menu shows display names through the game's translation system, so options.json gets
// keys and the scripts register the text for them with Codeware.
inline constexpr const char* kLabelKeyPrefix = "ModKeybindsMenu-";

// True for the settings groups this mod adds (one per mod section).
bool IsModGroup(const std::string& aGroup);

// Translation key for an action's label.
std::string LabelKey(const Action& aAction);

// Settings group of the action's section, e.g. /key_bindings/SettingsModLetThereBeFlight.
std::string GroupPath(const Action& aAction);

// Translation key and text of the action's section header.
std::string SectionKey(const Action& aAction);
std::string SectionText(const Action& aAction);

using LogFn = std::function<void(const std::string&)>;

// Mod-added actions: overridableUI names in Input Loader's merged mappings that the base
// mappings and the base options.json don't already have, plus mod mappings with a single
// key and no overridableUI (by mapping name). Gamepad-only actions are skipped.
// Sorted by section.
std::vector<Action> Collect(const std::filesystem::path& aRoot, const std::string& aBaseOptions, const LogFn& aLog);

// Merged mappings (aMerged) with overridableUI added to the single-key mappings in aActions,
// so the game lets them be rebound. Empty if there is nothing to add.
std::string MarkRebindable(const std::filesystem::path& aMerged, const std::vector<Action>& aActions);

// aBaseOptions with one group per section added. Groups from an older run are replaced.
std::string MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions);

// "vehicleFlightLinearBrake" -> "Vehicle Flight Linear Brake"
std::string Humanize(const std::string& aText);
} // namespace Keybinds
