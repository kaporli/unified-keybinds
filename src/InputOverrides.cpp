#include "InputOverrides.hpp"

#include <map>
#include <mutex>
#include <set>

#include <RED4ext/Relocation.hpp>

// Layouts and address hashes as used by Mod Settings (MIT, Jack Humbert) on game 2.5.
namespace
{
constexpr uint32_t kApplyOverrides = 1773342692;      // game::input::Manager::ApplyOverrides
constexpr uint32_t kCalcHash = 2583235069;            // hash of an overridableUI name
constexpr uint32_t kGetMappingCollection = 3702071356; // UserMapping::GetMappingCollection

struct Mapping
{
    uint8_t unk00[0x2];
    uint16_t key;     // 02
    uint32_t keyFull; // 04, the same key as a full EInputKey, used for input hints
    uint8_t unk08[0x2C];
    uint32_t overridable; // 34, hash of overridableUI
    uint8_t unk38[0x8];
};
static_assert(sizeof(Mapping) == 0x40);
static_assert(offsetof(Mapping, keyFull) == 0x4);

struct MappingCollection
{
    RED4ext::DynArray<Mapping> mappings;
};

struct UserMapping;
struct ContextManager
{
    void* vftable;
    UserMapping* userMapping; // 08
};

struct Manager
{
    void* vftable;
    void* backend;
    ContextManager* contextManager; // 10
    uint32_t unk18;
    uint8_t unk1C;
    uint8_t overrideSet; // 1D
    uint8_t unk1E[0xE];
    int32_t lastOverrideStatus; // 2C
};
static_assert(offsetof(Manager, overrideSet) == 0x1D);
static_assert(offsetof(Manager, lastOverrideStatus) == 0x2C);

using ApplyOverrides_t = void (*)(Manager*);
using CalcHash_t = uint32_t (*)(const RED4ext::CString&);
using GetMappingCollection_t = MappingCollection* (*)(UserMapping*, RED4ext::CName);

struct Entry
{
    uint32_t hash;
    std::vector<RED4ext::CName> mappings;
    uint16_t key;
    bool original; // the binding loaded at startup, e.g. a key combination
};

struct Original
{
    uint16_t key;
    uint32_t keyFull;
};

void* g_target = nullptr;
ApplyOverrides_t g_original = nullptr;
std::mutex g_mutex;
std::map<std::string, Entry> g_entries;
std::map<std::pair<uint64_t, uint32_t>, Original> g_originals; // by mapping name hash and overridableUI hash
Manager* g_manager = nullptr;
bool g_applied = false; // the game applied its bindings once

// EInputKey values by name, and the gamepad ones (a mod may put the same name on a pad button).
struct Keys
{
    std::map<RED4ext::CName, uint16_t> byName;
    std::set<uint16_t> pad;
};

const Keys& InputKeys()
{
    static Keys keys = [] {
        Keys out;
        auto e = RED4ext::CRTTISystem::Get()->GetEnum("EInputKey");
        if (!e)
            return out;
        for (uint32_t i = 0; i < e->hashList.size() && i < e->valueList.size(); ++i)
        {
            auto value = static_cast<uint16_t>(e->valueList[i]);
            out.byName[e->hashList[i]] = value;
            if (std::string_view(e->hashList[i].ToString()).starts_with("IK_Pad"))
                out.pad.insert(value);
        }
        return out;
    }();
    return keys;
}

// Returns true if a key changed.
bool Apply(Manager* aManager)
{
    static RED4ext::UniversalRelocFunc<GetMappingCollection_t> getMappingCollection(kGetMappingCollection);
    if (!aManager || !aManager->contextManager || !aManager->contextManager->userMapping)
        return false;
    auto userMapping = aManager->contextManager->userMapping;
    const auto& pad = InputKeys().pad;

    bool changed = false;
    for (const auto& [name, entry] : g_entries)
    {
        for (const auto& mapName : entry.mappings)
        {
            auto collection = getMappingCollection(userMapping, mapName);
            if (!collection)
                continue;
            for (auto& mapping : collection->mappings)
            {
                if (mapping.overridable != entry.hash)
                    continue;
                auto& original = g_originals.try_emplace({mapName.hash, entry.hash}, Original{mapping.key, mapping.keyFull})
                                     .first->second;
                if (pad.contains(original.key))
                    continue;
                uint16_t key = entry.original ? original.key : entry.key;
                uint32_t keyFull = entry.original ? original.keyFull : key;
                if (mapping.key != key || mapping.keyFull != keyFull)
                {
                    mapping.key = key;
                    mapping.keyFull = keyFull;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

void ApplyOverrides_Hook(Manager* aManager)
{
    {
        std::lock_guard lock(g_mutex);
        g_manager = aManager;
        // Like the game's own handling of a changed binding, so it rebuilds its key lookup.
        if (Apply(aManager) && g_applied)
        {
            aManager->overrideSet = 1;
            if (aManager->lastOverrideStatus <= 1)
                aManager->lastOverrideStatus = 1;
        }
        g_applied = true;
    }
    g_original(aManager);
}
} // namespace

bool InputOverrides::Attach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    g_target = reinterpret_cast<void*>(static_cast<ApplyOverrides_t>(RED4ext::UniversalRelocFunc<ApplyOverrides_t>(kApplyOverrides)));
    return g_target && aSdk->hooking->Attach(aHandle, g_target, reinterpret_cast<void*>(&ApplyOverrides_Hook),
                                             reinterpret_cast<void**>(&g_original));
}

void InputOverrides::Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    if (g_target && g_original)
        aSdk->hooking->Detach(aHandle, g_target);
}

void InputOverrides::Set(const std::vector<Key>& aKeys)
{
    static RED4ext::UniversalRelocFunc<CalcHash_t> calcHash(kCalcHash);
    const auto& byName = InputKeys().byName;

    Manager* manager = nullptr;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& k : aKeys)
        {
            // A name that is no key (a key combination) means the binding loaded at startup: the
            // game only takes combinations from its input files.
            auto key = byName.find(RED4ext::CName(k.key.c_str()));
            Entry entry{calcHash(RED4ext::CString(k.name.c_str())), {}, 0, key == byName.end()};
            if (!entry.original)
                entry.key = key->second;
            for (const auto& m : k.mappings)
                entry.mappings.emplace_back(m.c_str());
            g_entries[k.name] = std::move(entry);
        }
        if (g_applied)
            manager = g_manager;
    }
    // Through the hooked function, so every plugin that adds overrides takes part.
    if (manager)
        reinterpret_cast<ApplyOverrides_t>(g_target)(manager);
}

