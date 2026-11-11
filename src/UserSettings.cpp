#include "UserSettings.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
// keys.json: {"action": "IK_...", ..., "_defaults": {"action": "IK_..."}}, the default each key had
// when it was saved.
constexpr const char* kDefaults = "_defaults";
// The game's controller keys per control scheme: {"_schemes": {"Agile": {"MKPad_jump": ..., "_defaults": {...}}}}.
constexpr const char* kSchemes = "_schemes";
constexpr const char* kPadGroup = "/key_bindings/SettingsModPad";

std::string g_scheme; // control scheme in use

bool IsGamePadGroup(const std::string& aGroup)
{
    return aGroup.rfind(kPadGroup, 0) == 0;
}

// Where a key of aGroup is kept: the scheme's part for the game's controller keys, else the store.
nlohmann::ordered_json& Target(nlohmann::ordered_json& aStore, const std::string& aGroup)
{
    if (g_scheme.empty() || !IsGamePadGroup(aGroup))
        return aStore;
    if (!aStore.contains(kSchemes) || !aStore[kSchemes].is_object())
        aStore[kSchemes] = nlohmann::ordered_json::object();
    auto& scheme = aStore[kSchemes][g_scheme];
    if (!scheme.is_object())
        scheme = nlohmann::ordered_json::object();
    return scheme;
}

std::string Read(const fs::path& aPath)
{
    std::ifstream in(aPath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A string member of a JSON object, empty for anything else (value() throws on another type).
std::string Text(const json& aObject, const char* aKey)
{
    if (!aObject.is_object())
        return {};
    auto it = aObject.find(aKey);
    return it != aObject.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// The store's content. A file that exists but can't be read as keys is moved aside (keys.json.bad)
// rather than written over, so a damaged file never costs the player's keys silently.
json ReadStore(const fs::path& aStore)
{
    auto text = Read(aStore);
    auto data = json::parse(text, nullptr, false);
    if (data.is_object())
        return data;
    if (text.find_first_not_of(" \t\r\n") != std::string::npos)
    {
        std::error_code ec;
        auto bad = aStore;
        bad += ".bad";
        fs::copy_file(aStore, bad, fs::copy_options::overwrite_existing, ec);
    }
    return json::object();
}

// Writes the store, keeping the version before it as keys.json.bak.
bool WriteStore(const fs::path& aStore, const json& aData)
{
    std::error_code ec;
    if (fs::exists(aStore, ec))
    {
        auto bak = aStore;
        bak += ".bak";
        fs::copy_file(aStore, bak, fs::copy_options::overwrite_existing, ec);
    }
    return UserSettings::WriteFile(aStore, aData.dump(4));
}

json ReadSettings(const fs::path& aPath)
{
    auto data = json::parse(Read(aPath), nullptr, false);
    if (data.is_object() && data.contains("data") && data["data"].is_array())
        return data;
    return json();
}
} // namespace

bool UserSettings::WriteFile(const fs::path& aPath, const std::string& aText)
{
    std::error_code ec;
    fs::create_directories(aPath.parent_path(), ec);
    // Written next to the file and moved over it, so a crash mid-write leaves the old file whole.
    auto temp = aPath;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << aText;
        out.close();
        if (out && MoveFileExW(temp.c_str(), aPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
    }
    fs::remove(temp, ec);
    // Where the move isn't possible (a file held open, some virtual file systems): written in place.
    std::ofstream out(aPath, std::ios::binary | std::ios::trunc);
    out << aText;
    out.close();
    return static_cast<bool>(out);
}

fs::path UserSettings::Path()
{
    PWSTR local = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        out = fs::path(local) / "CD Projekt Red" / "Cyberpunk 2077" / "UserSettings.json";
    CoTaskMemFree(local);
    return out;
}

bool UserSettings::Split(const fs::path& aSource, const fs::path& aTarget, const fs::path& aStore,
                         const Keybinds::LogFn& aLog, const std::map<std::string, std::string>& aDefaults)
{
    auto settings = ReadSettings(aSource);
    if (settings.is_null())
        return false;

    auto store = ReadStore(aStore);
    auto& groups = settings["data"];
    std::size_t moved = 0;
    for (auto it = groups.begin(); it != groups.end();)
    {
        if (!Keybinds::IsModGroup(Text(*it, "group_name")))
        {
            ++it;
            continue;
        }
        if (it->contains("options") && (*it)["options"].is_array())
        {
            auto& target = Target(store, Text(*it, "group_name"));
            for (const auto& opt : (*it)["options"])
            {
                auto name = Text(opt, "name");
                if (name.empty() || !opt.contains("value") || !opt["value"].is_string())
                    continue;
                target[name] = opt["value"];
                if (auto d = aDefaults.find(name); d != aDefaults.end())
                {
                    if (!target.contains(kDefaults) || !target[kDefaults].is_object())
                        target[kDefaults] = json::object();
                    target[kDefaults][name] = d->second;
                }
            }
        }
        it = groups.erase(it);
        ++moved;
    }

    // Keys first, so they are never lost with the sections.
    if (moved > 0 && !WriteStore(aStore, store))
    {
        aLog("Could not write " + aStore.string());
        return false;
    }
    if (moved == 0 && aSource == aTarget)
        return true;
    if (!WriteFile(aTarget, settings.dump(4)))
    {
        aLog("Could not write " + aTarget.string());
        return false;
    }
    if (moved > 0)
        aLog("Saved settings; " + std::to_string(moved) + " mod sections kept in " + aStore.filename().string());
    return true;
}

std::string UserSettings::WithModSections(const fs::path& aSettings, const fs::path& aStore,
                                          const std::vector<Keybinds::Action>& aActions)
{
    auto settings = ReadSettings(aSettings);
    if (settings.is_null())
        return {};

    auto& groups = settings["data"];
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const json& g) { return Keybinds::IsModGroup(Text(g, "group_name")); }),
                 groups.end());

    auto saved = SavedKeys(aStore);
    std::string current;
    std::size_t index = 0;
    for (const auto& a : aActions)
    {
        auto path = Keybinds::GroupPath(a);
        if (path != current)
        {
            current = path;
            groups.push_back({{"group_name", path}, {"options", json::array()}});
            index = groups.size() - 1;
        }
        auto key = Keybinds::StartKey(a, saved);
        groups[index]["options"].push_back({{"name", a.name}, {"type", "name"}, {"value", key}, {"default_value", a.defaultKey}});
    }
    return settings.dump(4);
}

std::map<std::string, std::string> UserSettings::SavedKeys(const fs::path& aStore)
{
    std::map<std::string, std::string> out;
    // Kept in a variable: items() refers to the json, which a temporary would not outlive.
    auto store = ReadStore(aStore);
    const json none = json::object();
    auto read = [&](const json& aKeys) {
        const auto& defaults = aKeys.contains(kDefaults) && aKeys[kDefaults].is_object() ? aKeys[kDefaults] : none;
        for (const auto& [name, value] : aKeys.items())
        {
            // "None": an entry the game left empty, never set by the player (IK_None is unbound).
            if (!value.is_string() || value == "None" || value == "")
                continue;
            if (auto d = defaults.find(name); d != defaults.end() && *d == value)
                continue;
            out[name] = value.get<std::string>();
        }
    };
    read(store);
    if (!g_scheme.empty() && store.contains(kSchemes) && store[kSchemes].is_object() &&
        store[kSchemes].contains(g_scheme) && store[kSchemes][g_scheme].is_object())
        read(store[kSchemes][g_scheme]);
    return out;
}

void UserSettings::SetScheme(const std::string& aScheme)
{
    g_scheme = aScheme;
}

void UserSettings::MoveToSchemes(const fs::path& aStore, const std::vector<Keybinds::Action>& aActions,
                                 const Keybinds::LogFn& aLog)
{
    auto store = ReadStore(aStore);
    if (g_scheme.empty() || store.empty() || store.contains(kSchemes))
        return;
    bool any = std::any_of(aActions.begin(), aActions.end(), [](const auto& a) { return IsGamePadGroup(a.group); });
    if (!any)
        return;
    static const char* schemes[] = {"Legacy", "Agile", "Alternative"};
    store[kSchemes] = json::object();
    for (const auto* s : schemes)
        store[kSchemes][s] = json::object();
    std::size_t moved = 0;
    for (const auto& a : aActions)
    {
        if (!IsGamePadGroup(a.group) || !store.contains(a.name))
            continue;
        auto value = store[a.name];
        json d;
        if (store.contains(kDefaults) && store[kDefaults].is_object() && store[kDefaults].contains(a.name))
            d = store[kDefaults][a.name];
        for (const auto* s : schemes)
        {
            // A key of an entry that follows the scheme was set for the scheme in use.
            if (a.scheme && g_scheme != s)
                continue;
            store[kSchemes][s][a.name] = value;
            if (!d.is_null())
                store[kSchemes][s][kDefaults][a.name] = d;
        }
        store.erase(a.name);
        if (store.contains(kDefaults) && store[kDefaults].is_object())
            store[kDefaults].erase(a.name);
        ++moved;
    }
    if (!WriteStore(aStore, store))
        aLog("Could not write " + aStore.string());
    else
        aLog("Controller keys kept per control scheme from now on: " + std::to_string(moved) + " moved");
}

bool UserSettings::SchemeEdited(const fs::path& aStore, const std::string& aScheme)
{
    auto store = ReadStore(aStore);
    if (!store.contains(kSchemes) || !store[kSchemes].is_object() || !store[kSchemes].contains(aScheme))
        return false;
    const auto& keys = store[kSchemes][aScheme];
    if (!keys.is_object())
        return false;
    const json none = json::object();
    const auto& defaults = keys.contains(kDefaults) && keys[kDefaults].is_object() ? keys[kDefaults] : none;
    for (const auto& [name, value] : keys.items())
    {
        if (name == kDefaults || !value.is_string() || value == "None" || value == "")
            continue;
        auto d = defaults.find(name);
        if (d == defaults.end() || *d != value)
            return true;
    }
    return false;
}

void UserSettings::DropUnrecordedSchemeKeys(const fs::path& aStore, const std::vector<Keybinds::Action>& aActions,
                                            const Keybinds::LogFn& aLog)
{
    auto store = ReadStore(aStore);
    if (store.empty() || store.contains(kDefaults))
        return;
    std::size_t dropped = 0;
    for (const auto& a : aActions)
    {
        if (a.scheme && store.contains(a.name))
        {
            store.erase(a.name);
            ++dropped;
        }
    }
    store[kDefaults] = json::object();
    if (!WriteStore(aStore, store))
        aLog("Could not write " + aStore.string());
    else if (dropped > 0)
        aLog("Dropped " + std::to_string(dropped) + " controller keys saved by an older version (control scheme)");
}

std::string UserSettings::ListValue(const fs::path& aSettings, const fs::path& aOptions, const std::string& aGroup,
                                    const std::string& aName)
{
    // Both files list groups as {"group_name": ..., "options": [{"name": ...}]}.
    auto find = [&](const json& aGroups) -> const json* {
        if (!aGroups.is_array())
            return nullptr;
        for (const auto& g : aGroups)
        {
            if (Text(g, "group_name") != aGroup || !g.contains("options") || !g["options"].is_array())
                continue;
            for (const auto& opt : g["options"])
            {
                if (Text(opt, "name") == aName)
                    return &opt;
            }
        }
        return nullptr;
    };

    auto settings = ReadSettings(aSettings);
    if (!settings.is_null())
    {
        if (auto opt = find(settings["data"]); opt && opt->contains("value") && (*opt)["value"].is_string())
            return (*opt)["value"].get<std::string>();
    }
    auto options = json::parse(Read(aOptions), nullptr, false);
    if (options.is_object() && options.contains("options"))
    {
        auto opt = find(options["options"]);
        if (opt && opt->contains("values") && (*opt)["values"].is_array())
        {
            auto found = opt->find("default_index");
            auto index = found != opt->end() && found->is_number_integer() ? found->get<int>() : 0;
            const auto& values = (*opt)["values"];
            if (index >= 0 && index < static_cast<int>(values.size()) && values[index].is_string())
                return values[index].get<std::string>();
        }
    }
    return {};
}
