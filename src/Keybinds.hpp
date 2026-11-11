#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <set>
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
    std::string place;                 // controller entries: where the button applies, empty on foot
    std::string holdOption;            // controller entry of a "(Hold)" binding or Scan (Toggle): the game's option, e.g. sprintHold
    std::vector<std::string> gameActions; // ...and the game actions its mappings feed, e.g. Sprint
    bool holdShare = false;            // ...and its button may be shared: it starts on a long press then
};

// A "(Hold)" action whose button is shared with another action on foot: it should start only once
// the button is held (a long press), so a tap does the other action. Decided per device.
struct HoldOnly
{
    std::string action;      // game action, e.g. Sprint
    std::string option;      // the game's (Hold) option, e.g. sprintHold (its keyboard entry)
    std::string padEntry;    // its controller entry, e.g. MKPad_sprintHold
    bool controller = false; // its controller button is shared
    bool keyboard = false;   // its keyboard key is shared
    std::vector<std::string> padMappings; // mappings of the controller entries it shares with
    std::vector<std::string> keyOptions;  // the game's key binding options it shares a key with
};

// How a device's copy of an action's accepted events is set.
enum class EventMode
{
    Keep,  // as in the input contexts
    All,   // empty: every event
    NoHold, // without the hold events
    None,   // no event from the device (a pattern event a button never sends)
    Tap,    // pressed and released only: reacts to a tap, not to a hold
    Hold    // hold complete only: reacts to a hold, not to a tap
};

struct EventRule
{
    std::string action;
    EventMode keyboard = EventMode::Keep;
    EventMode controller = EventMode::Keep;
    // For a hold-only action: the actions on its shared key, per device. A prompt for one of them
    // (its input action) on screen makes the hold-only action give way.
    std::vector<std::string> keyboardShared;
    std::vector<std::string> controllerShared;
};

// Keys of the game's own key binding options in its settings file: group path -> option -> key.
std::map<std::string, std::map<std::string, std::string>> GameKeys(const std::filesystem::path& aUserSettings);

// The hold-only actions for the keys a session starts with: controller buttons from aSaved (or
// defaults), keyboard keys from the game's settings file (same settings group as the hold option).
// aAll: also the (Hold) actions that share nothing now but may (holdShare). They get the same setup
// with every event on both devices, so a button shared later counts at once.
std::vector<HoldOnly> HoldOnlyActions(const std::vector<Action>& aActions,
                                      const std::map<std::string, std::string>& aSaved,
                                      const std::filesystem::path& aUserSettings, bool aAll = false);

// Mappings that feed an action which changes when a (Hold) action comes onto its button: one that
// reacts to a hold itself (it gives the hold up), or Sprint (Toggle). Those changes are made when the
// game starts, so sharing such a button applies after a restart.
// aShareable: the (Hold) actions that may share (two of them on one button both start on a long press).
std::set<std::string> HoldSensitiveMappings(const std::filesystem::path& aMerged, const std::set<std::string>& aShareable);

// The actions that aMappings feed during play (aContexts: the merged input contexts).
std::vector<std::string> FedActions(const std::filesystem::path& aContexts, const std::vector<std::string>& aMappings);

// Actions of the (Hold) bindings whose key may be shared (holdShare entries), e.g. Sprint, Crouch.
std::set<std::string> ShareableActions(const std::vector<Action>& aActions);

// The Classic scheme dodges on a double tap, which the game counts itself. It misses the taps on a
// button that also has a hold (the press is held back and arrives together with the release), so a
// dodge moved to such a button never fires. Input contexts (aXml) with an action of ours on aMapping
// (the dodge's Classic copy) next to every Dodge: it gets the plain presses, and the scripts count
// them (ClassicDodge.reds). Empty if unchanged.
inline constexpr const char* kClassicDodge = "ModKeybindsDodge";
std::string AddClassicDodge(const std::string& aXml, const std::string& aMapping);

// Input contexts (aXml) with aActions taken out of the menu's conflict checks (the Settings*
// contexts), so their key can be shared with another action: an included context that holds one
// is written out in place without it. Gameplay contexts stay as they are. Empty if unchanged.
std::string MarkShareable(const std::string& aXml, const std::set<std::string>& aActions,
                          const std::function<void(const std::string&)>& aLog);

// Merged input contexts (aMerged) with what the hold-only actions need, and the event rules for
// AcceptedEvents. A hold-only action gets acceptedEvents "hold complete" and "released" (one with a
// list of its own is left alone), kept on the devices where its button is shared. An action it
// shares with that reacts to a hold (hold events in its list, or a <hold> without a list) loses the
// hold on that device; without a list it gets one with every other event. aMappings is the merged
// mappings file, for the mappings of the game's options. Empty if the contexts need no change.
std::string MarkHoldOnly(const std::filesystem::path& aMerged, const std::filesystem::path& aMappings,
                         const std::vector<HoldOnly>& aHoldOnly, std::vector<EventRule>& aRules,
                         const std::function<void(const std::string&)>& aLog);

// Prefix of the controller entries this mod adds for an action that has a keyboard entry; the
// rest of the name is that entry's. Its controller button gets this as overridableUI.
inline constexpr const char* kPadPrefix = "MKPad_";

// Key of an entry the player unbound (the game's own value for an action without a key). Kept as
// the player's choice. An entry without a stored key ("None") was never set and gets the mod's key.
inline constexpr const char* kUnbound = "IK_None";

// A key no keyboard or controller sends (a reserved key code): the mappings file's stand-in for a
// controller entry that has no button yet.
inline constexpr const char* kNoKey = "IK_Unknown0A";

// The key an action starts a session with: RCF's for keys it keeps (the scripts copy every
// change to RCF), else the one saved in the menu, else the mod's.
std::string StartKey(const Action& aAction, const std::map<std::string, std::string>& aSaved);

// False for a key a controller entry can't take: a keyboard key, or a trigger for a button or the
// reverse (the game then drops the button from the mapping until it restarts). A controller combination
// ("IK_Pad_LeftShoulder+IK_Pad_A_CROSS") fits a button entry. Unbound fits any entry.
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

// aActions plus the (Hold) bindings that may share their key when Collect left out the vanilla
// controller entries (they are not in the menu then, but their keyboard key can still be shared).
std::vector<Action> WithHoldOptions(const std::vector<Action>& aActions);

// Merged mappings (aMerged) with overridableUI added to the single-key mappings in aActions,
// so the game lets them be rebound. Empty if there is nothing to add.
// aSaved holds the stored keys. A key combination picked in the menu ("IK_LControl+IK_K", or two
// controller buttons for a controller entry) goes into the action's mapping as a buttonGroup, as many
// as the game allows. A controller entry without a default button gets a row with its saved button.
// aHoldKeys: keyboard keys of the game's (Hold) options that may share their key, written into
// their mapping (the game doesn't apply a key another binding uses).
std::string MarkRebindable(const std::filesystem::path& aMerged, const std::vector<Action>& aActions,
                           const std::map<std::string, std::string>& aSaved,
                           const std::map<std::string, std::string>& aHoldKeys, const LogFn& aLog);

// aBaseOptions with one group per section added. Groups from an older run are replaced.
std::string MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions);

// A key binding entry for the conflict check.
struct BindingEntry
{
    std::string name;                  // settings var name
    std::string key;                   // its key, a combination as "IK_A+IK_B"
    std::vector<std::string> mappings; // mappings it sets; empty: the mod reads the key itself
    bool atDefault = false;            // at its default key (the game's, or the mod's)
    std::string mod;                   // settings group of the mod section it is in, empty for the game's
    bool gamePad = false;              // one of the game's controller entries (the game's menu doesn't check them)
    bool shareable = false;            // a (Hold) entry that may share its key (tap / hold)
};

// Mappings of each overridableUI name in a mappings file (aMappings).
std::map<std::string, std::vector<std::string>> OptionMappings(const std::filesystem::path& aMappings);

// For each entry, the other entries on the same key that can react at the same time: their actions
// are in one gameplay context (aContexts, the merged input contexts), or one of them reads the key
// itself (no mappings). At least one must be a mod's entry, or both the game's controller entries
// (the game's menu checks its own keyboard entries). Two
// entries at their default keys count only when they are from two different mods (mods pick
// defaults that fit the game, not each other). Not counted: a (Hold) entry
// sharing with a non-(Hold) one (tap / hold), entries feeding the same action, and entries whose
// mappings no context uses.
std::vector<std::vector<std::string>> Conflicts(const std::filesystem::path& aContexts,
                                                const std::vector<BindingEntry>& aEntries);

// "vehicleFlightLinearBrake" -> "Vehicle Flight Linear Brake"
std::string Humanize(const std::string& aText);
} // namespace Keybinds
