#include "AcceptedEvents.hpp"

#include <Windows.h>

#include <algorithm>
#include <mutex>

namespace
{
// Game 2.5 functions, found by their code rather than their address.
// Registers one mapping of an action in an input context with its accepted events:
// bool (registry, context name, action name, mapping name, accepted events, flag, flag).
// The first registration of an action creates its runtime object and copies the events into it.
constexpr const char* kRegisterPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 8B 81 0C 01 00 00 49 8B D9 49 8B F8 33 F6";
// Constructor of an action's runtime object: (object, action name, mapping name, flag).
constexpr const char* kInitPattern = "4C 89 44 24 18 53 48 83 EC 20 48 8B D9 48 89 11 33 D2 48 8D 05 ?? ?? ?? ?? "
                                     "48 89 51 10 48 83 C1 08 48 89 01 88 53 3F 48 89 53 70";
// The object's accepted event lists: the one in use, keyboard, controller. The game copies the
// keyboard or controller list into the first when the device changes, not when it loads them.
constexpr size_t kLists = 0x118;
constexpr size_t kListStride = 0x10;
constexpr int kInUseList = 0;
constexpr int kKeyboardList = 1;
constexpr int kControllerList = 2;

struct EventList // the game's array of event ids
{
    int32_t* entries;
    uint32_t capacity;
    uint32_t size;
};

using Register_t = bool (*)(void*, uint64_t, uint64_t, uint64_t, EventList*, uint8_t, uint8_t);
using Init_t = void* (*)(void*, uint64_t, uint64_t, uint8_t);

void* g_registerTarget = nullptr;
Register_t g_registerOriginal = nullptr;
void* g_initTarget = nullptr;
Init_t g_initOriginal = nullptr;

std::mutex g_mutex;
struct Rule
{
    uint64_t hash;
    Keybinds::EventRule rule;
};
struct Object
{
    uint64_t hash;
    uint8_t* object;
    std::vector<int32_t> loaded; // the list the game made, empty until seen
    bool checked = false;        // its memory was checked since it was made or a session changed
};
std::vector<Rule> g_rules;
std::vector<Object> g_objects; // newest runtime object of each listed action
std::vector<std::string> g_changed;
bool g_pad = false; // the device in use is a controller
std::vector<uint64_t> g_blocked; // hold-only actions giving way to a prompt

EventList* List(uint8_t* aObject, int aList)
{
    return reinterpret_cast<EventList*>(aObject + kLists + aList * kListStride);
}

// Memory that can be read, checked before reading through a pointer of an unknown layout. Not a
// structured exception handler: a crash reporter can end the game on the first fault.
bool Readable(const void* aAt, size_t aSize, bool aWritable = false)
{
    auto at = reinterpret_cast<uintptr_t>(aAt);
    if (at < 0x10000 || at + aSize >= 0x800000000000ull)
        return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(aAt, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
        !(mbi.Protect & (aWritable ? PAGE_READWRITE | PAGE_EXECUTE_READWRITE
                                   : PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    return at + aSize <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

// The object looks as it does on game 2.5: three lists, each in a buffer of its own with room for
// aSize event ids. aFirst: as the game made them, all three full and equal. Anything else means
// another layout (another game version): the object is left alone.
bool Plausible(uint8_t* aObject, uint32_t aSize, bool aFirst)
{
    // Writable: the lists are written after this check.
    if (aSize == 0 || aSize > 16 || !Readable(aObject + kLists, 3 * kListStride, true))
        return false;
    EventList* lists[] = {List(aObject, kInUseList), List(aObject, kKeyboardList), List(aObject, kControllerList)};
    for (auto list : lists)
    {
        if (list->capacity < aSize || list->capacity > 32 || list->size > list->capacity ||
            (aFirst && list->size != aSize) || !Readable(list->entries, list->capacity * sizeof(int32_t), true))
            return false;
    }
    if (lists[0]->entries == lists[1]->entries || lists[0]->entries == lists[2]->entries ||
        lists[1]->entries == lists[2]->entries)
        return false;
    if (aFirst)
    {
        for (uint32_t i = 0; i < aSize; ++i)
        {
            auto id = lists[0]->entries[i];
            if (id < 0 || id > 16 || lists[1]->entries[i] != id || lists[2]->entries[i] != id)
                return false;
        }
    }
    return true;
}

// The game's hold events: button hold progress and complete, pattern hold progress and passed.
bool IsHold(int32_t aId)
{
    return aId == 2 || aId == 3 || aId == 13 || aId == 14;
}

std::vector<int32_t> Content(const std::vector<int32_t>& aLoaded, Keybinds::EventMode aMode)
{
    std::vector<int32_t> out;
    if (aMode == Keybinds::EventMode::All)
        return out; // an empty list takes every event
    if (aMode == Keybinds::EventMode::None)
        return {14}; // pattern hold passed: never sent for a plain button
    if (aMode == Keybinds::EventMode::Tap)
        return {0, 1}; // button pressed, released
    if (aMode == Keybinds::EventMode::Hold)
        return {3}; // button hold complete
    for (auto id : aLoaded)
    {
        if (aMode == Keybinds::EventMode::Keep || !IsHold(id))
            out.push_back(id);
    }
    return out;
}

void Write(EventList* aList, const std::vector<int32_t>& aContent)
{
    for (size_t i = 0; i < aContent.size(); ++i)
        aList->entries[i] = aContent[i];
    aList->size = static_cast<uint32_t>(aContent.size());
}

// Keyboard and controller lists as the rule says, and the list in use matching the device.
// Runs on every device change, many times a second with a gyro mapped to the mouse: the memory check
// (VirtualQuery, milliseconds each in the game's process) is made once per object, and again after a
// session started or ended (Recheck).
void Apply(Object& aObject, const Keybinds::EventRule& aRule)
{
    if (aObject.loaded.empty())
        return;
    if (!aObject.checked)
    {
        if (!Plausible(aObject.object, static_cast<uint32_t>(aObject.loaded.size()), false))
            return;
        aObject.checked = true;
    }
    auto keyboard = Content(aObject.loaded, aRule.keyboard);
    auto pad = Content(aObject.loaded, aRule.controller);
    for (int list : {kInUseList, kKeyboardList, kControllerList})
    {
        if (List(aObject.object, list)->capacity < std::max(keyboard.size(), pad.size()))
            return;
    }
    Write(List(aObject.object, kKeyboardList), keyboard);
    Write(List(aObject.object, kControllerList), pad);
    auto inUse = g_pad ? pad : keyboard;
    if (std::find(g_blocked.begin(), g_blocked.end(), aObject.hash) != g_blocked.end())
        inUse = Content(inUse, Keybinds::EventMode::NoHold);
    Write(List(aObject.object, kInUseList), inUse);
}

const Keybinds::EventRule* Find(uint64_t aAction)
{
    for (const auto& r : g_rules)
    {
        if (r.hash == aAction)
            return &r.rule;
    }
    return nullptr;
}

const char* ModeName(Keybinds::EventMode aMode)
{
    switch (aMode)
    {
    case Keybinds::EventMode::Keep: return "as set";
    case Keybinds::EventMode::All: return "every event";
    case Keybinds::EventMode::NoHold: return "no hold";
    case Keybinds::EventMode::None: return "no event";
    case Keybinds::EventMode::Tap: return "tap";
    case Keybinds::EventMode::Hold: return "hold";
    }
    return "";
}

void* FindPattern(const char* aPattern)
{
    std::vector<int> bytes;
    for (const char* p = aPattern; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        bytes.push_back(*p == '?' ? -1 : static_cast<int>(strtoul(p, nullptr, 16)));
        p += 2;
    }
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if (memcmp(section->Name, ".text", 5) != 0)
            continue;
        auto start = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize;
        void* found = nullptr;
        for (size_t at = 0; at + bytes.size() <= size; ++at)
        {
            size_t k = 0;
            while (k < bytes.size() && (bytes[k] < 0 || start[at + k] == bytes[k]))
                ++k;
            if (k == bytes.size())
            {
                if (found)
                    return nullptr; // not unique: another game version
                found = start + at;
            }
        }
        return found;
    }
    return nullptr;
}

void* Init_Hook(void* aObject, uint64_t aAction, uint64_t aMapping, uint8_t aFlag)
{
    auto result = g_initOriginal(aObject, aAction, aMapping, aFlag);
    std::lock_guard lock(g_mutex);
    // The game builds the objects again when a session starts: only the newest one is live.
    if (Find(aAction))
    {
        std::erase_if(g_objects, [&](const auto& o) { return o.hash == aAction; });
        g_objects.push_back({aAction, static_cast<uint8_t*>(aObject), {}});
    }
    return result;
}

bool Register_Hook(void* aRegistry, uint64_t aContext, uint64_t aAction, uint64_t aMapping, EventList* aEvents,
                   uint8_t aFlag1, uint8_t aFlag2)
{
    bool result = g_registerOriginal(aRegistry, aContext, aAction, aMapping, aEvents, aFlag1, aFlag2);
    std::lock_guard lock(g_mutex);
    auto rule = Find(aAction);
    if (!rule)
        return result;
    for (auto& o : g_objects)
    {
        if (o.hash != aAction)
            continue;
        if (o.loaded.empty())
        {
            auto pad = List(o.object, kControllerList);
            if (!Readable(pad, sizeof(EventList)) || !Plausible(o.object, pad->size, true))
            {
                g_changed.push_back(rule->action + " left alone: unexpected layout (other game version?)");
                continue;
            }
            o.loaded.assign(pad->entries, pad->entries + pad->size);
            o.checked = true;
            g_changed.push_back(rule->action + ": keyboard " + ModeName(rule->keyboard) + ", controller " +
                                ModeName(rule->controller));
        }
        Apply(o, *rule);
    }
    return result;
}
} // namespace

bool AcceptedEvents::Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    g_registerTarget = FindPattern(kRegisterPattern);
    g_initTarget = FindPattern(kInitPattern);
    if (!g_registerTarget || !g_initTarget)
        return false;
    if (!aSdk->hooking->Attach(aHandle, g_initTarget, reinterpret_cast<void*>(&Init_Hook),
                               reinterpret_cast<void**>(&g_initOriginal)))
        return false;
    if (!aSdk->hooking->Attach(aHandle, g_registerTarget, reinterpret_cast<void*>(&Register_Hook),
                               reinterpret_cast<void**>(&g_registerOriginal)))
    {
        aSdk->hooking->Detach(aHandle, g_initTarget);
        g_initOriginal = nullptr;
        return false;
    }
    return true;
}

bool AcceptedEvents::Active()
{
    return g_registerOriginal && g_initOriginal;
}

void AcceptedEvents::Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    if (g_registerOriginal)
        aSdk->hooking->Detach(aHandle, g_registerTarget);
    if (g_initOriginal)
        aSdk->hooking->Detach(aHandle, g_initTarget);
}

void AcceptedEvents::Set(const std::vector<Keybinds::EventRule>& aRules)
{
    std::lock_guard lock(g_mutex);
    g_rules.clear();
    for (const auto& r : aRules)
        g_rules.push_back({RED4ext::CName(r.action.c_str()).hash, r});
}

void AcceptedEvents::SetController(const std::string& aAction, Keybinds::EventMode aMode)
{
    auto hash = RED4ext::CName(aAction.c_str()).hash;
    std::lock_guard lock(g_mutex);
    for (auto& r : g_rules)
    {
        if (r.hash != hash || r.rule.controller == aMode)
            continue;
        r.rule.controller = aMode;
        for (auto& o : g_objects)
        {
            if (o.hash == hash)
                Apply(o, r.rule);
        }
    }
}

void AcceptedEvents::SetDevice(bool aPad)
{
    std::lock_guard lock(g_mutex);
    g_pad = aPad;
    for (auto& o : g_objects)
    {
        if (auto rule = Find(o.hash))
            Apply(o, *rule);
    }
}

void AcceptedEvents::Recheck()
{
    std::lock_guard lock(g_mutex);
    for (auto& o : g_objects)
        o.checked = false;
}

void AcceptedEvents::SetBlocked(const std::vector<std::string>& aActions)
{
    std::lock_guard lock(g_mutex);
    std::vector<uint64_t> blocked;
    for (const auto& a : aActions)
        blocked.push_back(RED4ext::CName(a.c_str()).hash);
    if (blocked == g_blocked)
        return;
    g_blocked = std::move(blocked);
    for (auto& o : g_objects)
    {
        if (auto rule = Find(o.hash))
            Apply(o, *rule);
    }
}

std::vector<std::string> AcceptedEvents::TakeChanged()
{
    std::lock_guard lock(g_mutex);
    auto out = std::move(g_changed);
    g_changed.clear();
    return out;
}

#ifdef MKM_TEST
std::string AcceptedEvents::TestLists(int aList, int aSize)
{
    std::lock_guard lock(g_mutex);
    std::string out = std::to_string(g_objects.size()) + " objects:";
    for (auto& o : g_objects)
    {
        auto object = o.object;
        out += " [";
        for (int i = 0; i < 3; ++i)
        {
            auto list = List(object, i);
            if (i == aList && aSize >= 0 && static_cast<uint32_t>(aSize) <= list->capacity)
                list->size = static_cast<uint32_t>(aSize);
            out += (i ? " " : "") + std::to_string(list->size) + "/" + std::to_string(list->capacity);
            for (uint32_t k = 0; k < list->size && list->entries; ++k)
                out += (k ? "," : ":") + std::to_string(list->entries[k]);
        }
        out += "]";
    }
    return out;
}
#endif
