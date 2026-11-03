#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <RED4ext/RED4ext.hpp>

#include "Keybinds.hpp"

namespace fs = std::filesystem;

namespace
{
RED4ext::v1::PluginHandle g_handle = nullptr;
const RED4ext::v1::Sdk* g_sdk = nullptr;

fs::path g_root;      // game folder
fs::path g_generated; // r6/cache/modkeybinds/options.json

std::mutex g_mutex;
bool g_collected = false;
std::vector<Keybinds::Action> g_actions;

thread_local bool t_busy = false;

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileW_t g_createFileW = nullptr;

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

    auto actions = Keybinds::Collect(g_root, base, Log);
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

HANDLE WINAPI CreateFileW_Hook(LPCWSTR aName, DWORD aAccess, DWORD aShare, LPSECURITY_ATTRIBUTES aSec, DWORD aDisp,
                               DWORD aFlags, HANDLE aTemplate)
{
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

// Scripts may ask before the game read options.json (or if the hook never fired).
void EnsureCollected()
{
    {
        std::lock_guard lock(g_mutex);
        if (g_collected)
            return;
    }
    t_busy = true;
    Generate(g_root / "r6" / "config" / "settings" / "platform" / "pc" / "options.json");
    t_busy = false;
}

enum class NameList
{
    Names,
    Defaults,
    Groups
};

void ReturnNames(RED4ext::DynArray<RED4ext::CName>* aOut, NameList aList)
{
    if (!aOut)
        return;
    EnsureCollected();
    std::lock_guard lock(g_mutex);
    for (const auto& a : g_actions)
    {
        std::string value = aList == NameList::Names      ? a.name
                            : aList == NameList::Defaults ? a.defaultKey
                                                          : Keybinds::GroupPath(a);
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

        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Main), &self);
        auto scripts = ModulePath(self).parent_path() / "scripts";
        aSdk->scripts->Add(aHandle, scripts.c_str());

        auto target = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW");
        if (!target || !aSdk->hooking->Attach(aHandle, reinterpret_cast<void*>(target),
                                              reinterpret_cast<void*>(&CreateFileW_Hook),
                                              reinterpret_cast<void**>(&g_createFileW)))
        {
            Log("Could not hook CreateFileW, the Mods group will not appear");
        }

        auto rtti = RED4ext::CRTTISystem::Get();
        rtti->AddRegisterCallback(RegisterTypes);
        rtti->AddPostRegisterCallback(PostRegisterTypes);
        Log("Loaded, game folder: " + g_root.string());
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        auto target = GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "CreateFileW");
        if (target && g_createFileW)
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(target));
        break;
    }
    }
    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"Mod Keybinds Menu";
    aInfo->author = L"Elias";
    aInfo->version = RED4EXT_V1_SEMVER(1, 0, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
