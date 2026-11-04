#include "UserSettings.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <fstream>
#include <sstream>

#include <json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
std::string Read(const fs::path& aPath)
{
    std::ifstream in(aPath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool Write(const fs::path& aPath, const std::string& aText)
{
    std::error_code ec;
    fs::create_directories(aPath.parent_path(), ec);
    std::ofstream out(aPath, std::ios::binary | std::ios::trunc);
    out << aText;
    out.close();
    return static_cast<bool>(out);
}

json ReadStore(const fs::path& aStore)
{
    auto data = json::parse(Read(aStore), nullptr, false);
    return data.is_object() ? data : json::object();
}

json ReadSettings(const fs::path& aPath)
{
    auto data = json::parse(Read(aPath), nullptr, false);
    if (data.is_object() && data.contains("data") && data["data"].is_array())
        return data;
    return json();
}
} // namespace

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
                         const Keybinds::LogFn& aLog)
{
    auto settings = ReadSettings(aSource);
    if (settings.is_null())
        return false;

    auto store = ReadStore(aStore);
    auto& groups = settings["data"];
    std::size_t moved = 0;
    for (auto it = groups.begin(); it != groups.end();)
    {
        if (!Keybinds::IsModGroup(it->value("group_name", "")))
        {
            ++it;
            continue;
        }
        if (it->contains("options"))
        {
            for (const auto& opt : (*it)["options"])
            {
                if (opt.contains("name") && opt.contains("value") && opt["value"].is_string())
                    store[opt["name"].get<std::string>()] = opt["value"];
            }
        }
        it = groups.erase(it);
        ++moved;
    }

    // Keys first, so they are never lost with the sections.
    if (moved > 0 && !Write(aStore, store.dump(4)))
    {
        aLog("Could not write " + aStore.string());
        return false;
    }
    if (moved == 0 && aSource == aTarget)
        return true;
    if (!Write(aTarget, settings.dump(4)))
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
                                [](const json& g) { return Keybinds::IsModGroup(g.value("group_name", "")); }),
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
        auto key = saved.contains(a.name) ? saved[a.name] : !a.current.empty() ? a.current : a.defaultKey;
        groups[index]["options"].push_back({{"name", a.name}, {"type", "name"}, {"value", key}, {"default_value", a.defaultKey}});
    }
    return settings.dump(4);
}

std::map<std::string, std::string> UserSettings::SavedKeys(const fs::path& aStore)
{
    std::map<std::string, std::string> out;
    // Kept in a variable: items() refers to the json, which a temporary would not outlive.
    auto store = ReadStore(aStore);
    for (const auto& [name, value] : store.items())
    {
        if (value.is_string())
            out[name] = value.get<std::string>();
    }
    return out;
}
