#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <RED4ext/RED4ext.hpp>

#include "InputOverrides.hpp"
#include "Keybinds.hpp"
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

std::mutex g_writeMutex;
std::unordered_set<HANDLE> g_writeHandles;

std::mutex g_mutex;
bool g_collected = false;
std::vector<Keybinds::Action> g_actions;

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
        actions = Keybinds::Collect(g_root, base, Log);
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
        std::lock_guard lock(g_mutex);
        g_actions = std::move(actions);
        g_collected = true;
        Log(std::to_string(g_actions.size()) + " actions");
        for (const auto& a : g_actions)
            Log("  [" + a.section + "] " + a.name + " = " + a.defaultKey + "  \"" + a.label + "\"");
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
        auto marked = Keybinds::MarkRebindable(aMerged, actions);
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
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        if (UserSettings::Split(g_settingsWrite, g_settings, g_store, Log))
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

BOOL WINAPI CloseHandle_Hook(HANDLE aHandle)
{
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
            value = saved.contains(a.name) ? saved[a.name] : "None";
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
            if (key.rfind("LocKey#", 0) != 0)
                aOut->PushBack(RED4ext::CString((aKeys ? key : Keybinds::SectionText(a)).c_str()));
        }
        auto key = Keybinds::LabelKey(a);
        if (key.rfind("LocKey#", 0) != 0)
            aOut->PushBack(RED4ext::CString((aKeys ? key : a.label).c_str()));
    }
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
                out.push_back({name, a.mappings, keys[i].ToString()});
        }
    }
    InputOverrides::Set(out);
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
            out.push_back({a.name, a.mappings, saved.contains(a.name) ? saved[a.name] : a.defaultKey});
    }
    InputOverrides::Set(out);
}

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

    auto setKeys =
        RED4ext::CGlobalFunction::Create("ModKeybindsMenu_SetKeys", "ModKeybindsMenu_SetKeys", &Native_SetKeys);
    setKeys->flags = flags;
    setKeys->AddParam("array:CName", "names");
    setKeys->AddParam("array:CName", "keys");
    rtti->RegisterFunction(setKeys);

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

        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Main), &self);
        auto scripts = ModulePath(self).parent_path() / "scripts";
        aSdk->scripts->Add(aHandle, scripts.c_str());

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

        auto rtti = RED4ext::CRTTISystem::Get();
        rtti->AddRegisterCallback(RegisterTypes);
        rtti->AddPostRegisterCallback(PostRegisterTypes);
        Log("Loaded, game folder: " + g_root.string());
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
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
    aInfo->version = RED4EXT_V1_SEMVER(1, 1, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
