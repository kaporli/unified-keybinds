#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Keybinds
{
// One rebindable keyboard action that a mod added to the game's input mappings.
struct Action
{
    std::string name;       // overridableUI name, also the settings var name
    std::string defaultKey; // key from the mod's input file, e.g. IK_L, or a buttonGroup id
    std::string section;    // mod name, one menu section per mod
    std::string label;      // action text shown in the menu
    std::vector<std::string> mappings; // input mappings that carry the action
    std::vector<std::string> combo;    // keys of the default key combination, else empty
    std::string current;               // Native Settings UI, RCF: key the mod has
    bool placeholder = false;          // bound to an empty buttonGroup, another mod fills in the key
    std::string modId;                 // RCF: the mod that keeps this key, else empty
    std::string labelKey;              // translation key the mod registers for the label, else empty
    bool pad = false;                  // a controller button: shown while a controller is in use
    std::string group;                 // settings group, when not the section's own (vanilla controller keys)
    std::string sectionKey;            // translation key of the section header, when the game has one
    std::vector<std::string> labelKeys; // keys filled into a label like "% / %", as the game's own entries do
    std::string labelFormat;           // such a label, when it isn't the game's (a controller entry's "% (Vehicle)")
    bool scheme = false;               // a controller entry whose buttons come from a control scheme's copy
};

// Prefix of the controller entries this mod adds for an action that has a keyboard entry; the
// rest of the name is that entry's. Its controller button gets this as overridableUI.
inline constexpr const char* kPadPrefix = "MKPad_";

// Key of an entry the player unbound (the game's own value for an action without a key). Kept as
// the player's choice. An entry without a stored key ("None") was never set and gets the mod's key.
inline constexpr const char* kUnbound = "IK_None";

// The key an action starts a session with: RCF's for keys it keeps (the scripts copy every
// change to RCF), else the one saved in the menu, else the mod's.
std::string StartKey(const Action& aAction, const std::map<std::string, std::string>& aSaved);

// False for a key a controller entry can't take: a keyboard key, or a trigger for a button or the
// reverse (the game then drops the button from the mapping until it restarts). Unbound fits any entry.
bool Fits(const Action& aAction, const std::string& aKey);

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
// key and no overridableUI (by mapping name). Controller entries: a mod's own controller
// overridableUI, and for each keyboard action (mod or vanilla) whose mappings hold exactly one
// controller button without one, an entry named kPadPrefix + the action.
// Sorted by section; vanilla controller entries first, in the game's order. aScheme is the
// controller scheme in use (Legacy, Agile, Alternative): some mappings have a copy per scheme.
// aVanillaPads: include the vanilla controller entries (the optional Vanilla Controller file).
std::vector<Action> Collect(const std::filesystem::path& aRoot, const std::string& aBaseOptions,
                            const std::string& aScheme, bool aVanillaPads, const LogFn& aLog);

// Merged mappings (aMerged) with overridableUI added to the single-key mappings in aActions,
// so the game lets them be rebound. Empty if there is nothing to add.
// aCombos maps an action to a key combination picked in the menu ("IK_LControl+IK_K"); the
// action's mapping gets that combination (a buttonGroup), as many as the game allows.
std::string MarkRebindable(const std::filesystem::path& aMerged, const std::vector<Action>& aActions,
                           const std::map<std::string, std::string>& aCombos, const LogFn& aLog);

// aBaseOptions with one group per section added. Groups from an older run are replaced.
std::string MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions);

// "vehicleFlightLinearBrake" -> "Vehicle Flight Linear Brake"
std::string Humanize(const std::string& aText);
} // namespace Keybinds
