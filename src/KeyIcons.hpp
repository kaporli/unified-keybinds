#pragma once

#include <RED4ext/RED4ext.hpp>

// Key icons in on-screen hints (the game's ink::InputKeyIconManager). The game looks the name of a
// hint's key up in its icon table without checking that the key is there. A key combination or an
// unbound entry leaves an action without a single key on a device, and its hint then crashed the
// game when it was drawn for that device (the heal item hint on every device change, for example).
// The plugin passes a key that isn't in the table on as no key, so the hint gets the game's fallback.
namespace KeyIcons
{
bool Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
void Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
} // namespace KeyIcons
