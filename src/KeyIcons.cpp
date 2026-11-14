#include "KeyIcons.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <share.h>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/red/ResourceReferenceScriptToken.hpp>
#include <RED4ext/Scripting/Natives/Generated/ink/InputDisplayController.hpp>
#include <RED4ext/Scripting/Utils.hpp>

#include "InputKeyNames.hpp"

namespace
{
// Game 2.5, found by its code. ink::InputKeyIconManager::GetInputKeyName(const InputKeyData&) returns
// red::String; keys 1 to 0x156 branch to the table lookup (the jbe at the end of the pattern).
constexpr uint8_t kNamePattern[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24,
                                    0x18, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x45, 0x8B, 0x08, 0x4D, 0x8B, 0xF0, 0x48,
                                    0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x41, 0x8D, 0x41, 0xFF, 0x3D, 0x55, 0x01, 0x00, 0x00,
                                    0x0F, 0x86};
// The lookup it branches to, 0x29 bytes in: the table's fields at 0xA8 to 0xC4 of the manager.
constexpr uint8_t kLookupPattern[] = {0x39, 0x9E, 0xB0, 0x00, 0x00, 0x00, 0x74, 0x4D, 0x33, 0xD2, 0x41, 0x8B, 0xC1,
                                      0xF7, 0xB6, 0xB4, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00,
                                      0x41, 0x83, 0xCB, 0xFF, 0x8B, 0x14, 0x90, 0x41, 0x3B, 0xD3, 0x74, 0x2F, 0x44,
                                      0x8B, 0x86, 0xC4, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x96, 0xB8, 0x00, 0x00, 0x00};
constexpr size_t kLookupOffset = 0x29;
// ink::InkSystem::GetUIActionToInputKeyData(CName action) -> InputKeyData: the key a hint shows for an
// action on the device in use. Reads the input system at 0x300 of the ink system.
constexpr uint8_t kActionKeyPattern[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48,
                                         0x83, 0xEC, 0x20, 0x4C, 0x8B, 0x91, 0x00, 0x03, 0x00, 0x00, 0x49, 0x8B,
                                         0xD8, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x4D, 0x85, 0xD2, 0x74, 0x51};
// The ink system's key icon manager, and in it the device the hints are drawn for (EInputDevice).
constexpr size_t kIconManager = 0x210;
constexpr size_t kIconDevice = 0x1F8;
constexpr uint32_t kKeyboardDevice = 1;
// From EInputKey IK_Count up: the game's answers for an action whose first button of a device is no
// key (340 on the controller, 341 on the keyboard).
constexpr uint32_t kNoKey = 340;

struct InputKeyData
{
    uint32_t key;
    uint8_t flag;
    uint8_t pad[3];
};

using GetName_t = void* (*)(void*, void*, const InputKeyData*);
using ActionKey_t = InputKeyData* (*)(void*, InputKeyData*, uint64_t);

void* g_target = nullptr;
GetName_t g_original = nullptr;
void* g_actionKeyTarget = nullptr;
ActionKey_t g_actionKeyOriginal = nullptr;

// ink::InputDisplayController::UpdateVisuals(): draws a hint's key. It asks for the action's key 0x87
// bytes in (GetUIActionToInputKeyData) and for its icon 0xD8 bytes in
// (InputKeyIconManager::GetInputKeyIconTextureData).
constexpr uint8_t kVisualsPattern[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                       0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01,
                                       0x00, 0x00, 0x45, 0x33, 0xED, 0x48, 0x8B, 0xF1, 0x44, 0x89, 0xAD, 0x80, 0x00,
                                       0x00, 0x00, 0x44, 0x38, 0xA9, 0x49, 0x01, 0x00, 0x00};
constexpr size_t kVisualsActionKeyCall = 0x87;
constexpr size_t kVisualsIconCall = 0xD8;

// ink::InputIconTextureData: the icon of a key, its atlas and part.
struct IconTexture
{
    RED4ext::Handle<RED4ext::ISerializable> atlas;
    uint64_t atlasPath = 0;
    RED4ext::CName part;
};
static_assert(sizeof(IconTexture) == 0x20);

using Visuals_t = void (*)(void*);
using IconTexture_t = bool (*)(void*, const InputKeyData*, IconTexture*, uint32_t);

uint8_t** g_inkSystem = nullptr; // the game's ink::InkSystem pointer, read by UpdateVisuals 0x49 bytes in
constexpr size_t kVisualsInkSystem = 0x49;
void* g_visualsTarget = nullptr;
Visuals_t g_visualsOriginal = nullptr;
IconTexture_t g_iconTexture = nullptr;

struct ComboKeys
{
    uint32_t keyboard = 0;
    uint32_t pad = 0;
    uint32_t keyboardModifier = 0;
    uint32_t padModifier = 0;
};
// Hints that show a held key (their controllers), to take it away when the hint changes.
std::unordered_set<void*> g_decorated;
std::mutex g_mutex;
std::unordered_map<uint64_t, ComboKeys> g_combos; // action name hash -> main key of its combination per device

#ifdef MKM_TEST
// Test builds: every distinct action, device, key and flag the hints ask for, in ModKeybindsMenu-keyicons.log
// next to the plugin.
FILE* g_testLog = nullptr;
std::vector<std::tuple<uint64_t, uint32_t, uint32_t, uint8_t>> g_seen;
#endif

uint8_t* FindCode(const uint8_t* aPattern, size_t aSize)
{
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if (memcmp(section->Name, ".text", 5) != 0)
            continue;
        auto start = base + section->VirtualAddress;
        size_t size = section->Misc.VirtualSize;
        uint8_t* found = nullptr;
        for (size_t at = 0; at + aSize <= size; ++at)
        {
            if (memcmp(start + at, aPattern, aSize) != 0)
                continue;
            if (found)
                return nullptr; // not unique: another game version
            found = start + at;
        }
        return found;
    }
    return nullptr;
}

// The game's own lookup: buckets of entry indices, each entry chaining to the next (0xFFFFFFFF ends).
bool InTable(uint8_t* aManager, uint32_t aKey)
{
    auto buckets = *reinterpret_cast<uint32_t**>(aManager + 0xA8);
    auto count = *reinterpret_cast<uint32_t*>(aManager + 0xB0);
    auto bucketCount = *reinterpret_cast<uint32_t*>(aManager + 0xB4);
    auto entries = *reinterpret_cast<uint8_t**>(aManager + 0xB8);
    auto capacity = *reinterpret_cast<uint32_t*>(aManager + 0xC0);
    auto stride = *reinterpret_cast<uint32_t*>(aManager + 0xC4);
    if (count == 0 || bucketCount == 0 || !buckets || !entries)
        return false;
    auto index = buckets[aKey % bucketCount];
    for (uint32_t steps = 0; index != 0xFFFFFFFF && index < capacity && steps <= capacity; ++steps)
    {
        auto entry = entries + static_cast<size_t>(index) * stride;
        if (*reinterpret_cast<uint32_t*>(entry + 4) == aKey && *reinterpret_cast<uint32_t*>(entry + 8) == aKey)
            return true;
        index = *reinterpret_cast<uint32_t*>(entry);
    }
    return false;
}

void* GetName_Hook(void* aManager, void* aOut, const InputKeyData* aData)
{
    // Keys outside 1 to 0x156 take the game's fallback name; 0 is one of them.
    if (aData && aData->key - 1 <= 0x155 && !InTable(static_cast<uint8_t*>(aManager), aData->key))
    {
        InputKeyData none = *aData;
        none.key = 0;
        return g_original(aManager, aOut, &none);
    }
    return g_original(aManager, aOut, aData);
}

// The device the hints are drawn for, 0 if unknown.
uint32_t HintDevice(void* aInk)
{
    auto manager = aInk ? *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(aInk) + kIconManager) : nullptr;
    return manager ? *reinterpret_cast<uint32_t*>(manager + kIconDevice) : 0;
}

// An action bound to a key combination has no single key on that device (the game answers no key, or
// IK_Count). Its hint shows the combination's main key instead.
InputKeyData* ActionKey_Hook(void* aInk, InputKeyData* aOut, uint64_t aAction)
{
    auto result = g_actionKeyOriginal(aInk, aOut, aAction);
    uint32_t device = 0;
    if (aOut && (aOut->key == 0 || aOut->key >= kNoKey))
    {
        std::lock_guard lock(g_mutex);
        if (auto it = g_combos.find(aAction); it != g_combos.end())
        {
            device = HintDevice(aInk);
            auto key = device == kKeyboardDevice ? it->second.keyboard : it->second.pad;
            if (device != 0 && key != 0)
                aOut->key = key;
        }
    }
#ifdef MKM_TEST
    if (aOut && g_testLog)
    {
        device = device ? device : HintDevice(aInk);
        std::tuple<uint64_t, uint32_t, uint32_t, uint8_t> entry{aAction, device, aOut->key, aOut->flag};
        if (std::find(g_seen.begin(), g_seen.end(), entry) == g_seen.end())
        {
            g_seen.push_back(entry);
            fprintf(g_testLog, "%s device %u key %u flag %u\n", RED4ext::CName(aAction).ToString(), device,
                    aOut->key, aOut->flag);
            fflush(g_testLog);
        }
    }
#endif
    return result;
}

// The calls in a function at aOffset: the target of the call there (E8 rel32), null if no call.
uint8_t* CallTarget(uint8_t* aFunction, size_t aOffset)
{
    auto at = aFunction + aOffset;
    if (*at != 0xE8)
        return nullptr;
    return at + 5 + *reinterpret_cast<int32_t*>(at + 1);
}

// The script side (ModKeybindsComboIcon.Update in Combo.reds) puts the held key's icon and a "+"
// in front of the hint's icon, or takes them away.
void ShowHeldKey(RED4ext::ink::InputDisplayController* aHint, const IconTexture* aTexture)
{
    // Script functions are found by their full name, with the parameter types.
    static RED4ext::CBaseFunction* update = nullptr;
    static bool looked = false;
    if (!looked)
    {
        looked = true;
        for (auto name : {"ModKeybindsMenu.ModKeybindsComboIcon::Update;inkWidgetResRefCNameBool",
                          "ModKeybindsMenu.ModKeybindsComboIcon::Update"})
        {
            if (!update)
                update = RED4ext::CRTTISystem::Get()->GetFunction(RED4ext::CName(name));
        }
    }
    if (!update)
        return;
    auto icon = aHint->iconRef.widget.Lock();
    if (!icon)
        return;
    RED4ext::ResRef atlas;
    uint64_t path = aTexture ? aTexture->atlasPath : 0;
    std::memcpy(&atlas, &path, sizeof(path));
    RED4ext::CName part = aTexture ? aTexture->part : RED4ext::CName();
    bool show = aTexture != nullptr;
    int32_t status = -1;
    RED4ext::StackArgs_t args;
    args.emplace_back(nullptr, &icon);
    args.emplace_back(nullptr, &atlas);
    args.emplace_back(nullptr, &part);
    args.emplace_back(nullptr, &show);
    RED4ext::ExecuteFunction(static_cast<void*>(nullptr), update, &status, args);
#ifdef MKM_TEST
    if (g_testLog)
    {
        fprintf(g_testLog, "update show %d status %d\n", show, status);
        fflush(g_testLog);
    }
#endif
}

// After the game drew a hint: the held key of an action on a key combination in front of its key.
void Visuals_Hook(void* aHint)
{
    g_visualsOriginal(aHint);
    auto hint = static_cast<RED4ext::ink::InputDisplayController*>(aHint);
    auto ink = g_inkSystem ? *g_inkSystem : nullptr;
    auto manager = ink ? *reinterpret_cast<uint8_t**>(ink + kIconManager) : nullptr;
    uint32_t modifier = 0;
    if (manager && hint->inputActionName)
    {
        auto device = *reinterpret_cast<uint32_t*>(manager + kIconDevice);
        std::lock_guard lock(g_mutex);
        if (auto it = g_combos.find(hint->inputActionName.hash); it != g_combos.end())
            modifier = device == kKeyboardDevice ? it->second.keyboardModifier : it->second.padModifier;
    }
    IconTexture texture;
    InputKeyData key{modifier, 0, {}};
    if (modifier && g_iconTexture(manager, &key, &texture, 0))
    {
        g_decorated.insert(aHint);
        ShowHeldKey(hint, &texture);
    }
    else if (g_decorated.erase(aHint))
    {
        ShowHeldKey(hint, nullptr);
    }
#ifdef MKM_TEST
    if (g_testLog && modifier)
    {
        fprintf(g_testLog, "visuals %s modifier %u atlas %llx part %s\n", hint->inputActionName.ToString(), modifier,
                static_cast<unsigned long long>(texture.atlasPath), texture.part.ToString());
        fflush(g_testLog);
    }
#endif
}

uint32_t KeyValue(const std::string& aName)
{
    for (const auto& [value, name] : InputKeyNames::kAll)
    {
        if (aName == name)
            return static_cast<uint32_t>(value);
    }
    return 0;
}
} // namespace

bool KeyIcons::Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    auto target = FindCode(kNamePattern, sizeof(kNamePattern));
    if (!target)
        return false;
    // The lookup's layout must match too: the jbe's target, 0x29 bytes in.
    auto rel = *reinterpret_cast<int32_t*>(target + sizeof(kNamePattern));
    auto lookup = target + sizeof(kNamePattern) + 4 + rel + kLookupOffset;
    if (memcmp(lookup, kLookupPattern, sizeof(kLookupPattern)) != 0)
        return false;
    if (!aSdk->hooking->Attach(aHandle, target, reinterpret_cast<void*>(&GetName_Hook),
                               reinterpret_cast<void**>(&g_original)))
        return false;
    g_target = target;

    // Combination hints are an extra: without this function hints of combinations stay empty.
    g_actionKeyTarget = FindCode(kActionKeyPattern, sizeof(kActionKeyPattern));
    if (g_actionKeyTarget && !aSdk->hooking->Attach(aHandle, g_actionKeyTarget, reinterpret_cast<void*>(&ActionKey_Hook),
                                                    reinterpret_cast<void**>(&g_actionKeyOriginal)))
        g_actionKeyTarget = nullptr;

    // The held key of a combination in front of the hint's key: the function must ask for the action's
    // key through the function above and read the ink system the same way.
    auto visuals = FindCode(kVisualsPattern, sizeof(kVisualsPattern));
    auto inkLoad = visuals ? visuals + kVisualsInkSystem : nullptr;
    if (visuals && g_actionKeyTarget && CallTarget(visuals, kVisualsActionKeyCall) == g_actionKeyTarget &&
        inkLoad[0] == 0x48 && inkLoad[1] == 0x8B && inkLoad[2] == 0x0D)
    {
        g_inkSystem = reinterpret_cast<uint8_t**>(inkLoad + 7 + *reinterpret_cast<int32_t*>(inkLoad + 3));
        g_iconTexture = reinterpret_cast<IconTexture_t>(CallTarget(visuals, kVisualsIconCall));
        if (g_iconTexture && aSdk->hooking->Attach(aHandle, visuals, reinterpret_cast<void*>(&Visuals_Hook),
                                                   reinterpret_cast<void**>(&g_visualsOriginal)))
            g_visualsTarget = visuals;
    }
#ifdef MKM_TEST
    HMODULE self = nullptr;
    wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ActionKey_Hook), &self) &&
        GetModuleFileNameW(self, path, MAX_PATH))
    {
        std::wstring file(path);
        file.resize(file.find_last_of(L"\\/") + 1);
        g_testLog = _wfsopen((file + L"ModKeybindsMenu-keyicons.log").c_str(), L"w", _SH_DENYNO);
    }
    if (g_testLog)
    {
        fprintf(g_testLog, "attach actionKey %p visuals %p ink %p icon %p\n", g_actionKeyTarget, g_visualsTarget,
                static_cast<void*>(g_inkSystem), reinterpret_cast<void*>(g_iconTexture));
        fflush(g_testLog);
    }
#endif
    return true;
}

void KeyIcons::Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    if (g_visualsOriginal && g_visualsTarget)
        aSdk->hooking->Detach(aHandle, g_visualsTarget);
    if (g_actionKeyOriginal && g_actionKeyTarget)
        aSdk->hooking->Detach(aHandle, g_actionKeyTarget);
    if (g_original && g_target)
        aSdk->hooking->Detach(aHandle, g_target);
}

void KeyIcons::SetCombos(const std::vector<Combo>& aCombos)
{
    std::unordered_map<uint64_t, ComboKeys> combos;
    for (const auto& c : aCombos)
    {
        auto key = KeyValue(c.key);
        if (key == 0)
            continue;
        auto& keys = combos[RED4ext::CName(c.action.c_str()).hash];
        (c.pad ? keys.pad : keys.keyboard) = key;
        (c.pad ? keys.padModifier : keys.keyboardModifier) = KeyValue(c.modifier);
    }
#ifdef MKM_TEST
    if (g_testLog)
    {
        for (const auto& c : aCombos)
            fprintf(g_testLog, "combo %s pad %d %s + %s\n", c.action.c_str(), c.pad, c.modifier.c_str(), c.key.c_str());
        fflush(g_testLog);
    }
#endif
    std::lock_guard lock(g_mutex);
    g_combos = std::move(combos);
}
