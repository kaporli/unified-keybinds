#pragma once

#include <string>
#include <utility>
#include <vector>

#include <RED4ext/RED4ext.hpp>

// The game only applies key binding settings to its own actions: a mod key changed in the menu
// would keep its old key. This applies them the way Mod Settings does for its own keys, by hooking
// the game's ApplyOverrides and setting the key on the mappings that carry the action's
// overridableUI name.
namespace InputOverrides
{
struct Key
{
    std::string name;                  // overridableUI name
    std::vector<std::string> mappings; // mappings that carry it
    std::string key;                   // EInputKey name, e.g. IK_F7, or IK_LControl+IK_K
    bool pad = false;                  // a controller entry: may set controller buttons
};

bool Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
void Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);

// Keys used from now on. A key combination ("IK_LControl+IK_K", or a mod's buttonGroup id) keeps
// the binding loaded at startup: the game only takes combinations from its input files. Applied
// at the next Update once the game has applied its own bindings.
void Set(const std::vector<Key>& aKeys);

// Applies keys set since the last call. Called once per frame from the game's main loop: the
// scripts can set keys from another thread while the game replaces its bindings (a new game
// starting), and applying them there read bindings the game had just freed.
void Update();

// False while a session starts or ends (the game replaces its bindings then): Update waits.
void SetStable(bool aStable);

// True when the game dropped the binding of this entry from its mappings. It keeps one of two
// buttons with the same key in a mapping, so unbinding an action's key and its controller button
// leaves only one: the other is bound again after a restart.
bool Dropped(const std::string& aName, const std::vector<std::string>& aMappings);

#ifdef MKM_TEST
// Test builds: every button of a mapping as the input manager holds it now:
// "<key>|<keyFull>|<overridableUI hash>".
std::vector<std::string> LiveButtons(const std::string& aMapping);
uint32_t Hash(const std::string& aName);
#endif
} // namespace InputOverrides
