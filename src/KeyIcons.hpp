#pragma once

#include <string>
#include <vector>

#include <RED4ext/RED4ext.hpp>

// Key icons in on-screen hints (the game's ink::InputKeyIconManager). The game looks the name of a
// hint's key up in its icon table without checking that the key is there. A key combination or an
// unbound entry leaves an action without a single key on a device, and its hint then crashed the
// game when it was drawn for that device (the heal item hint on every device change, for example).
// The plugin passes a key that isn't in the table on as no key, so the hint gets the game's fallback, and
// gives the hint of an action on a key combination the combination's main key.
namespace KeyIcons
{
bool Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
void Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);

// An input action bound to a key combination on one device: the combination's main key (pressed
// last) and the key held before it. Hints of the action show the main key on that device, where the
// game finds no single key, with the held key and a "+" in front of it (ModKeybindsComboIcon).
struct Combo
{
    std::string action;
    bool pad = false;
    std::string key;
    std::string modifier;
};
void SetCombos(const std::vector<Combo>& aCombos);
} // namespace KeyIcons
