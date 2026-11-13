#include "InputOverrides.hpp"

#include <algorithm>
#include <iterator>
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
    bool pad;      // a controller entry
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
// The bindings g_manager held when the game last applied them. A new game replaces them and frees
// the old ones while g_manager can still point at them.
ContextManager* g_context = nullptr;
UserMapping* g_userMapping = nullptr;
bool g_applied = false; // the game applied its bindings once
bool g_pending = false; // keys set since the last Update
bool g_stable = true;   // no session starting or ending
DWORD g_mainThread = 0; // the thread Update runs on

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

// The manager's bindings, false if they can't be read.
bool ReadBindings(Manager* aManager, ContextManager** aContext, UserMapping** aMapping)
{
    __try
    {
        *aContext = aManager->contextManager;
        *aMapping = *aContext ? (*aContext)->userMapping : nullptr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// True when g_manager still holds the bindings the game last applied. Call with g_mutex held.
bool BindingsUnchanged()
{
    ContextManager* context = nullptr;
    UserMapping* mapping = nullptr;
    return g_manager && g_userMapping && ReadBindings(g_manager, &context, &mapping) && context == g_context &&
           mapping == g_userMapping;
}

// Key codes Windows never sends (reserved or unassigned virtual keys), for unbound rows.
constexpr uint16_t kUnboundKeys[] = {0x0A, 0x0B, 0x0E, 0x0F, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40};

bool IsUnboundKey(uint16_t aKey)
{
    return std::find(std::begin(kUnboundKeys), std::end(kUnboundKeys), aKey) != std::end(kUnboundKeys);
}

// A key for an unbound row that no other row of the collection has; the row's own if it has one already.
uint16_t UnboundKey(const MappingCollection& aCollection, const Mapping& aRow)
{
    auto used = [&](uint16_t aKey) {
        for (const auto& other : aCollection.mappings)
        {
            if (&other != &aRow && other.key == aKey)
                return true;
        }
        return false;
    };
    if (IsUnboundKey(aRow.key) && !used(aRow.key))
        return aRow.key;
    for (auto k : kUnboundKeys)
    {
        if (!used(k))
            return k;
    }
    return 0;
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
                // A keyboard entry leaves a controller button that shares its name alone.
                if (pad.contains(original.key) && !entry.pad)
                    continue;
                uint16_t key = entry.original ? original.key : entry.key;
                uint32_t keyFull = entry.original ? original.keyFull : key;
                // Unbound: a key no device sends, one no other row of the mapping has, and no key to show.
                // The game keeps only one of two rows with the same key, so two unbound rows of a mapping
                // (an action's key and its button) lost one until a restart.
                if (!entry.original && key == 0)
                {
                    key = UnboundKey(*collection, mapping);
                    keyFull = 0;
                }
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
    // Starting a new game clears the bindings for a moment, and plugins that apply their keys when a
    // session starts (this one, Mod Settings, RCF) can call in then. The game's function reads the
    // missing bindings and crashes; there is nothing to apply until they are loaded again.
    if (!aManager || !aManager->contextManager || !aManager->contextManager->userMapping)
    {
        std::lock_guard lock(g_mutex);
        g_userMapping = nullptr;
        return;
    }
    {
        std::lock_guard lock(g_mutex);
        g_manager = aManager;
        g_context = aManager->contextManager;
        g_userMapping = g_context->userMapping;
        g_pending = false;
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
    g_mainThread = GetCurrentThreadId();
    g_target = reinterpret_cast<void*>(static_cast<ApplyOverrides_t>(RED4ext::UniversalRelocFunc<ApplyOverrides_t>(kApplyOverrides)));
    return g_target && aSdk->hooking->Attach(aHandle, g_target, reinterpret_cast<void*>(&ApplyOverrides_Hook),
                                             reinterpret_cast<void**>(&g_original));
}

void InputOverrides::Detach(const RED4ext::v1::Sdk* aSdk, RED4ext::v1::PluginHandle aHandle)
{
    if (g_target && g_original)
        aSdk->hooking->Detach(aHandle, g_target);
}

bool InputOverrides::Dropped(const std::string& aName, const std::vector<std::string>& aMappings)
{
    static RED4ext::UniversalRelocFunc<CalcHash_t> calcHash(kCalcHash);
    static RED4ext::UniversalRelocFunc<GetMappingCollection_t> getMappingCollection(kGetMappingCollection);
    auto hash = calcHash(RED4ext::CString(aName.c_str()));
    std::lock_guard lock(g_mutex);
    // Only on the main thread, like Update.
    if (GetCurrentThreadId() != g_mainThread || !g_stable || !BindingsUnchanged())
        return false;
    bool had = false;
    for (const auto& m : aMappings)
    {
        RED4ext::CName mapName(m.c_str());
        // Only mappings that had the binding when it was first applied.
        if (!g_originals.contains({mapName.hash, hash}))
            continue;
        had = true;
        auto collection = getMappingCollection(g_userMapping, mapName);
        if (!collection)
            continue;
        for (const auto& row : collection->mappings)
        {
            if (row.overridable == hash)
                return false;
        }
    }
    return had;
}

#ifdef MKM_TEST
std::vector<std::string> InputOverrides::LiveButtons(const std::string& aMapping)
{
    static RED4ext::UniversalRelocFunc<GetMappingCollection_t> getMappingCollection(kGetMappingCollection);
    std::vector<std::string> out;
    std::lock_guard lock(g_mutex);
    if (!BindingsUnchanged())
        return out;
    auto collection = getMappingCollection(g_userMapping, RED4ext::CName(aMapping.c_str()));
    if (!collection)
        return out;
    for (const auto& m : collection->mappings)
    {
        // ...and the row's raw bytes, for reverse engineering.
        static const char* hex = "0123456789abcdef";
        std::string raw;
        auto bytes = reinterpret_cast<const uint8_t*>(&m);
        for (std::size_t i = 0; i < sizeof(Mapping); ++i)
        {
            raw += hex[bytes[i] >> 4];
            raw += hex[bytes[i] & 15];
        }
        out.push_back(std::to_string(m.key) + "|" + std::to_string(m.keyFull) + "|" + std::to_string(m.overridable) + "|" + raw);
    }
    return out;
}

uint32_t InputOverrides::Hash(const std::string& aName)
{
    static RED4ext::UniversalRelocFunc<CalcHash_t> calcHash(kCalcHash);
    return calcHash(RED4ext::CString(aName.c_str()));
}
#endif

void InputOverrides::Set(const std::vector<Key>& aKeys)
{
    static RED4ext::UniversalRelocFunc<CalcHash_t> calcHash(kCalcHash);
    const auto& byName = InputKeys().byName;

    std::lock_guard lock(g_mutex);
    for (const auto& k : aKeys)
    {
        // A name that is no key (a key combination) means the binding loaded at startup: the
        // game only takes combinations from its input files.
        auto key = byName.find(RED4ext::CName(k.key.c_str()));
        Entry entry{calcHash(RED4ext::CString(k.name.c_str())), {}, 0, key == byName.end(), k.pad};
        if (!entry.original)
            entry.key = key->second;
        for (const auto& m : k.mappings)
            entry.mappings.emplace_back(m.c_str());
        g_entries[k.name] = std::move(entry);
    }
    g_pending = true;
}

void InputOverrides::SetStable(bool aStable)
{
    std::lock_guard lock(g_mutex);
    g_stable = aStable;
}

void InputOverrides::Update()
{
    Manager* manager = nullptr;
    {
        std::lock_guard lock(g_mutex);
        // Not while the game has no bindings loaded or replaced them (a new game): it applies them
        // itself once they are loaded, and the hook sets these keys at that point.
        if (!g_pending || !g_applied || !g_stable || !BindingsUnchanged())
            return;
        g_pending = false;
        manager = g_manager;
    }
    // Through the hooked function, so every plugin that adds overrides takes part.
    reinterpret_cast<ApplyOverrides_t>(g_target)(manager);
}

