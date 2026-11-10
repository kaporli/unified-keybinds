#pragma once

#include <string>
#include <vector>

#include <RED4ext/RED4ext.hpp>

#include "Keybinds.hpp"

// Accepted events per device. inputContexts.xml sets an action's accepted events per action, the
// same for every device. The game keeps a keyboard and a controller copy in the action's runtime
// object, and copies the one for the device in use into a third when the device changes. The plugin
// sets each copy by a rule: as set, every event (empty list), or without the hold events. So a
// (Hold) action sharing a button starts on a long press only on the device where it is shared, and
// the action it shares with stops reacting to that hold there. The scripts report the device, so
// the copy in use is right before the first change too.
namespace AcceptedEvents
{
bool Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);
void Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle);

// Before the game loads its input contexts.
void Set(const std::vector<Keybinds::EventRule>& aRules);

// The device in use (the game's InputSchemes.Device): true for a controller.
void SetDevice(bool aPad);

// For the log: what was changed or left alone since the last call.
std::vector<std::string> TakeChanged();

#ifdef MKM_TEST
// Test builds: sets list aList (0-2) of each listed action's object to aSize (if it fits) and
// returns the sizes and entries of all three.
std::string TestLists(int aList, int aSize);
#endif
} // namespace AcceptedEvents
