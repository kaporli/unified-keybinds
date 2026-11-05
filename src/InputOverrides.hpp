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
// right away once the game has applied its own bindings.
void Set(const std::vector<Key>& aKeys);

#ifdef MKM_TEST
// Test builds: every button of a mapping as the input manager holds it now:
// "<key>|<keyFull>|<overridableUI hash>".
std::vector<std::string> LiveButtons(const std::string& aMapping);
uint32_t Hash(const std::string& aName);
#endif
} // namespace InputOverrides
