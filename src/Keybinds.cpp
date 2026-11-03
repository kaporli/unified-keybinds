#include "Keybinds.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <json.hpp>
#include <pugixml.hpp>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
constexpr const char* kGroup = "/key_bindings/SettingsMods";

struct Button
{
    std::string id;
    std::size_t order;
};

bool IsPad(const std::string& aKey)
{
    return aKey.rfind("IK_Pad", 0) == 0;
}

// overridableUI -> first keyboard button id, in document order.
std::map<std::string, Button> ReadMappings(const fs::path& aFile)
{
    std::map<std::string, Button> out;
    pugi::xml_document doc;
    if (!doc.load_file(aFile.c_str()))
        return out;

    std::size_t order = 0;
    for (auto node : doc.select_nodes("//button[@overridableUI]"))
    {
        auto button = node.node();
        std::string name = button.attribute("overridableUI").as_string();
        std::string id = button.attribute("id").as_string();
        if (name.empty() || id.empty())
            continue;

        auto it = out.find(name);
        if (it == out.end())
            out.emplace(name, Button{id, order++});
        else if (IsPad(it->second.id) && !IsPad(id))
            it->second.id = id;
    }
    return out;
}

std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(), [](unsigned char c) { return std::tolower(c); });
    return aText;
}

std::vector<std::string> Words(const std::string& aText)
{
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty())
            words.push_back(cur);
        cur.clear();
    };

    for (std::size_t i = 0; i < aText.size(); ++i)
    {
        unsigned char c = aText[i];
        if (c == '_' || c == '-' || c == ' ' || c == '.')
        {
            flush();
            continue;
        }
        if (!cur.empty())
        {
            unsigned char prev = aText[i - 1];
            unsigned char next = i + 1 < aText.size() ? aText[i + 1] : 0;
            // "QoL" stays one word: a capital that ends the token is part of it
            bool endsToken = next == 0 || !std::isalpha(next);
            bool lowerToUpper = std::islower(prev) && std::isupper(c) && !endsToken;
            bool acronymEnd = std::isupper(prev) && std::isupper(c) && std::islower(next);
            bool digitEdge = (std::isdigit(prev) != 0) != (std::isdigit(c) != 0);
            if (lowerToUpper || acronymEnd || digitEdge)
                flush();
        }
        cur += static_cast<char>(c);
    }
    flush();
    return words;
}

std::string Join(const std::vector<std::string>& aWords)
{
    std::string out;
    for (const auto& w : aWords)
    {
        if (!out.empty())
            out += ' ';
        out += w;
    }
    return out;
}

std::string Capitalize(std::string aWord)
{
    if (!aWord.empty())
        aWord[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(aWord[0])));
    return aWord;
}

// Load-order prefixes like "#", "zz_" and "z-" say nothing about the mod.
std::string StripOrderPrefix(std::string aName)
{
    while (!aName.empty() && (aName[0] == '#' || aName[0] == '_' || aName[0] == '-' || aName[0] == '!'))
        aName.erase(0, 1);
    auto lower = Lower(aName);
    for (const char* p : {"zzz_", "zz_", "z_", "zzz-", "zz-", "z-"})
    {
        if (lower.rfind(p, 0) == 0)
        {
            aName.erase(0, std::char_traits<char>::length(p));
            break;
        }
    }
    return aName;
}

// Name of the mod that ships an input file: the folder under r6/input or red4ext/plugins,
// or the file name for a loose r6/input file.
std::string SourceName(const fs::path& aRoot, const fs::path& aFile)
{
    // Lexical on purpose: mod managers' virtual file systems resolve canonical paths to
    // the mod's own folder, which would hide where the game sees the file.
    std::vector<std::string> parts;
    auto root = aRoot.begin();
    for (const auto& p : aFile)
    {
        if (root != aRoot.end() && Lower(root->string()) == Lower(p.string()))
        {
            ++root;
            continue;
        }
        parts.push_back(p.string());
    }

    std::string name;
    if (parts.size() >= 3 && Lower(parts[0]) == "r6" && Lower(parts[1]) == "input")
        name = parts.size() == 3 ? aFile.stem().string() : parts[2];
    else if (parts.size() >= 4 && Lower(parts[0]) == "red4ext" && Lower(parts[1]) == "plugins")
        name = parts[2];
    else
        name = aFile.stem().string();

    return Keybinds::Humanize(StripOrderPrefix(name));
}

// overridableUI -> mod name, from every input file a mod ships.
std::map<std::string, std::string> ReadSources(const fs::path& aRoot)
{
    std::map<std::string, std::string> out;
    for (const auto& dir : {aRoot / "r6" / "input", aRoot / "red4ext" / "plugins"})
    {
        std::error_code ec;
        if (!fs::is_directory(dir, ec))
            continue;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec || !it->is_regular_file(ec) || Lower(it->path().extension().string()) != ".xml")
                continue;
            // Full input configs (copies of the game's own) are not one mod's inputs.
            auto file = Lower(it->path().filename().string());
            if (file == "inputusermappings.xml" || file == "inputcontexts.xml")
                continue;
            for (const auto& [name, button] : ReadMappings(it->path()))
                out.emplace(name, SourceName(aRoot, it->path()));
        }
    }
    return out;
}

// Optional labels shipped by mods: r6/config/modkeybinds/*.json, {"overridableUI": "Label"}.
std::map<std::string, std::string> ReadLabelFiles(const fs::path& aRoot, const Keybinds::LogFn& aLog)
{
    std::map<std::string, std::string> out;
    std::error_code ec;
    auto dir = aRoot / "r6" / "config" / "modkeybinds";
    if (!fs::is_directory(dir, ec))
        return out;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (Lower(entry.path().extension().string()) != ".json")
            continue;
        std::ifstream in(entry.path());
        auto data = json::parse(in, nullptr, false);
        if (!data.is_object())
        {
            aLog("Skipped label file (not a JSON object): " + entry.path().filename().string());
            continue;
        }
        for (const auto& [name, label] : data.items())
        {
            if (label.is_string())
                out[name] = label.get<std::string>();
        }
    }
    return out;
}

// Mods often prefix their action names with the mod's initials (ADE_, SKP_, cmm...).
std::string ActionName(const std::string& aName, const std::string& aSource)
{
    auto words = Words(aName);
    auto sourceWords = Words(aSource);
    std::string initials;
    for (const auto& w : sourceWords)
        initials += static_cast<char>(std::tolower(static_cast<unsigned char>(w[0])));

    // Drop a prefix that repeats the mod name: its initials (ADE_) or its first words (fppSpeedo).
    if (words.size() > 1 && Lower(words.front()) == initials)
        words.erase(words.begin());
    std::size_t i = 0;
    while (words.size() > 1 && i < sourceWords.size() && Lower(words.front()) == Lower(sourceWords[i]))
    {
        words.erase(words.begin());
        ++i;
    }
    if (words.size() > 1 && Lower(words.back()) == "key")
        words.pop_back();

    for (auto& w : words)
        w = Capitalize(w);
    return Join(words);
}

std::set<std::string> OptionNames(const json& aOptions)
{
    std::set<std::string> out;
    if (!aOptions.contains("options"))
        return out;
    for (const auto& group : aOptions["options"])
    {
        if (!group.contains("options"))
            continue;
        for (const auto& opt : group["options"])
        {
            if (opt.contains("name") && opt["name"].is_string())
                out.insert(opt["name"].get<std::string>());
        }
    }
    return out;
}
} // namespace

std::string Keybinds::Humanize(const std::string& aText)
{
    auto words = Words(aText);
    for (auto& w : words)
        w = Capitalize(w);
    return Join(words);
}

std::vector<Keybinds::Action> Keybinds::Collect(const fs::path& aRoot, const std::string& aBaseOptions,
                                                const LogFn& aLog)
{
    auto merged = ReadMappings(aRoot / "r6" / "cache" / "inputUserMappings.xml");
    if (merged.empty())
    {
        aLog("No merged input mappings in r6/cache. Is Input Loader installed?");
        return {};
    }

    auto base = ReadMappings(aRoot / "r6" / "config" / "inputUserMappings.xml");
    auto baseOptions = json::parse(aBaseOptions, nullptr, false);
    auto listed = baseOptions.is_discarded() ? std::set<std::string>{} : OptionNames(baseOptions);
    // Drop our own group so a stale copy doesn't hide actions.
    if (!baseOptions.is_discarded() && baseOptions.contains("options"))
    {
        for (const auto& group : baseOptions["options"])
        {
            if (group.value("group_name", "") != kGroup || !group.contains("options"))
                continue;
            for (const auto& opt : group["options"])
                listed.erase(opt.value("name", ""));
        }
    }

    auto sources = ReadSources(aRoot);
    auto labels = ReadLabelFiles(aRoot, aLog);

    std::vector<std::pair<Button, Action>> found;
    for (const auto& [name, button] : merged)
    {
        if (base.contains(name) || listed.contains(name) || IsPad(button.id))
            continue;

        auto src = sources.find(name);
        std::string source = src != sources.end() ? src->second : "";
        std::string label;
        if (auto it = labels.find(name); it != labels.end())
            label = it->second;
        else if (!source.empty())
            label = source + ": " + ActionName(name, source);
        else
            label = Humanize(name);

        found.push_back({button, Action{name, button.id, label}});
    }

    // Group by mod, keep each mod's own order.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        auto sa = a.second.label.substr(0, a.second.label.find(':'));
        auto sb = b.second.label.substr(0, b.second.label.find(':'));
        if (sa != sb)
            return sa < sb;
        return a.first.order < b.first.order;
    });

    std::vector<Action> out;
    for (auto& [button, action] : found)
        out.push_back(std::move(action));
    return out;
}

std::string Keybinds::MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions)
{
    auto data = json::parse(aBaseOptions, nullptr, false);
    if (data.is_discarded() || !data.contains("groups") || !data.contains("options"))
        return {};

    auto& groups = data["groups"];
    auto& options = data["options"];
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [](const json& g) { return g.value("group_name", "") == kGroup; }),
                 groups.end());
    options.erase(std::remove_if(options.begin(), options.end(),
                                 [](const json& g) { return g.value("group_name", "") == kGroup; }),
                  options.end());

    if (aActions.empty())
        return data.dump(4);

    int top = 0;
    for (const auto& g : groups)
    {
        if (g.value("group_name", "").rfind("/key_bindings/", 0) == 0)
            top = std::max(top, g.value("order", 0));
    }
    groups.push_back({{"group_name", kGroup}, {"display_name", "Mods"}, {"order", top + 1}});

    json list = json::array();
    int order = 0;
    for (const auto& a : aActions)
        list.push_back({{"name", a.name}, {"display_name", a.label}, {"order", order++}, {"is_input", true}});
    options.push_back({{"group_name", kGroup}, {"options", list}});

    return data.dump(4);
}
