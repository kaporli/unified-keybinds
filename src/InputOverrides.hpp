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
    std::string key;                   // EInputKey name, e.g. IK_F7
};

bool Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
void Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);

// Keys used from now on. Applied right away once the game has applied its own bindings.
void Set(const std::vector<Key>& aKeys);
} // namespace InputOverrides
