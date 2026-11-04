#include "Rcf.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <json.hpp>

#include "InputKeyNames.hpp"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
struct Key
{
    std::string modId;
    std::string section;
    std::string labelKey;
    std::string defaultKey;
    std::string current;
    bool pad = false;
    std::size_t order = 0;
};

json ReadJson(const fs::path& aPath)
{
    std::ifstream in(aPath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    auto data = json::parse(ss.str(), nullptr, false);
    return data.is_object() ? data : json::object();
}

// RCF stores keys as EInputKey values.
std::map<int64_t, std::string> KeyNames()
{
    std::map<int64_t, std::string> out;
    for (const auto& [value, name] : InputKeyNames::kAll)
        out.emplace(value, name);
    return out;
}

std::string KeyName(const std::map<int64_t, std::string>& aNames, const json& aValue)
{
    if (!aValue.is_number_integer())
        return {};
    auto it = aNames.find(aValue.get<int64_t>());
    return it != aNames.end() ? it->second : std::string{};
}

// "DVCore.Category.ChromePlating" -> "Chrome Plating"; plain text stays as it is.
std::string SectionName(const std::string& aText)
{
    if (aText.find(' ') != std::string::npos || aText.find('.') == std::string::npos)
        return aText;
    return Keybinds::Humanize(aText.substr(aText.rfind('.') + 1));
}

// A label without spaces is a translation key the mod registers.
bool IsTranslationKey(const std::string& aText)
{
    return !aText.empty() && aText.find(' ') == std::string::npos && aText.find('.') != std::string::npos;
}

// Keys from every page RCF has seen, by key name (the overridableUI they push to).
std::map<std::string, Key> ReadKeys(const fs::path& aRoot, const std::map<int64_t, std::string>& aNames,
                                    const Keybinds::LogFn& aLog)
{
    std::map<std::string, Key> out;
    auto dir = aRoot / "r6" / "storages" / "RedscriptConfigFramework";
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return out;

    std::size_t order = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        auto file = entry.path().filename().string();
        const std::string suffix = ".cached.json";
        if (file.size() <= suffix.size() || file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        auto page = ReadJson(entry.path());
        std::string modId = page.value("modId", "");
        if (modId.empty() || !page.contains("tabs") || !page["tabs"].is_array())
            continue;
        auto values = ReadJson(dir / (modId + ".json"));

        // A page with keys in several sections gets a menu section for each (one per DigitalVixen
        // mod); otherwise the mod's name.
        std::vector<std::pair<std::string, json>> rows;
        std::vector<std::string> sections;
        for (const auto& tab : page["tabs"])
        {
            for (const auto& sec : tab.value("sections", json::array()))
            {
                for (const auto& row : sec.value("rows", json::array()))
                {
                    if (row.value("kind", "") != "keybind" || row.value("key", "").empty())
                        continue;
                    std::string name = sec.value("name", "");
                    rows.push_back({name, row});
                    if (!row.value("pad", false) && std::find(sections.begin(), sections.end(), name) == sections.end())
                        sections.push_back(name);
                }
            }
        }
        for (const auto& [sectionName, row] : rows)
        {
            Key key;
            key.modId = modId;
            key.section = sections.size() > 1 ? SectionName(sectionName) : Keybinds::Humanize(modId);
            key.labelKey = row.value("label", "");
            key.pad = row.value("pad", false);
            key.defaultKey = KeyName(aNames, row.value("default", json(0)));
            std::string name = row["key"].get<std::string>();
            key.current = values.contains(name) ? KeyName(aNames, values[name]) : key.defaultKey;
            if (key.defaultKey.empty() || key.current.empty())
            {
                aLog("RCF: " + modId + "/" + name + " has a key value that is no key, skipped");
                continue;
            }
            key.order = order++;
            out[name] = std::move(key);
        }
    }

    // RCF's own hub key. 0 means its default, F8.
    auto framework = ReadJson(dir / (std::string(Rcf::kFrameworkId) + ".json"));
    auto open = framework.value("openKey", json(0));
    Key key;
    key.modId = Rcf::kFrameworkId;
    key.section = "Redscript Configuration Framework";
    key.labelKey = "DVRCF.opt.openKey";
    key.defaultKey = "IK_F8";
    key.current = open.is_number_integer() && open.get<int64_t>() > 0 ? KeyName(aNames, open) : key.defaultKey;
    key.order = order++;
    if (!key.current.empty())
        out["dvrcfOpenKey"] = std::move(key);
    return out;
}
} // namespace

void Rcf::Apply(const fs::path& aRoot, std::vector<Keybinds::Action>& aActions, const Keybinds::LogFn& aLog)
{
    std::map<std::string, Key> keys;
    try
    {
        keys = ReadKeys(aRoot, KeyNames(), aLog);
    }
    catch (const std::exception& e)
    {
        aLog(std::string("RCF: could not read its settings, RCF keys are left out: ") + e.what());
    }

    std::vector<Keybinds::Action> kept;
    std::vector<std::pair<std::size_t, Keybinds::Action>> fromRcf;
    std::size_t dropped = 0;
    for (auto& a : aActions)
    {
        auto it = keys.find(a.name);
        if (it == keys.end())
        {
            if (a.placeholder)
                ++dropped;
            else
                kept.push_back(std::move(a));
            continue;
        }
        const auto& key = it->second;
        if (key.pad)
            continue;
        a.modId = key.modId;
        a.section = key.section;
        if (IsTranslationKey(key.labelKey))
            a.labelKey = key.labelKey;
        else if (!key.labelKey.empty())
            a.label = key.labelKey;
        a.defaultKey = key.defaultKey;
        a.current = key.current;
        a.combo.clear();
        fromRcf.push_back({key.order, std::move(a)});
    }
    std::stable_sort(fromRcf.begin(), fromRcf.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& [order, a] : fromRcf)
        kept.push_back(std::move(a));

    if (!fromRcf.empty() || dropped > 0)
    {
        aLog("RCF: " + std::to_string(fromRcf.size()) + " keys, " + std::to_string(dropped) +
             " placeholder keys left out");
    }
    aActions = std::move(kept);
}
