#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <atomic>
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
std::vector<Keybinds::HoldOnly> g_holdOnly; // hold-only actions the session started with
std::string g_scheme;     // controller scheme at startup (Legacy, Agile, Alternative)
bool g_vanillaPads = false; // the optional Vanilla Controller file is installed

std::mutex g_writeMutex;
std::unordered_set<HANDLE> g_writeHandles;

std::mutex g_mutex;
bool g_collected = false;
std::vector<Keybinds::Action> g_actions;
std::map<std::string, std::string> g_loaded;    // key each action got at startup
std::map<std::string, std::string> g_announced; // combination changes the player was told about

thread_local bool t_busy = false;

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

bool IsOptionsFile(LPCWSTR aPath)
{
    if (!aPath)
        return false;
    std::wstring p(aPath);
    for (auto& c : p)
        c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
    const std::wstring tail = L"\\r6\\config\\settings\\platform\\pc\\options.json";
    return p.size() >= tail.size() && p.compare(p.size() - tail.size(), tail.size(), tail) == 0;
}

// Builds the merged options.json next to the Input Loader cache. Returns false to let the
// game read its own file.
bool Generate(const fs::path& aBase)
{
    auto base = ReadFile(aBase);
    if (base.empty())
    {
        Log("Could not read " + aBase.string());
        return false;
    }

    std::vector<Keybinds::Action> actions;
    {
        std::lock_guard lock(g_mutex);
        if (g_collected)
            actions = g_actions;
    }
    bool fresh = actions.empty();
    if (fresh)
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
    }
    auto merged = Keybinds::MergeOptions(base, actions);
    if (merged.empty())
    {
        Log("options.json has an unexpected layout, leaving it alone");
        return false;
    }

    std::error_code ec;
    fs::create_directories(g_generated.parent_path(), ec);
    std::ofstream out(g_generated, std::ios::binary | std::ios::trunc);
    out << merged;
    out.close();
    if (!out)
    {
        Log("Could not write " + g_generated.string());
        return false;
    }

    if (fresh)
    {
        UserSettings::DropUnrecordedSchemeKeys(g_store, actions, Log);
        std::lock_guard lock(g_mutex);
        g_actions = std::move(actions);
        g_collected = true;
        Log(std::to_string(g_actions.size()) + " actions");
        for (const auto& a : g_actions)
        {
            std::string mappings;
            for (const auto& m : a.mappings)
                mappings += (mappings.empty() ? "" : ",") + m;
            Log("  [" + a.section + "] " + a.name + " = " + a.defaultKey + "  \"" + a.label + "\"" +
                (a.pad ? " (controller: " + mappings + (a.scheme ? ", scheme" : "") + ")" : ""));
        }
    }
    return true;
}

bool IsMappingsFile(LPCWSTR aPath)
{
    if (!aPath)
        return false;
    std::wstring p(aPath);
    for (auto& c : p)
        c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
    const std::wstring tail = L"\\r6\\cache\\inputusermappings.xml";
    return p.size() >= tail.size() && p.compare(p.size() - tail.size(), tail.size(), tail) == 0;
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
        std::ofstream out(g_mappings, std::ios::binary | std::ios::trunc);
        out << marked;
        out.close();
        ok = static_cast<bool>(out);
        if (!ok)
            Log("Could not write " + g_mappings.string());
    });
    return ok;
}

bool IsContextsFile(LPCWSTR aPath)
{
    if (!aPath)
        return false;
    std::wstring p(aPath);
    for (auto& c : p)
        c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
    const std::wstring tail = L"\\r6\\cache\\inputcontexts.xml";
    return p.size() >= tail.size() && p.compare(p.size() - tail.size(), tail.size(), tail) == 0;
}

// Input Loader's merged contexts with the hold-only actions marked. Returns false to let the game
// read the file as it is. The decisions also go to AcceptedEvents, which keeps the long press to the
// device whose button is shared.
bool GenerateContexts(LPCWSTR aMerged)
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [&] {
        EnsureCollected();
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        g_holdOnly = Keybinds::HoldOnlyActions(Keybinds::WithHoldOptions(actions), UserSettings::SavedKeys(g_store), g_settings);
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
        AcceptedEvents::Set(rules);
        // The menu lets the (Hold) bindings share their key.
        auto shareable = Keybinds::MarkShareable(marked.empty() ? ReadFile(aMerged) : marked,
                                                 Keybinds::ShareableActions(Keybinds::WithHoldOptions(actions)), Log);
        if (!shareable.empty())
            marked = shareable;
        if (marked.empty())
            return;
        std::ofstream out(g_contexts, std::ios::binary | std::ios::trunc);
        out << marked;
        out.close();
        ok = static_cast<bool>(out);
        if (!ok)
            Log("Could not write " + g_contexts.string());
    });
    return ok;
}

std::wstring Normalized(std::wstring aPath)
{
    for (auto& c : aPath)
        c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
    return aPath;
}

// Only the game's own settings file, not other files with the same name.
bool IsUserSettingsFile(LPCWSTR aPath)
{
    static const std::wstring settings = Normalized(g_settings.wstring());
    return aPath && !settings.empty() && Normalized(aPath) == settings;
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
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (UserSettings::Split(g_settingsWrite, g_settings, g_store, Log, defaults))
        {
            std::error_code ec;
            fs::remove(g_settingsWrite, ec);
            return;
        }
        Sleep(25);
    }
    Log("Could not split the settings the game wrote (" + g_settingsWrite.string() +
        "), will retry at next launch");
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
    if (!t_busy && IsUserSettingsFile(aName))
    {
        if (aAccess & (GENERIC_WRITE | FILE_WRITE_DATA))
        {
            // Split into UserSettings.json and keys.json when the game closes the file.
            HANDLE h = g_createFileW(g_settingsWrite.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
            if (h != INVALID_HANDLE_VALUE)
            {
                std::lock_guard lock(g_writeMutex);
                g_writeHandles.insert(h);
            }
            return h;
        }

        t_busy = true;
        // The game can read its settings before options.json.
        EnsureCollected();
        std::vector<Keybinds::Action> actions;
        {
            std::lock_guard lock(g_mutex);
            actions = g_actions;
        }
        auto merged = UserSettings::WithModSections(aName, g_store, actions);
        bool ok = false;
        if (!merged.empty())
        {
            std::ofstream out(g_settingsRead, std::ios::binary | std::ios::trunc);
            out << merged;
            out.close();
            ok = static_cast<bool>(out);
        }
        t_busy = false;
        if (ok)
            return g_createFileW(g_settingsRead.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
        Log("Could not add the mod sections to " + fs::path(aName).string() + ", the game reads it as it is");
    }

    if (!t_busy && !(aAccess & (GENERIC_WRITE | FILE_WRITE_DATA)) && IsContextsFile(aName))
    {
        t_busy = true;
        bool ok = GenerateContexts(aName);
        t_busy = false;
        if (ok)
            return g_createFileW(g_contexts.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
    }

    if (!t_busy && !(aAccess & (GENERIC_WRITE | FILE_WRITE_DATA)) && IsMappingsFile(aName))
    {
        t_busy = true;
        bool ok = GenerateMappings(aName);
        t_busy = false;
        if (ok)
        {
            return g_createFileW(g_mappings.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
        }
    }

    if (!t_busy && !(aAccess & (GENERIC_WRITE | FILE_WRITE_DATA)) && IsOptionsFile(aName))
    {
        t_busy = true;
        bool ok = Generate(aName);
        t_busy = false;
        if (ok)
        {
            Log("Game opened options.json, serving the merged copy");
            return g_createFileW(g_generated.c_str(), aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
        }
    }
    return g_createFileW(aName, aAccess, aShare, aSec, aDisp, aFlags, aTemplate);
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
    if (!t_busy)
    {
        std::lock_guard lock(g_writeMutex);
        lastWrite = g_writeHandles.erase(aHandle) > 0 && g_writeHandles.empty();
    }
    BOOL result = g_closeHandle(aHandle);
    if (lastWrite)
    {
        t_busy = true;
        SplitWrittenSettings();
        t_busy = false;
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
    bool busy = t_busy;
    t_busy = true;
    Generate(g_root / "r6" / "config" / "settings" / "platform" / "pc" / "options.json");
    t_busy = busy;
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
    std::lock_guard lock(g_mutex);
    for (uint32_t i = 0; i < names.size() && i < keys.size(); ++i)
    {
        std::string name = names[i].ToString();
        for (const auto& a : g_actions)
        {
            if (a.name == name)
                out.push_back({name, a.mappings, Keybinds::Fits(a, keys[i].ToString()) ? keys[i].ToString() : a.defaultKey, a.pad});
        }
    }
    InputOverrides::Set(out);
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
        }
    }
    InputOverrides::Set(out);
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

// True when the keys the menu holds change a key combination compared to startup, or bind an entry
// whose binding the game dropped (those only apply after a restart), and the player wasn't told
// about these changes yet.
void Native_RestartNotice(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, bool* aOut, int64_t)
{
    RED4ext::DynArray<RED4ext::CName> names;
    RED4ext::DynArray<RED4ext::CName> keys;
    RED4ext::GetParameter(aFrame, &names);
    RED4ext::GetParameter(aFrame, &keys);
    aFrame->code++;

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
            // A controller entry without a default button has no row until it has a saved button.
            bool noRow = action->pad && action->defaultKey == Keybinds::kUnbound && loaded->second == Keybinds::kUnbound;
            if (IsCombo(*action, key) || IsCombo(*action, loaded->second) || noRow)
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
        auto now = Keybinds::HoldOnlyActions(Keybinds::WithHoldOptions(actions), current, g_settings);
        auto same = now.size() == g_holdOnly.size() &&
                    std::equal(now.begin(), now.end(), g_holdOnly.begin(), [](const auto& a, const auto& b) {
                        return a.action == b.action && a.controller == b.controller && a.keyboard == b.keyboard &&
                               a.padMappings == b.padMappings && a.keyOptions == b.keyOptions;
                    });
        if (!same)
        {
            std::string text;
            for (const auto& h : now)
                text += h.action + (h.controller ? "c" : "") + (h.keyboard ? "k" : "") + ";";
            pending["holdOnly"] = text;
        }
    }
    bool notice = !pending.empty() && pending != g_announced;
    g_announced = pending;
    if (aOut)
        *aOut = notice;
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

void RegisterTypes()
{
}

void PostRegisterTypes()
{
    auto rtti = RED4ext::CRTTISystem::Get();
    RED4ext::CBaseFunction::Flags flags = {.isNative = true, .isStatic = true};

    auto names = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_Names", "ModKeybindsMenu_Names", &Native_Names);
    names->flags = flags;
    names->SetReturnType("array:CName");
    rtti->RegisterFunction(names);

    auto defaults =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_Defaults", "ModKeybindsMenu_Defaults", &Native_Defaults);
    defaults->flags = flags;
    defaults->SetReturnType("array:CName");
    rtti->RegisterFunction(defaults);

    auto groups =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_Groups", "ModKeybindsMenu_Groups", &Native_Groups);
    groups->flags = flags;
    groups->SetReturnType("array:CName");
    rtti->RegisterFunction(groups);

    auto saved = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_Saved", "ModKeybindsMenu_Saved", &Native_Saved);
    saved->flags = flags;
    saved->SetReturnType("array:CName");
    rtti->RegisterFunction(saved);

    auto keys = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_LabelKeys", "ModKeybindsMenu_LabelKeys",
                                                 &Native_LabelKeys);
    keys->flags = flags;
    keys->SetReturnType("array:String");
    rtti->RegisterFunction(keys);

    auto texts = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_LabelTexts", "ModKeybindsMenu_LabelTexts",
                                                  &Native_LabelTexts);
    texts->flags = flags;
    texts->SetReturnType("array:String");
    rtti->RegisterFunction(texts);

    auto padNames =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_PadNames", "ModKeybindsMenu_PadNames", &Native_PadNames);
    padNames->flags = flags;
    padNames->SetReturnType("array:CName");
    rtti->RegisterFunction(padNames);


    auto holdShare = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_HoldShareNames", "ModKeybindsMenu_HoldShareNames",
                                                      &Native_HoldShareNames);
    holdShare->flags = flags;
    holdShare->SetReturnType("array:CName");
    rtti->RegisterFunction(holdShare);

    auto padScheme =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_PadScheme", "ModKeybindsMenu_PadScheme", &Native_PadScheme);
    padScheme->flags = flags;
    padScheme->SetReturnType("CName");
    rtti->RegisterFunction(padScheme);

    auto schemeNotice = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_SchemeNotice", "ModKeybindsMenu_SchemeNotice",
                                                         &Native_SchemeNotice);
    schemeNotice->flags = flags;
    schemeNotice->AddParam("String", "scheme");
    schemeNotice->SetReturnType("Bool");
    rtti->RegisterFunction(schemeNotice);

    auto rcfMods =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_RcfMods", "ModKeybindsMenu_RcfMods", &Native_RcfMods);
    rcfMods->flags = flags;
    rcfMods->SetReturnType("array:String");
    rtti->RegisterFunction(rcfMods);

    auto setKeys =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_SetKeys", "ModKeybindsMenu_SetKeys", &Native_SetKeys);
    setKeys->flags = flags;
    setKeys->AddParam("array:CName", "names");
    setKeys->AddParam("array:CName", "keys");
    rtti->RegisterFunction(setKeys);

    auto setStable =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_SetStable", "ModKeybindsMenu_SetStable", &Native_SetStable);
    setStable->flags = flags;
    setStable->AddParam("Bool", "stable");
    rtti->RegisterFunction(setStable);

    auto comboKeys =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_ComboKeys", "ModKeybindsMenu_ComboKeys", &Native_ComboKeys);
    comboKeys->flags = flags;
    comboKeys->AddParam("CName", "value");
    comboKeys->SetReturnType("array:CName");
    rtti->RegisterFunction(comboKeys);

    auto restartNotice = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_RestartNotice", "ModKeybindsMenu_RestartNotice",
                                                          &Native_RestartNotice);
    restartNotice->flags = flags;
    restartNotice->AddParam("array:CName", "names");
    restartNotice->AddParam("array:CName", "keys");
    restartNotice->SetReturnType("Bool");
    rtti->RegisterFunction(restartNotice);

    auto watchModifier = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_WatchModifier", "ModKeybindsMenu_WatchModifier",
                                                          &Native_WatchModifier);
    watchModifier->flags = flags;
    rtti->RegisterFunction(watchModifier);

    auto keyCombo =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_KeyCombo", "ModKeybindsMenu_KeyCombo", &Native_KeyCombo);
    keyCombo->flags = flags;
    keyCombo->SetReturnType("String");
    rtti->RegisterFunction(keyCombo);

#ifdef MKM_TEST
    auto testLists =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_TestLists", "ModKeybindsMenu_TestLists", &Native_TestLists);
    testLists->flags = flags;
    testLists->AddParam("Int32", "list");
    testLists->AddParam("Int32", "size");
    testLists->SetReturnType("String");
    rtti->RegisterFunction(testLists);
#endif

    auto setDevice =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_SetDevice", "ModKeybindsMenu_SetDevice", &Native_SetDevice);
    setDevice->flags = flags;
    setDevice->AddParam("Bool", "pad");
    rtti->RegisterFunction(setDevice);

    auto watchPad =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_WatchPad", "ModKeybindsMenu_WatchPad", &Native_WatchPad);
    watchPad->flags = flags;
    rtti->RegisterFunction(watchPad);

    auto padCombo =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_PadCombo", "ModKeybindsMenu_PadCombo", &Native_PadCombo);
    padCombo->flags = flags;
    padCombo->SetReturnType("String");
    rtti->RegisterFunction(padCombo);

    auto nativeSettings = RED4ext::CGlobalFunction::Create(
        "ModKeybindsMenu_SetNativeSettings", "ModKeybindsMenu_SetNativeSettings", &Native_SetNativeSettings);
    nativeSettings->flags = flags;
    for (auto param : {"names", "sections", "labels", "keys", "defaults"})
        nativeSettings->AddParam("array:String", param);
    rtti->RegisterFunction(nativeSettings);

#ifdef MKM_TEST
    auto testMapping = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_TestMapping", "ModKeybindsMenu_TestMapping",
                                                        &Native_TestMapping);
    testMapping->flags = flags;
    testMapping->AddParam("String", "mapping");
    testMapping->SetReturnType("array:String");
    rtti->RegisterFunction(testMapping);
    auto testHash =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_TestHash", "ModKeybindsMenu_TestHash", &Native_TestHash);
    testHash->flags = flags;
    testHash->AddParam("String", "name");
    testHash->SetReturnType("Int32");
    rtti->RegisterFunction(testHash);

    auto testScan = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_TestScan", "ModKeybindsMenu_TestScan", &Native_TestScan);
    testScan->flags = flags;
    testScan->AddParam("String", "name");
    testScan->SetReturnType("Int32");
    rtti->RegisterFunction(testScan);
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

        // A write the game made right before a crash may not have been split yet.
        if (fs::exists(g_settingsWrite, ec) && fs::exists(g_settings, ec) &&
            fs::last_write_time(g_settingsWrite, ec) > fs::last_write_time(g_settings, ec))
        {
            SplitWrittenSettings();
        }
        fs::remove(g_settingsWrite, ec);

        // Version 1.0.0 saved the mod sections in UserSettings.json: move them out.
        UserSettings::Split(g_settings, g_settings, g_store, Log);
        g_scheme = UserSettings::ListValue(g_settings, g_root / "r6" / "config" / "settings" / "options.json",
                                           "/controls/controller", "InputSchemesMode");

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
        rtti->AddRegisterCallback(RegisterTypes);
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
    aInfo->version = RED4EXT_V1_SEMVER(1, 7, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
