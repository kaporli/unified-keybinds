#include <Windows.h>

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
        // Combinations picked in the menu ("modifier+key"), from the stored keys.
        std::map<std::string, std::string> combos;
        for (const auto& [name, key] : UserSettings::SavedKeys(g_store))
        {
            if (key.find('+') != std::string::npos)
                combos[name] = key;
        }
        auto marked = Keybinds::MarkRebindable(aMerged, actions, combos, Log);
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

// While an entry waits for a key: the modifier (Shift, Ctrl, Alt) that was held when another key
// went down. The menu binds a key only once every key is released, too late to ask then.
struct ModifierWatch
{
    std::mutex mutex;
    std::thread thread;
    std::atomic<bool> running = false;
    std::string modifier; // result
};
ModifierWatch g_watch;

const std::pair<int, const char*> kModifierKeys[] = {{VK_LCONTROL, "IK_LControl"}, {VK_RCONTROL, "IK_RControl"},
                                                    {VK_LSHIFT, "IK_LShift"},     {VK_RSHIFT, "IK_RShift"},
                                                    {VK_LMENU, "IK_Alt"},         {VK_RMENU, "IK_Alt"}};

bool IsModifierVk(int aVk)
{
    if (aVk == VK_SHIFT || aVk == VK_CONTROL || aVk == VK_MENU)
        return true;
    for (const auto& [vk, name] : kModifierKeys)
    {
        if (vk == aVk)
            return true;
    }
    return false;
}

void StopWatch()
{
    g_watch.running = false;
    if (g_watch.thread.joinable())
        g_watch.thread.join();
}

void StartWatch()
{
    StopWatch();
    {
        std::lock_guard lock(g_watch.mutex);
        g_watch.modifier.clear();
    }
    g_watch.running = true;
    g_watch.thread = std::thread([] {
        // Keys already down when the entry started waiting (the click) don't count.
        std::array<bool, 256> wasDown{};
        for (int vk = 1; vk < 256; ++vk)
            wasDown[vk] = GetAsyncKeyState(vk) & 0x8000;
        for (int i = 0; i < 1000 && g_watch.running; ++i) // 10 s at most
        {
            std::string held;
            for (const auto& [vk, name] : kModifierKeys)
            {
                if (GetAsyncKeyState(vk) & 0x8000)
                {
                    held = name;
                    break;
                }
            }
            for (int vk = 1; vk < 256; ++vk)
            {
                bool down = GetAsyncKeyState(vk) & 0x8000;
                if (down && !wasDown[vk] && !IsModifierVk(vk))
                {
                    std::lock_guard lock(g_watch.mutex);
                    g_watch.modifier = held;
                    g_watch.running = false;
                    return;
                }
                wasDown[vk] = down;
            }
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

// The modifier held when the bound key went down, or None; ends the watch.
void Native_HeldModifier(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, RED4ext::CName* aOut, int64_t)
{
    aFrame->code++;
    StopWatch();
    std::lock_guard lock(g_watch.mutex);
    if (aOut)
        *aOut = g_watch.modifier.empty() ? RED4ext::CName() : RED4ext::CName(g_watch.modifier.c_str());
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
            if (IsCombo(*action, key) || IsCombo(*action, loaded->second))
                pending[name] = key;
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

    auto heldModifier = RED4ext::CGlobalFunction::Create("ModKeybindsMenu_HeldModifier", "ModKeybindsMenu_HeldModifier",
                                                         &Native_HeldModifier);
    heldModifier->flags = flags;
    heldModifier->SetReturnType("CName");
    rtti->RegisterFunction(heldModifier);

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
    aInfo->version = RED4EXT_V1_SEMVER(1, 6, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
