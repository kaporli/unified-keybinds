#include "KeyIcons.hpp"

#include <Windows.h>

#include <cstring>

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

struct InputKeyData
{
    uint32_t key;
    uint8_t flag;
    uint8_t pad[3];
};

using GetName_t = void* (*)(void*, void*, const InputKeyData*);

void* g_target = nullptr;
GetName_t g_original = nullptr;

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
    return true;
}

void KeyIcons::Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    if (g_original && g_target)
        aSdk->hooking->Detach(aHandle, g_target);
}
