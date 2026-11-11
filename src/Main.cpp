#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <RED4ext/RED4ext.hpp>

#include "AcceptedEvents.hpp"
#include "InputKeyNames.hpp"
#include "InputOverrides.hpp"
#include "Keybinds.hpp"
#include "NativeSettings.hpp"
#include "Rcf.hpp"
#include "UserSettings.hpp"

namespace fs = std::filesystem;

namespace
{
RED4ext::v1::PluginHandle g_handle = nullptr;
const RED4ext::v1::Sdk* g_sdk = nullptr;

fs::path g_root;      // game folder
fs::path g_generated; // r6/cache/modkeybinds/options.json
fs::path g_store;     // r6/cache/modkeybinds/keys.json, mod keys kept out of UserSettings.json
fs::path g_settings;  // the game's UserSettings.json
fs::path g_settingsRead;  // what the game reads: UserSettings.json with the mod sections
fs::path g_settingsWrite; // what the game writes, split back into UserSettings.json and keys.json
fs::path g_mappings;      // what the game reads: Input Loader's merged mappings, single keys made rebindable
fs::path g_contexts;      // what the game reads: Input Loader's merged contexts, hold-only actions added
fs::path g_merged;        // Input Loader's merged contexts, as the game opened them
std::vector<Keybinds::HoldOnly> g_holdOnly; // hold-only actions the session started with
std::vector<Keybinds::EventRule> g_rules;   // the accepted event rules applied
std::string g_scheme;     // controller scheme at startup (Legacy, Agile, Alternative)
bool g_vanillaPads = false; // the optional Vanilla Controller file is installed

std::mutex g_writeMutex;
std::unordered_set<HANDLE> g_writeHandles;
std::atomic<int> g_writeCount = 0; // size of g_writeHandles, read on every CloseHandle

std::recursive_mutex g_collectMutex; // one thread collects the actions and builds the copies
std::atomic<bool> g_optionsFailed = false; // the options copy could not be made: the game reads its own files

std::mutex g_mutex;
bool g_collected = false;
std::vector<Keybinds::Action> g_actions;
std::map<std::string, std::string> g_loaded;    // key each action got at startup
std::map<std::string, std::string> g_current;   // key each action has now
bool g_holdAll = false;   // the (Hold) actions have their hold rule also while unshared, see HoldOnlyActions
std::vector<Keybinds::HoldOnly> g_holdLive; // controller sharing as the buttons are now, see UpdateHoldOnly
bool g_crouchTap = false; // Dynamic scheme: crouch's controller events follow its button, see UpdateCrouchTap
std::map<std::string, std::string> g_announced; // combination changes the player was told about

thread_local bool t_busy = false;

// While alive, files this thread opens are not redirected by the plugin's own file hook.
struct Busy
{
    bool was = t_busy;
    Busy() { t_busy = true; }
    ~Busy() { t_busy = was; }
};

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileW_t g_createFileW = nullptr;

using CloseHandle_t = BOOL(WINAPI*)(HANDLE);
CloseHandle_t g_closeHandle = nullptr;

void Log(const std::string& aMessage)
{
    if (g_sdk)
        g_sdk->logger->Info(g_handle, aMessage.c_str());
}

fs::path ModulePath(HMODULE aModule)
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD len = GetModuleFileNameW(aModule, buf.data(), static_cast<DWORD>(buf.size()));
        if (len < buf.size())
        {
            buf.resize(len);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

std::string ReadFile(const fs::path& aPath)
{
    std::ifstream in(aPath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// True when aPath ends with aTail (lower case, backslashes), whatever its case and slashes.
bool EndsWith(LPCWSTR aPath, std::wstring_view aTail)
{
    if (!aPath)
        return false;
    auto length = wcslen(aPath);
    if (length < aTail.size())
        return false;
    auto at = aPath + (length - aTail.size());
    for (auto c : aTail)
    {
        wchar_t p = *at++;
        if ((p == L'/' ? L'\\' : static_cast<wchar_t>(towlower(p))) != c)
            return false;
    }
    return true;
}

bool IsOptionsFile(LPCWSTR aPath)
{
    return EndsWith(aPath, L"\\r6\\config\\settings\\platform\\pc\\options.json");
}

bool IsMappingsFile(LPCWSTR aPath)
{
    return EndsWith(aPath, L"\\r6\\cache\\inputusermappings.xml");
}

bool IsContextsFile(LPCWSTR aPath)
{
    return EndsWith(aPath, L"\\r6\\cache\\inputcontexts.xml");
}

// Builds the merged options.json next to the Input Loader cache. Returns false to let the
// game read its own file.
bool Generate(const fs::path& aBase)
{
    std::lock_guard collecting(g_collectMutex);
    g_optionsFailed = true;
    auto base = ReadFile(aBase);
    if (base.empty())
    {
        Log("Could not read " + aBase.string());
        return false;
    }

    bool collected = false;
    {
        std::lock_guard lock(g_mutex);
        collected = g_collected;
    }
    if (!collected)
    {
        std::vector<Keybinds::Action> actions;
        try
        {
            actions = Keybinds::Collect(g_root, base, g_scheme, g_vanillaPads, Log);
            Rcf::Apply(g_root, actions, Log);
            for (auto& a : NativeSettings::Read(g_root))
            {
                if (std::none_of(actions.begin(), actions.end(), [&](const auto& b) { return b.name == a.name; }))
                    actions.push_back(std::move(a));
            }
            // One run per section, also when a mod has keys in both. Vanilla controller sections first.
            std::stable_sort(actions.begin(), actions.end(), [](const auto& a, const auto& b) {
                bool va = !a.group.empty(), vb = !b.group.empty();
                if (va || vb)
                    return va && !vb;
                return a.section < b.section;
            });
            UserSettings::DropUnrecordedSchemeKeys(g_store, actions, Log);
            UserSettings::MoveToSchemes(g_store, actions, Log);
        }
        catch (const std::exception& e)
        {
            Log(std::string("Could not collect the mod actions (") + e.what() + "), the menu stays as the game made it");
            actions.clear();
        }
        Log(std::to_string(actions.size()) + " actions");
        for (const auto& a : actions)
        {
            std::string mappings;
            for (const auto& m : a.mappings)
                mappings += (mappings.empty() ? "" : ",") + m;
            Log("  [" + a.section + "] " + a.name + " = " + a.defaultKey + "  \"" + a.label + "\"" +
                (a.pad ? " (controller: " + mappings + (a.scheme ? ", scheme" : "") + ")" : ""));
        }
        // Kept also when the copy below can't be made: collecting again would not help.
        std::lock_guard lock(g_mutex);
        g_actions = std::move(actions);
        g_collected = true;
    }

    // The game opens options.json several times per launch: the copy is written once per content.
    static std::string builtFrom;
    if (builtFrom != base)
    {
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        std::string merged;
        try
        {
            merged = Keybinds::MergeOptions(base, actions);
        }
        catch (const std::exception&)
        {
        }
        if (merged.empty())
        {
            Log("options.json has an unexpected layout, leaving it alone");
            return false;
        }
        if (!UserSettings::WriteFile(g_generated, merged))
        {
            Log("Could not write " + g_generated.string());
            return false;
        }
        builtFrom = base;
    }
    g_optionsFailed = false;
    return true;
}

void EnsureCollected();

// Input Loader's merged mappings with the single-key mod mappings marked rebindable. Returns
// false to let the game read the file as it is.
bool GenerateMappings(LPCWSTR aMerged)
{
    // The game reads the file several times per launch; Input Loader writes it once.
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [&] {
        EnsureCollected();
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        // The game doesn't apply a keyboard key that another binding uses: a (Hold) binding sharing
        // its key gets it in the copy instead.
        std::map<std::string, std::string> holdKeys;
        auto game = Keybinds::GameKeys(g_settings);
        for (const auto& a : Keybinds::WithHoldOptions(actions))
        {
            if (!a.holdShare)
                continue;
            for (const auto& [group, options] : game)
            {
                auto key = options.find(a.holdOption);
                if (key != options.end() && key->second.rfind("IK_", 0) == 0 && key->second != Keybinds::kUnbound)
                    holdKeys[a.holdOption] = key->second;
            }
        }
        auto marked = Keybinds::MarkRebindable(aMerged, actions, UserSettings::SavedKeys(g_store), holdKeys, Log);
        if (marked.empty())
            return;
        ok = UserSettings::WriteFile(g_mappings, marked);
        if (!ok)
            Log("Could not write " + g_mappings.string());
    });
    return ok;
}

// The Dynamic scheme crouches on a hold, so B can dodge on a tap. With crouch on a button of its own
// that hold has no use: crouch then reacts to a tap, as on the other schemes.
void UpdateCrouchTap()
{
    bool tap = false;
    {
        std::lock_guard lock(g_mutex);
        if (!g_crouchTap)
            return;
        auto crouch = g_current.find(std::string(Keybinds::kPadPrefix) + "crouchToggle");
        auto dodge = g_current.find(std::string(Keybinds::kPadPrefix) + "dodgeDash");
        tap = crouch != g_current.end() && dodge != g_current.end() && crouch->second != dodge->second &&
              crouch->second.rfind("IK_Pad", 0) == 0 && crouch->second.find('+') == std::string::npos;
    }
    // On the dodge's button: the hold the game itself sets for this scheme (not the input file's list,
    // which also takes the press).
    AcceptedEvents::SetController("ToggleCrouch", tap ? Keybinds::EventMode::Tap : Keybinds::EventMode::Hold);
}

// A (Hold) action starts on a long press on the controller while its button is shared, and on the
// press otherwise: follows the buttons as they are now. (Keyboard keys and the actions that give up
// their hold are set when the game starts.)
void UpdateHoldOnly()
{
    std::vector<Keybinds::Action> actions;
    std::map<std::string, std::string> current;
    {
        std::lock_guard lock(g_mutex);
        if (!g_holdAll)
            return;
        actions = Keybinds::WithHoldOptions(g_actions);
        current = g_current;
    }
    // No settings file: only the controller side is needed here, and it comes from the entries.
    auto live = Keybinds::HoldOnlyActions(actions, current, {}, true);
    for (const auto& h : live)
    {
        // Only actions set up at startup (one with accepted events of its own was left alone).
        if (std::any_of(g_rules.begin(), g_rules.end(), [&](const auto& r) { return r.action == h.action; }))
            AcceptedEvents::SetController(h.action, h.controller ? Keybinds::EventMode::Keep : Keybinds::EventMode::All);
    }
    std::lock_guard lock(g_mutex);
    g_holdLive = std::move(live);
}

// Input Loader's merged contexts with the hold-only actions marked. Returns false to let the game
// read the file as it is. The decisions also go to AcceptedEvents, which keeps the long press to the
// device whose button is shared.
bool GenerateContexts(LPCWSTR aMerged)
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [&] {
        g_merged = aMerged;
        EnsureCollected();
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        // With the per-device event lists, the hold rule is set up for every (Hold) action that may
        // share a button, so the controller side can follow the buttons during play.
        g_holdAll = AcceptedEvents::Active();
        g_holdOnly = Keybinds::HoldOnlyActions(Keybinds::WithHoldOptions(actions), UserSettings::SavedKeys(g_store), g_settings,
                                               g_holdAll);
#ifdef MKM_TEST
        // Test builds: a file next to the copies turns the hold-only rule off, to compare.
        std::error_code ec;
        if (fs::exists(g_contexts.parent_path() / "test-no-holdonly", ec))
        {
            Log("TEST: hold-only off");
            g_holdOnly.clear();
        }
#endif
        std::vector<Keybinds::EventRule> rules;
        auto marked = Keybinds::MarkHoldOnly(aMerged, g_root / "r6" / "cache" / "inputUserMappings.xml", g_holdOnly,
                                             rules, Log);
        bool crouchTap = g_scheme == "Agile" &&
                         std::none_of(rules.begin(), rules.end(), [](const auto& r) { return r.action == "ToggleCrouch"; }) &&
                         std::any_of(actions.begin(), actions.end(), [](const auto& a) {
                             return a.scheme && a.name == std::string(Keybinds::kPadPrefix) + "crouchToggle";
                         });
        if (crouchTap)
            rules.push_back({"ToggleCrouch", Keybinds::EventMode::Keep, Keybinds::EventMode::Hold});
        AcceptedEvents::Set(rules);
        g_rules = rules;
        {
            std::lock_guard lock(g_mutex);
            g_crouchTap = crouchTap;
        }
        UpdateCrouchTap();
        // The menu lets the (Hold) bindings share their key.
        auto shareable = Keybinds::MarkShareable(marked.empty() ? ReadFile(aMerged) : marked,
                                                 Keybinds::ShareableActions(Keybinds::WithHoldOptions(actions)), Log);
        if (!shareable.empty())
            marked = shareable;
        // Classic scheme: the scripts count the dodge's double tap, see Keybinds::AddClassicDodge.
        if (g_scheme == "Legacy")
        {
            auto dodge = std::find_if(actions.begin(), actions.end(), [](const auto& a) {
                return a.scheme && !a.mappings.empty() && a.name == std::string(Keybinds::kPadPrefix) + "dodgeDash";
            });
            if (dodge != actions.end())
            {
                auto added = Keybinds::AddClassicDodge(marked.empty() ? ReadFile(aMerged) : marked, dodge->mappings.front());
                if (!added.empty())
                    marked = added;
            }
        }
        if (marked.empty())
            return;
        ok = UserSettings::WriteFile(g_contexts, marked);
        if (!ok)
            Log("Could not write " + g_contexts.string());
    });
    return ok;
}

// Only the game's own settings file, not other files with the same name.
bool IsUserSettingsFile(LPCWSTR aPath)
{
    static const std::wstring settings = [] {
        auto path = g_settings.wstring();
        for (auto& c : path)
            c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
        return path;
    }();
    return aPath && !settings.empty() && wcslen(aPath) == settings.size() && EndsWith(aPath, settings);
}

// Moves what the game wrote into UserSettings.json and keys.json. The file can still be
// incomplete right after the handle closes, so this retries briefly.
void SplitWrittenSettings()
{
    std::map<std::string, std::string> defaults;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& a : g_actions)
            defaults[a.name] = a.defaultKey;
    }
    std::error_code exists;
    for (int attempt = 0; attempt < 20 && fs::exists(g_settingsWrite, exists); ++attempt)
    {
        if (UserSettings::Split(g_settingsWrite, g_settings, g_store, Log, defaults))
        {
            std::error_code ec;
            fs::remove(g_settingsWrite, ec);
            return;
        }
        Sleep(25);
    }
    if (!fs::exists(g_settingsWrite, exists))
        return;
    Log("Could not split the settings the game wrote (" + g_settingsWrite.string() +
        "), will retry at next launch");
}

// The copy to open in place of a file the game reads, or null to open the file itself.
const fs::path* Served(LPCWSTR aName)
{
    if (IsUserSettingsFile(aName))
    {
        // The game can read its settings before options.json.
        EnsureCollected();
        // Mod sections without the options copy that defines them would reset every setting.
        if (g_optionsFailed)
            return nullptr;
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        auto merged = UserSettings::WithModSections(aName, g_store, actions);
        if (!merged.empty() && UserSettings::WriteFile(g_settingsRead, merged))
            return &g_settingsRead;
        Log("Could not add the mod sections to " + fs::path(aName).string() + ", the game reads it as it is");
        return nullptr;
    }
    if (IsContextsFile(aName))
        return GenerateContexts(aName) ? &g_contexts : nullptr;
    if (IsMappingsFile(aName))
        return GenerateMappings(aName) ? &g_mappings : nullptr;
    if (IsOptionsFile(aName) && Generate(aName))
        return &g_generated;
    return nullptr;
}

HANDLE WINAPI CreateFileW_Hook(LPCWSTR aName, DWORD aAccess, DWORD aShare, LPSECURITY_ATTRIBUTES aSec, DWORD aDisp,
                               DWORD aFlags, HANDLE aTemplate)
{
    if (!g_createFileW)
    {
        CREATEFILE2_EXTENDED_PARAMETERS params{sizeof(params)};
        params.dwFileAttributes = aFlags & 0xFFFF;
        params.dwFileFlags = aFlags & 0xFFF00000;
        params.dwSecurityQosFlags = aFlags & 0x001F0000;
        params.lpSecurityAttributes = aSec;
        params.hTemplateFile = aTemplate;
        return CreateFile2(aName, aAccess, aShare, aDisp, &params);
    }
    if (t_busy)
        return g_createFileW(aName, aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
    if (aAccess & (GENERIC_WRITE | FILE_WRITE_DATA))
    {
        if (!IsUserSettingsFile(aName))
            return g_createFileW(aName, aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
        // Split into UserSettings.json and keys.json when the game closes the file.
        HANDLE h = g_createFileW(g_settingsWrite.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
        if (h != INVALID_HANDLE_VALUE)
        {
            std::lock_guard lock(g_writeMutex);
            g_writeHandles.insert(h);
            g_writeCount = static_cast<int>(g_writeHandles.size());
        }
        return h;
    }

    const fs::path* served = nullptr;
    try
    {
        Busy busy;
        served = Served(aName);
    }
    catch (const std::exception& e)
    {
        // Never out of the hook: the game reads its own file.
        Log(std::string("Could not prepare a file the game opened: ") + e.what());
    }
    if (served && IsOptionsFile(aName))
        Log("Game opened options.json, serving the merged copy");
    return g_createFileW(served ? served->c_str() : aName, aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
}

// The hooks can run on another thread before Attach has stored the original function: call the system directly then.
BOOL CloseHandle_Direct(HANDLE aHandle)
{
    using NtClose_t = LONG(NTAPI*)(HANDLE);
    static auto ntClose = reinterpret_cast<NtClose_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtClose"));
    return ntClose && ntClose(aHandle) >= 0;
}

BOOL WINAPI CloseHandle_Hook(HANDLE aHandle)
{
    if (!g_closeHandle)
        return CloseHandle_Direct(aHandle);
    bool lastWrite = false;
    // Nearly every close in the process comes through here: no lock unless a settings write is open.
    if (g_writeCount > 0 && !t_busy)
    {
        std::lock_guard lock(g_writeMutex);
        lastWrite = g_writeHandles.erase(aHandle) > 0 && g_writeHandles.empty();
        g_writeCount = static_cast<int>(g_writeHandles.size());
    }
    BOOL result = g_closeHandle(aHandle);
    if (lastWrite)
    {
        try
        {
            Busy busy;
            SplitWrittenSettings();
        }
        catch (const std::exception& e)
        {
            Log(std::string("Could not split the settings the game wrote: ") + e.what());
        }
    }
    return result;
}

// Scripts may ask before the game read options.json (or if the hook never fired).
void EnsureCollected()
{
    {
        std::lock_guard lock(g_mutex);
        if (g_collected)
            return;
    }
    Busy busy;
    Generate(g_root / "r6" / "config" / "settings" / "platform" / "pc" / "options.json");
}

enum class NameList
{
    Names,
    Defaults,
    Groups,
    Saved
};

void ReturnNames(RED4ext::DynArray<RED4ext::CName>* aOut, NameList aList)
{
    if (!aOut)
        return;
    EnsureCollected();
    auto saved = aList == NameList::Saved ? UserSettings::SavedKeys(g_store) : std::map<std::string, std::string>{};
    std::lock_guard lock(g_mutex);
    for (const auto& a : g_actions)
    {
        std::string value;
        switch (aList)
        {
        case NameList::Names: value = a.name; break;
        case NameList::Defaults: value = a.defaultKey; break;
        case NameList::Groups: value = Keybinds::GroupPath(a); break;
        case NameList::Saved:
            value = !a.modId.empty() || saved.contains(a.name) || !a.current.empty() ? Keybinds::StartKey(a, saved)
                                                                                    : "None";
            break;
        }
        aOut->PushBack(RED4ext::CName(value.c_str()));
    }
}

// Translation keys or their texts, in the same order: each section header, then its actions.
// Entries a mod gave as LocKey# are left out, the game already knows them.
void ReturnStrings(RED4ext::DynArray<RED4ext::CString>* aOut, bool aKeys)
{
    if (!aOut)
        return;
    EnsureCollected();
    std::lock_guard lock(g_mutex);
    std::string section;
    for (const auto& a : g_actions)
    {
        if (a.section != section)
        {
            section = a.section;
            auto key = Keybinds::SectionKey(a);
            if (key.rfind("LocKey#", 0) != 0 && a.sectionKey.empty())
                aOut->PushBack(RED4ext::CString((aKeys ? key : Keybinds::SectionText(a)).c_str()));
        }
        auto key = Keybinds::LabelKey(a);
        if (key.rfind("LocKey#", 0) != 0 && a.labelKey.empty())
            aOut->PushBack(RED4ext::CString((aKeys ? key : a.label).c_str()));
    }
}

// Names of the controller entries.
void Native_PadNames(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                     int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    EnsureCollected();
    std::lock_guard lock(g_mutex);
    for (const auto& a : g_actions)
    {
        if (a.pad)
            aOut->PushBack(RED4ext::CName(a.name.c_str()));
    }
}

// Entries whose key may be shared with another action, since they then start on a long press: the
// controller entries of the game's (Hold) bindings and those bindings' own keyboard options.
void Native_HoldShareNames(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                           int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    EnsureCollected();
    std::lock_guard lock(g_mutex);
    for (const auto& a : Keybinds::WithHoldOptions(g_actions))
    {
        if (!a.holdShare)
            continue;
        aOut->PushBack(RED4ext::CName(a.name.c_str()));
        aOut->PushBack(RED4ext::CName(a.holdOption.c_str()));
    }
}

// For each entry (names, keys, settings groups, at its default key), the names of the other entries
// on the same key that react at the same time, comma separated. See Keybinds::Conflicts.
void Native_Conflicts(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CString>* aOut,
                      int64_t)
{
    RED4ext::DynArray<RED4ext::CName> names;
    RED4ext::DynArray<RED4ext::CName> keys;
    RED4ext::DynArray<RED4ext::CName> groups;
    RED4ext::DynArray<bool> atDefault;
    RED4ext::GetParameter(aFrame, &names);
    RED4ext::GetParameter(aFrame, &keys);
    RED4ext::GetParameter(aFrame, &groups);
    RED4ext::GetParameter(aFrame, &atDefault);
    aFrame->code++;
    if (!aOut)
        return;
    Busy busy;
    EnsureCollected();
    static const auto options = Keybinds::OptionMappings(g_mappings);
    // What the entries of each name bring to the check. The actions are fixed once collected.
    struct Known
    {
        std::vector<std::string> mappings;
        std::string defaultKey;
        bool shareable = false;
    };
    static const auto known = [] {
        std::map<std::string, Known> out;
        std::lock_guard lock(g_mutex);
        for (const auto& a : Keybinds::WithHoldOptions(g_actions))
        {
            auto& k = out[a.name];
            k.mappings.insert(k.mappings.end(), a.mappings.begin(), a.mappings.end());
            if (!a.defaultKey.empty())
                k.defaultKey = a.defaultKey;
            if (a.holdShare)
            {
                k.shareable = true;
                out[a.holdOption].shareable = true;
            }
        }
        return out;
    }();
    std::vector<Keybinds::BindingEntry> entries;
    {
        for (uint32_t i = 0; i < names.size() && i < keys.size(); ++i)
        {
            Keybinds::BindingEntry e;
            e.name = names[i].ToString();
            e.key = keys[i].ToString();
            e.atDefault = i < atDefault.size() && atDefault[i];
            // The game's controller entries have groups of their own (SettingsModPad...).
            std::string group = i < groups.size() ? groups[i].ToString() : std::string();
            e.gamePad = group.rfind("/key_bindings/SettingsModPad", 0) == 0;
            if (Keybinds::IsModGroup(group) && !e.gamePad)
                e.mod = group;
            auto found = options.find(e.name);
            if (found != options.end())
                e.mappings = found->second;
            if (auto k = known.find(e.name); k != known.end())
            {
                e.mappings.insert(e.mappings.end(), k->second.mappings.begin(), k->second.mappings.end());
                // The game has no default for a mod's entry: the mod's key is kept here.
                if (!k->second.defaultKey.empty())
                    e.atDefault = e.key == k->second.defaultKey;
                e.shareable = k->second.shareable;
            }
            entries.push_back(std::move(e));
        }
    }
    auto found = Keybinds::Conflicts(g_merged.empty() ? g_root / "r6" / "cache" / "inputContexts.xml" : g_merged, entries);
    for (const auto& list : found)
    {
        std::string line;
        for (const auto& n : list)
            line += (line.empty() ? "" : ",") + n;
        aOut->PushBack(RED4ext::CString(line.c_str()));
    }
}

// True when the conflicts (aText, as the popup lists them) are not empty and differ from the last
// ones shown, so the popup comes once per change.
void Native_ConflictNotice(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::CString text;
    RED4ext::GetParameter(aFrame, &text);
    aFrame->code++;
    // The first letter is the device view (keyboard or controller), each told on its own.
    static std::map<char, std::string> announced;
    std::string now = text.c_str();
    char view = now.empty() ? ' ' : now[0];
    if (!now.empty())
        now.erase(0, 1);
    bool notice = !now.empty() && now != announced[view];
    announced[view] = now;
    if (aOut)
        *aOut = notice;
}

// Hold-only actions in use, for the prompt check: "action|keyboard|controller|keyboardShared|controllerShared":
// whether it waits for the hold on each device (1/0), and the actions on its shared key per device (comma list).
void Native_HoldRules(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CString>* aOut,
                      int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    Busy busy;
    std::vector<Keybinds::HoldOnly> live;
    {
        std::lock_guard lock(g_mutex);
        live = g_holdLive;
    }
    auto contexts = g_root / "r6" / "cache" / "inputContexts.xml";
    auto join = [](const std::vector<std::string>& aList) {
        std::string out;
        for (const auto& a : aList)
            out += (out.empty() ? "" : ",") + a;
        return out.empty() ? std::string("-") : out;
    };
    for (const auto& h : g_holdOnly)
    {
        for (const auto& r : g_rules)
        {
            if (r.action != h.action)
                continue;
            auto keep = [](Keybinds::EventMode m) { return m == Keybinds::EventMode::Keep ? "1" : "0"; };
            std::string controller = keep(r.controller);
            auto controllerShared = r.controllerShared;
            // The controller side follows the buttons during play.
            auto now = std::find_if(live.begin(), live.end(), [&](const auto& l) { return l.action == h.action; });
            if (g_holdAll && now != live.end())
            {
                controller = now->controller ? "1" : "0";
                controllerShared = Keybinds::FedActions(contexts, now->padMappings);
                std::erase(controllerShared, h.action);
            }
            std::string line = h.action + "|" + keep(r.keyboard) + "|" + controller + "|" + join(r.keyboardShared) + "|" +
                               join(controllerShared);
            aOut->PushBack(RED4ext::CString(line.c_str()));
        }
    }
}

// Hold-only actions giving way to an on-screen prompt on their key, see AcceptedEvents::SetBlocked.
void Native_SetHoldBlocked(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    RED4ext::DynArray<RED4ext::CName> actions;
    RED4ext::GetParameter(aFrame, &actions);
    aFrame->code++;
    std::vector<std::string> list;
    for (const auto& a : actions)
        list.push_back(a.ToString());
    AcceptedEvents::SetBlocked(list);
}

// True when a control scheme's controller keys (aScheme: Legacy, Agile, Alternative) differ from its defaults.
void Native_SchemeEdited(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::CString scheme;
    RED4ext::GetParameter(aFrame, &scheme);
    aFrame->code++;
    if (aOut)
        *aOut = UserSettings::SchemeEdited(g_store, scheme.c_str());
}

// Controller scheme the controller entries were made for.
void Native_PadScheme(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::CName* aOut, int64_t)
{
    aFrame->code++;
    if (aOut)
        *aOut = RED4ext::CName(g_scheme.c_str());
}

// True when aScheme differs from the controller scheme the controller entries were made for, and
// the player wasn't told about it yet.
void Native_SchemeNotice(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::CString scheme;
    RED4ext::GetParameter(aFrame, &scheme);
    aFrame->code++;

    static std::string announced;
    std::string current = scheme.c_str();
    std::lock_guard lock(g_mutex);
    bool follows = std::any_of(g_actions.begin(), g_actions.end(), [](const auto& a) { return a.scheme; });
    bool notice = follows && !g_scheme.empty() && !current.empty() && current != g_scheme && current != announced;
    announced = current == g_scheme ? std::string() : current;
    if (aOut)
        *aOut = notice;
}

// For each action, the RCF mod that keeps its key, else empty.
void Native_RcfMods(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CString>* aOut,
                    int64_t)
{
    aFrame->code++;
    if (!aOut)
        return;
    EnsureCollected();
    std::lock_guard lock(g_mutex);
    for (const auto& a : g_actions)
        aOut->PushBack(RED4ext::CString(a.modId.c_str()));
}

void Native_Names(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                  int64_t)
{
    aFrame->code++;
    ReturnNames(aOut, NameList::Names);
}

void Native_Defaults(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                     int64_t)
{
    aFrame->code++;
    ReturnNames(aOut, NameList::Defaults);
}

void Native_Groups(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                   int64_t)
{
    aFrame->code++;
    ReturnNames(aOut, NameList::Groups);
}

void Native_Saved(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                  int64_t)
{
    aFrame->code++;
    ReturnNames(aOut, NameList::Saved);
}

void Native_LabelKeys(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CString>* aOut,
                      int64_t)
{
    aFrame->code++;
    ReturnStrings(aOut, true);
}

void Native_LabelTexts(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame,
                       RED4ext::DynArray<RED4ext::CString>* aOut, int64_t)
{
    aFrame->code++;
    ReturnStrings(aOut, false);
}

// The keys the menu holds now, for the overrides. Called by the scripts when a session starts
// and when a key changes.
void Native_SetKeys(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    RED4ext::DynArray<RED4ext::CName> names;
    RED4ext::DynArray<RED4ext::CName> keys;
    RED4ext::GetParameter(aFrame, &names);
    RED4ext::GetParameter(aFrame, &keys);
    aFrame->code++;

    EnsureCollected();
    std::vector<InputOverrides::Key> out;
    std::unique_lock lock(g_mutex);
    for (uint32_t i = 0; i < names.size() && i < keys.size(); ++i)
    {
        std::string name = names[i].ToString();
        for (const auto& a : g_actions)
        {
            if (a.name != name)
                continue;
            out.push_back({name, a.mappings, Keybinds::Fits(a, keys[i].ToString()) ? keys[i].ToString() : a.defaultKey, a.pad});
            g_current[name] = out.back().key;
        }
    }
    InputOverrides::Set(out);
    lock.unlock();
    UpdateCrouchTap();
    UpdateHoldOnly();
}

// Keys of a key combination: one a mod binds an action to (its buttonGroup id), or one picked in the
// menu ("modifier+key"). Empty for anything else.
void Native_ComboKeys(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                      int64_t)
{
    RED4ext::CName value;
    RED4ext::GetParameter(aFrame, &value);
    aFrame->code++;
    if (!aOut || value.IsNone())
        return;

    std::string id = value.ToString();
    // A combination picked in the menu: "modifier+key".
    if (auto plus = id.find('+'); plus != std::string::npos)
    {
        aOut->PushBack(RED4ext::CName(id.substr(0, plus).c_str()));
        aOut->PushBack(RED4ext::CName(id.substr(plus + 1).c_str()));
        return;
    }

    EnsureCollected();
    std::lock_guard lock(g_mutex);
    for (const auto& a : g_actions)
    {
        if (a.defaultKey == id && !a.combo.empty())
        {
            for (const auto& k : a.combo)
                aOut->PushBack(RED4ext::CName(k.c_str()));
            return;
        }
    }
}

#ifdef MKM_TEST
// Test builds: set one accepted-events list of the listed actions, see AcceptedEvents::TestLists.
void Native_TestLists(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    int32_t list = -1, size = -1;
    RED4ext::GetParameter(aFrame, &list);
    RED4ext::GetParameter(aFrame, &size);
    aFrame->code++;
    for (const auto& c : AcceptedEvents::TakeChanged())
        Log("Controller-only events: " + c);
    auto text = AcceptedEvents::TestLists(list, size);
    Log("TestLists " + std::to_string(list) + " " + std::to_string(size) + ": " + text);
    if (aOut)
        *aOut = RED4ext::CString(text.c_str());
}
#endif

// The device in use changed (true: a controller), see AcceptedEvents::SetDevice.
void Native_SetDevice(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    bool pad = false;
    RED4ext::GetParameter(aFrame, &pad);
    aFrame->code++;
    AcceptedEvents::SetDevice(pad);
    for (const auto& c : AcceptedEvents::TakeChanged())
        Log("Hold-only: " + c);
}

// False while a session starts or ends, see InputOverrides::SetStable.
void Native_SetStable(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    bool stable = true;
    RED4ext::GetParameter(aFrame, &stable);
    aFrame->code++;
    InputOverrides::SetStable(stable);
}

// Stored keys, for the first time the game applies its bindings.
void SetStoredKeys()
{
    EnsureCollected();
    auto saved = UserSettings::SavedKeys(g_store);
    std::vector<InputOverrides::Key> out;
    {
        std::lock_guard lock(g_mutex);
        for (const auto& a : g_actions)
        {
            out.push_back({a.name, a.mappings, Keybinds::StartKey(a, saved), a.pad});
            g_loaded[a.name] = out.back().key;
            g_current[a.name] = out.back().key;
        }
    }
    InputOverrides::Set(out);
    UpdateCrouchTap();
}

// Key bindings the Lua companion found in Native Settings UI: name, section, label, key, default.
// Stored for the next launch, when they get their menu entries.
void Native_SetNativeSettings(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    std::array<RED4ext::DynArray<RED4ext::CString>, 5> lists;
    for (auto& list : lists)
        RED4ext::GetParameter(aFrame, &list);
    aFrame->code++;

    std::vector<NativeSettings::Entry> entries;
    for (uint32_t i = 0; i < lists[0].size(); ++i)
    {
        bool complete = true;
        for (const auto& list : lists)
            complete = complete && i < list.size();
        if (complete)
            entries.push_back({lists[0][i].c_str(), lists[1][i].c_str(), lists[2][i].c_str(), lists[3][i].c_str(),
                               lists[4][i].c_str()});
    }
    if (!NativeSettings::Write(g_root, entries))
        Log("Could not store the Native Settings UI key bindings");
}

// While an entry waits for a key: two keys pressed together. The menu binds a key only once every
// key is released, too late to ask then.
struct KeyWatch
{
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> running = false;
    std::string keyCombo; // keyboard and mouse: "held+pressed", or empty
    std::string padCombo; // controller: "held+pressed", or empty

    ~KeyWatch()
    {
        running = false;
        if (thread.joinable())
            thread.join();
    }
};
KeyWatch g_watch;

// The game's name of a virtual key, or empty for keys left out: the left mouse button (it picks
// the entry) and the generic Shift, Ctrl and Alt codes (their left and right keys are reported too).
std::string KeyName(int aVk)
{
    switch (aVk)
    {
    case VK_LBUTTON:
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU: return {};
    case VK_XBUTTON1: return "IK_Mouse4";
    case VK_XBUTTON2: return "IK_Mouse5";
    case VK_LMENU:
    case VK_RMENU: return "IK_Alt";
    }
    for (const auto& [value, name] : InputKeyNames::kAll)
    {
        if (value == aVk)
            return std::string_view(name).starts_with("IK_Unknown") ? std::string() : std::string(name);
    }
    return {};
}

void StopWatch()
{
    g_watch.running = false;
    if (g_watch.thread.joinable())
        g_watch.thread.join();
}

// A key pressed while another is held makes the combination (held first). A key pressed and
// released on its own is bound alone.
void StartWatch()
{
    StopWatch();
    {
        std::lock_guard lock(g_watch.mutex);
        g_watch.keyCombo.clear();
    }
    g_watch.running = true;
    g_watch.thread = std::thread([] {
        // Keys already down when the entry started waiting (the click) don't count until pressed again.
        std::array<bool, 256> prev{};
        for (int vk = 1; vk < 256; ++vk)
            prev[vk] = GetAsyncKeyState(vk) & 0x8000;
        int heldVk = 0;
        std::string held;
        for (int i = 0; i < 1000 && g_watch.running; ++i) // 10 s at most
        {
            std::array<bool, 256> down{};
            for (int vk = 1; vk < 256; ++vk)
                down[vk] = GetAsyncKeyState(vk) & 0x8000;
            if (heldVk && !down[heldVk])
            {
                heldVk = 0;
                held.clear();
            }
            for (int vk = 1; vk < 256; ++vk)
            {
                if (!down[vk] || prev[vk])
                    continue;
                auto name = KeyName(vk);
                if (name.empty() || name == held)
                    continue;
                if (!heldVk)
                {
                    heldVk = vk;
                    held = name;
                    continue;
                }
                std::lock_guard lock(g_watch.mutex);
                g_watch.keyCombo = held + "+" + name;
                g_watch.running = false;
                return;
            }
            prev = down;
            Sleep(10);
        }
        g_watch.running = false;
    });
}

// Controller buttons as XInput reports them. Triggers stay out: a trigger is an axis to the game.
const std::pair<WORD, const char*> kPadButtons[] = {
    {XINPUT_GAMEPAD_A, "IK_Pad_A_CROSS"},          {XINPUT_GAMEPAD_B, "IK_Pad_B_CIRCLE"},
    {XINPUT_GAMEPAD_X, "IK_Pad_X_SQUARE"},         {XINPUT_GAMEPAD_Y, "IK_Pad_Y_TRIANGLE"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, "IK_Pad_LeftShoulder"}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, "IK_Pad_RightShoulder"},
    {XINPUT_GAMEPAD_LEFT_THUMB, "IK_Pad_LeftThumb"},       {XINPUT_GAMEPAD_RIGHT_THUMB, "IK_Pad_RightThumb"},
    {XINPUT_GAMEPAD_START, "IK_Pad_Start"},        {XINPUT_GAMEPAD_BACK, "IK_Pad_Back_Select"},
    {XINPUT_GAMEPAD_DPAD_UP, "IK_Pad_DigitUp"},    {XINPUT_GAMEPAD_DPAD_DOWN, "IK_Pad_DigitDown"},
    {XINPUT_GAMEPAD_DPAD_LEFT, "IK_Pad_DigitLeft"}, {XINPUT_GAMEPAD_DPAD_RIGHT, "IK_Pad_DigitRight"}};

// Buttons held on any connected controller. Loaded at run time, so the plugin starts without XInput.
WORD PadButtonsDown()
{
    using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    static auto getState = [] {
        auto dll = LoadLibraryW(L"xinput1_4.dll");
        return dll ? reinterpret_cast<XInputGetState_t>(GetProcAddress(dll, "XInputGetState")) : nullptr;
    }();
    WORD down = 0;
    for (DWORD i = 0; getState && i < XUSER_MAX_COUNT; ++i)
    {
        XINPUT_STATE state{};
        if (getState(i, &state) == ERROR_SUCCESS)
            down |= state.Gamepad.wButtons;
    }
    return down;
}

// While a controller entry waits for a button: a button pressed while another is held makes the
// combination (held first). A button pressed and released on its own is bound alone.
void StartPadWatch()
{
    StopWatch();
    {
        std::lock_guard lock(g_watch.mutex);
        g_watch.padCombo.clear();
    }
    g_watch.running = true;
    g_watch.thread = std::thread([] {
        // Buttons already down when the entry started waiting don't count until pressed again.
        WORD prev = PadButtonsDown();
        WORD heldBit = 0;
        std::string held;
        for (int i = 0; i < 1000 && g_watch.running; ++i) // 10 s at most
        {
            WORD down = PadButtonsDown();
            if (heldBit && !(down & heldBit))
            {
                heldBit = 0;
                held.clear();
            }
            for (const auto& [bit, name] : kPadButtons)
            {
                if (!(down & bit) || (prev & bit))
                    continue;
                if (!heldBit)
                {
                    heldBit = bit;
                    held = name;
                    continue;
                }
                std::lock_guard lock(g_watch.mutex);
                g_watch.padCombo = held + "+" + name;
                g_watch.running = false;
                return;
            }
            prev = down;
            Sleep(10);
        }
        g_watch.running = false;
    });
}

void Native_WatchModifier(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    StartWatch();
}

void Native_WatchPad(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t)
{
    aFrame->code++;
    StartPadWatch();
}

// The controller combination pressed while the entry waited ("held+pressed"), or empty; ends the watch.
// A String: a CName made here would not be in the name pool, so the scripts make it (StringToName).
void Native_PadCombo(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    StopWatch();
    std::lock_guard lock(g_watch.mutex);
    if (aOut)
        *aOut = RED4ext::CString(g_watch.padCombo.c_str());
}

// The keyboard or mouse combination pressed while the entry waited ("held+pressed"), or empty;
// ends the watch. A String, like PadCombo.
void Native_KeyCombo(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::CString* aOut, int64_t)
{
    aFrame->code++;
    StopWatch();
    std::lock_guard lock(g_watch.mutex);
    if (aOut)
        *aOut = RED4ext::CString(g_watch.keyCombo.c_str());
}

bool IsCombo(const Keybinds::Action& aAction, const std::string& aKey)
{
    return aKey.find('+') != std::string::npos || (aKey == aAction.defaultKey && !aAction.combo.empty());
}

// The entries whose change only applies after a restart: a key combination that differs from startup,
// an entry whose binding the game dropped, a (Hold) entry whose sharing changed in a way set up at
// startup. Empty when there are none or the player was already told about exactly these.
void Native_RestartNotice(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CName>* aOut,
                          int64_t)
{
    RED4ext::DynArray<RED4ext::CName> names;
    RED4ext::DynArray<RED4ext::CName> keys;
    RED4ext::GetParameter(aFrame, &names);
    RED4ext::GetParameter(aFrame, &keys);
    aFrame->code++;

    Busy busy;
    EnsureCollected();
    std::map<std::string, std::string> pending;
    {
        std::lock_guard lock(g_mutex);
        for (uint32_t i = 0; i < names.size() && i < keys.size(); ++i)
        {
            std::string name = names[i].ToString();
            std::string key = keys[i].ToString();
            auto loaded = g_loaded.find(name);
            auto action = std::find_if(g_actions.begin(), g_actions.end(), [&](const auto& a) { return a.name == name; });
            if (loaded == g_loaded.end() || action == g_actions.end())
                continue;
            if (key != Keybinds::kUnbound && InputOverrides::Dropped(name, action->mappings))
            {
                pending[name] = key;
                continue;
            }
            if (key == loaded->second)
                continue;
            if (IsCombo(*action, key) || IsCombo(*action, loaded->second))
                pending[name] = key;
        }
    }
    // Hold-only actions follow from shared buttons, read when the game loads its contexts.
    {
        std::vector<Keybinds::Action> actions;
        std::map<std::string, std::string> current;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        for (uint32_t i = 0; i < names.size() && i < keys.size(); ++i)
            current[names[i].ToString()] = keys[i].ToString();
        auto now = Keybinds::HoldOnlyActions(Keybinds::WithHoldOptions(actions), current, g_settings, g_holdAll);
        // Controller buttons follow during play (UpdateHoldOnly), unless a button coming or going
        // belongs to an action that is itself set up for sharing when the game starts.
        static const auto sensitive = Keybinds::HoldSensitiveMappings(g_root / "r6" / "cache" / "inputContexts.xml",
                                                                      Keybinds::ShareableActions(Keybinds::WithHoldOptions(actions)));
        auto padSame = [&](const Keybinds::HoldOnly& a, const Keybinds::HoldOnly& b) {
            if (!g_holdAll)
                return a.controller == b.controller && a.padMappings == b.padMappings;
            for (const auto* side : {&a, &b})
            {
                const auto& other = side == &a ? b : a;
                for (const auto& m : side->padMappings)
                {
                    if (sensitive.contains(m) && std::find(other.padMappings.begin(), other.padMappings.end(), m) == other.padMappings.end())
                        return false;
                }
            }
            return true;
        };
        // The (Hold) entries whose sharing changed since startup: the keyboard entry for a key, the
        // controller entry for a button. Matched by action: either list can have entries the other lacks.
        auto find = [](const std::vector<Keybinds::HoldOnly>& aList, const std::string& aAction) -> const Keybinds::HoldOnly* {
            for (const auto& h : aList)
            {
                if (h.action == aAction)
                    return &h;
            }
            return nullptr;
        };
        auto compare = [&](const Keybinds::HoldOnly& aNamed, const Keybinds::HoldOnly* aWas, const Keybinds::HoldOnly* aNow) {
            Keybinds::HoldOnly none;
            const auto& was = aWas ? *aWas : none;
            const auto& is = aNow ? *aNow : none;
            if (was.keyboard != is.keyboard || was.keyOptions != is.keyOptions)
                pending[aNamed.option] = "shared";
            if (!padSame(was, is))
                pending[aNamed.padEntry] = "shared";
        };
        for (const auto& h : now)
            compare(h, find(g_holdOnly, h.action), &h);
        for (const auto& h : g_holdOnly)
        {
            if (!find(now, h.action))
                compare(h, &h, nullptr);
        }
    }
    bool notice = !pending.empty() && pending != g_announced;
    g_announced = pending;
    if (aOut && notice)
    {
        for (const auto& [name, key] : pending)
            aOut->PushBack(RED4ext::CName(name.c_str()));
    }
}

#ifdef MKM_TEST
// Test builds: live buttons of a mapping, see InputOverrides::LiveButtons.
void Native_TestMapping(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::DynArray<RED4ext::CString>* aOut,
                        int64_t)
{
    RED4ext::CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    if (!aOut)
        return;
    for (const auto& s : InputOverrides::LiveButtons(name.c_str()))
        aOut->PushBack(RED4ext::CString(s.c_str()));
}

bool CopyGuarded(uint8_t* aTo, const uint8_t* aFrom, size_t aSize)
{
    __try
    {
        memcpy(aTo, aFrom, aSize);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Test builds: every 8-byte aligned copy of a CName hash in writable private memory, with 0x100 bytes
// around it, written to red4ext/logs/mkm-scan-<name>.txt. Returns the hit count.
void Native_TestScan(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    uint64_t hash = RED4ext::CName(name.c_str()).hash;
    std::ofstream out(g_root / "red4ext" / "logs" / ("mkm-scan-" + std::string(name.c_str()) + ".txt"));
    out << std::hex << "hash " << hash << "\n";
    int32_t hits = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    for (auto addr = reinterpret_cast<uint8_t*>(0x10000); VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi);
         addr = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize)
    {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || (mbi.Protect & PAGE_GUARD) ||
            !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            continue;
        auto base = static_cast<uint8_t*>(mbi.BaseAddress);
        // Copied first: another thread can free the region while it is read.
        std::vector<uint8_t> copy(mbi.RegionSize);
        if (!CopyGuarded(copy.data(), base, mbi.RegionSize))
            continue;
        auto words = reinterpret_cast<const uint64_t*>(copy.data());
        size_t n = mbi.RegionSize / 8;
        for (size_t i = 0; i < n && hits < 2000; ++i)
        {
            if (words[i] != hash)
                continue;
            ++hits;
            auto cbase = copy.data();
            auto cat = cbase + i * 8;
            auto from = std::max(cbase, cat - 0x80);
            auto to = std::min(cbase + mbi.RegionSize, cat + 0x80);
            out << "hit " << reinterpret_cast<uintptr_t>(base + i * 8) << " off " << (cat - from) << "\n";
            for (auto b = from; b < to; ++b)
            {
                char buf[4];
                snprintf(buf, sizeof(buf), "%02x", *b);
                out << buf << (((b - from) % 32) == 31 ? "\n" : " ");
            }
            out << "\n";
        }
    }
    if (aOut)
        *aOut = hits;
}

void Native_TestHash(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t)
{
    RED4ext::CString name;
    RED4ext::GetParameter(aFrame, &name);
    aFrame->code++;
    if (aOut)
        *aOut = static_cast<int32_t>(InputOverrides::Hash(name.c_str()));
}

#endif

// Registers a native: script name, function, return type (null for none), parameters as type and name.
template <typename Fn>
void Register(const char* aName, Fn aFn, const char* aReturn, std::initializer_list<std::pair<const char*, const char*>> aParams = {})
{
    auto function = RED4ext::CGlobalFunction::Create(aName, aName, aFn);
    function->flags = {.isNative = true, .isStatic = true};
    for (const auto& [type, name] : aParams)
        function->AddParam(type, name);
    if (aReturn)
        function->SetReturnType(aReturn);
    RED4ext::CRTTISystem::Get()->RegisterFunction(function);
}

void PostRegisterTypes()
{
    Register("ModKeybindsMenu_Names", &Native_Names, "array:CName");
    Register("ModKeybindsMenu_Defaults", &Native_Defaults, "array:CName");
    Register("ModKeybindsMenu_Groups", &Native_Groups, "array:CName");
    Register("ModKeybindsMenu_Saved", &Native_Saved, "array:CName");
    Register("ModKeybindsMenu_LabelKeys", &Native_LabelKeys, "array:String");
    Register("ModKeybindsMenu_LabelTexts", &Native_LabelTexts, "array:String");
    Register("ModKeybindsMenu_PadNames", &Native_PadNames, "array:CName");
    Register("ModKeybindsMenu_HoldShareNames", &Native_HoldShareNames, "array:CName");
    Register("ModKeybindsMenu_Conflicts", &Native_Conflicts, "array:String", {{"array:CName", "names"}, {"array:CName", "keys"}, {"array:CName", "groups"}, {"array:Bool", "atDefault"}});
    Register("ModKeybindsMenu_ConflictNotice", &Native_ConflictNotice, "Bool", {{"String", "text"}});
    Register("ModKeybindsMenu_HoldRules", &Native_HoldRules, "array:String");
    Register("ModKeybindsMenu_SetHoldBlocked", &Native_SetHoldBlocked, nullptr, {{"array:CName", "actions"}});
    Register("ModKeybindsMenu_SchemeEdited", &Native_SchemeEdited, "Bool", {{"String", "scheme"}});
    Register("ModKeybindsMenu_PadScheme", &Native_PadScheme, "CName");
    Register("ModKeybindsMenu_SchemeNotice", &Native_SchemeNotice, "Bool", {{"String", "scheme"}});
    Register("ModKeybindsMenu_RcfMods", &Native_RcfMods, "array:String");
    Register("ModKeybindsMenu_SetKeys", &Native_SetKeys, nullptr, {{"array:CName", "names"}, {"array:CName", "keys"}});
    Register("ModKeybindsMenu_SetStable", &Native_SetStable, nullptr, {{"Bool", "stable"}});
    Register("ModKeybindsMenu_ComboKeys", &Native_ComboKeys, "array:CName", {{"CName", "value"}});
    Register("ModKeybindsMenu_RestartNotice", &Native_RestartNotice, "array:CName", {{"array:CName", "names"}, {"array:CName", "keys"}});
    Register("ModKeybindsMenu_WatchModifier", &Native_WatchModifier, nullptr);
    Register("ModKeybindsMenu_KeyCombo", &Native_KeyCombo, "String");
#ifdef MKM_TEST
    Register("ModKeybindsMenu_TestLists", &Native_TestLists, "String", {{"Int32", "list"}, {"Int32", "size"}});
#endif
    Register("ModKeybindsMenu_SetDevice", &Native_SetDevice, nullptr, {{"Bool", "pad"}});
    Register("ModKeybindsMenu_WatchPad", &Native_WatchPad, nullptr);
    Register("ModKeybindsMenu_PadCombo", &Native_PadCombo, "String");
    Register("ModKeybindsMenu_SetNativeSettings", &Native_SetNativeSettings, nullptr, {{"array:String", "names"}, {"array:String", "sections"}, {"array:String", "labels"}, {"array:String", "keys"}, {"array:String", "defaults"}});
#ifdef MKM_TEST
    Register("ModKeybindsMenu_TestMapping", &Native_TestMapping, "array:String", {{"String", "mapping"}});
    Register("ModKeybindsMenu_TestHash", &Native_TestHash, "Int32", {{"String", "name"}});
    Register("ModKeybindsMenu_TestScan", &Native_TestScan, "Int32", {{"String", "name"}});
#endif
    SetStoredKeys();
}
} // namespace

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        g_handle = aHandle;
        g_sdk = aSdk;
        g_root = ModulePath(nullptr).parent_path().parent_path().parent_path();
        g_generated = g_root / "r6" / "cache" / "modkeybinds" / "options.json";
        g_store = g_root / "r6" / "cache" / "modkeybinds" / "keys.json";

        g_settings = UserSettings::Path();
        g_settingsRead = g_root / "r6" / "cache" / "modkeybinds" / "UserSettings.read.json";
        g_settingsWrite = g_root / "r6" / "cache" / "modkeybinds" / "UserSettings.write.json";
        g_mappings = g_root / "r6" / "cache" / "modkeybinds" / "inputUserMappings.xml";
        g_contexts = g_root / "r6" / "cache" / "modkeybinds" / "inputContexts.xml";
        std::error_code ec;
        fs::create_directories(g_generated.parent_path(), ec);
        // A write the game made right before a crash may not have been split yet. Its controller keys
        // belong to the scheme that session started with (the one in the settings file), so it is split
        // under that scheme, and the scheme for this session is read afterwards.
        auto scheme = [] {
            return UserSettings::ListValue(g_settings, g_root / "r6" / "config" / "settings" / "options.json",
                                           "/controls/controller", "InputSchemesMode");
        };
        try
        {
            bool pending = fs::exists(g_settingsWrite, ec) && fs::exists(g_settings, ec) &&
                           fs::last_write_time(g_settingsWrite, ec) > fs::last_write_time(g_settings, ec);
            UserSettings::SetScheme(scheme());
            // One try: a write cut short by the crash can't be read, and the settings file stays as it is.
            if (pending && !UserSettings::Split(g_settingsWrite, g_settings, g_store, Log))
                Log("The settings write of the last session is incomplete, kept the settings file as it is");
            fs::remove(g_settingsWrite, ec);

            // Version 1.0.0 saved the mod sections in UserSettings.json: move them out.
            UserSettings::Split(g_settings, g_settings, g_store, Log);
            g_scheme = scheme();
            UserSettings::SetScheme(g_scheme);
        }
        catch (const std::exception& e)
        {
            Log(std::string("Could not read the saved settings: ") + e.what());
        }

        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Main), &self);
        auto scripts = ModulePath(self).parent_path() / "scripts";
        aSdk->scripts->Add(aHandle, scripts.c_str());
        // The optional file adds the vanilla controller entries' script (the Control Scheme diagram).
        g_vanillaPads = fs::exists(scripts / "ModKeybindsMenu" / "VanillaController.reds", ec);

        auto target = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW");
        auto close = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CloseHandle");
        // Both or neither: without the CloseHandle hook, writes could not be split.
        if (!target || !close ||
            !aSdk->hooking->Attach(aHandle, reinterpret_cast<void*>(close), reinterpret_cast<void*>(&CloseHandle_Hook),
                                   reinterpret_cast<void**>(&g_closeHandle)))
        {
            Log("Could not hook CloseHandle, the mod sections will not appear");
        }
        else if (!aSdk->hooking->Attach(aHandle, reinterpret_cast<void*>(target),
                                        reinterpret_cast<void*>(&CreateFileW_Hook),
                                        reinterpret_cast<void**>(&g_createFileW)))
        {
            Log("Could not hook CreateFileW, the mod sections will not appear");
        }

        if (!InputOverrides::Attach(aSdk, aHandle))
            Log("Could not hook the game's key binding overrides, mod keys changed in the menu will not apply");
        if (!AcceptedEvents::Attach(aSdk, aHandle))
            Log("Could not hook the game's input event lists (other game version?), hold-only controller buttons apply to the keyboard too");
        // Keys changed in a session are applied on the main thread, once per frame.
        static RED4ext::v1::GameState running{nullptr, [](RED4ext::CGameApplication*) {
                                                  InputOverrides::Update();
                                                  return false;
                                              },
                                              nullptr};
        if (!aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &running))
            Log("Could not add the per-frame update, mod keys changed in the menu will not apply");

        auto rtti = RED4ext::CRTTISystem::Get();
        rtti->AddPostRegisterCallback(PostRegisterTypes);
        Log("Loaded, game folder: " + g_root.string());
        Log("Controller scheme: " + (g_scheme.empty() ? std::string("unknown") : g_scheme));
        Log(std::string("Vanilla controller entries: ") + (g_vanillaPads ? "on" : "off"));
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        StopWatch();
        InputOverrides::Detach(aSdk, aHandle);
        AcceptedEvents::Detach(aSdk, aHandle);
        auto target = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW");
        if (target && g_createFileW)
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(target));
        auto close = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CloseHandle");
        if (close && g_closeHandle)
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(close));
        break;
    }
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"Mod Keybinds Menu";
    aInfo->author = L"Elias";
    aInfo->version = RED4EXT_V1_SEMVER(1, 8, 1);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
