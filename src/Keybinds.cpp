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
// Every group this mod adds starts with this, so old copies are easy to find and replace.
constexpr const char* kGroupPrefix = "/key_bindings/SettingsMod";

struct Button
{
    std::string id;
    std::size_t order;
    std::vector<std::string> mappings; // mappings that carry the action
};

bool IsPad(const std::string& aKey)
{
    return aKey.rfind("IK_Pad", 0) == 0 || aKey.rfind("IK_Joy", 0) == 0;
}

// The only keyboard or mouse button of a Button mapping that has no overridableUI, or an
// empty node. Key combos (buttonGroup ids) don't count, the menu holds one key per action.
pugi::xml_node SingleKey(pugi::xml_node aMapping, const std::set<std::string>& aGroups)
{
    if (std::string(aMapping.attribute("type").as_string("Button")) != "Button" || !*aMapping.attribute("name").as_string())
        return {};
    pugi::xml_node key;
    for (auto button : aMapping.children("button"))
    {
        if (button.attribute("overridableUI"))
            return {};
        std::string id = button.attribute("id").as_string();
        if (id.rfind("IK_", 0) != 0 || IsPad(id) || aGroups.contains(id))
            continue;
        if (key)
            return {};
        key = button;
    }
    return key;
}

std::set<std::string> GroupIds(const pugi::xml_document& aDoc)
{
    std::set<std::string> out;
    for (auto group : aDoc.child("bindings").children("buttonGroup"))
        out.insert(group.attribute("id").as_string());
    return out;
}

// overridableUI -> first keyboard button id, in document order. A mapping with a single key
// and no overridableUI is listed by its own name, it gets one in the copy the game reads.
std::map<std::string, Button> ReadMappings(const fs::path& aFile)
{
    std::map<std::string, Button> out;
    pugi::xml_document doc;
    if (!doc.load_file(aFile.c_str()))
        return out;

    auto groups = GroupIds(doc);
    std::size_t order = 0;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        if (auto key = SingleKey(mapping, groups))
        {
            std::string name = mapping.attribute("name").as_string();
            out.emplace(name, Button{key.attribute("id").as_string(), order++, {name}});
            continue;
        }
        for (auto button : mapping.children("button"))
        {
            std::string name = button.attribute("overridableUI").as_string();
            std::string id = button.attribute("id").as_string();
            if (name.empty() || id.empty())
                continue;

            auto it = out.find(name);
            if (it == out.end())
                it = out.emplace(name, Button{id, order++}).first;
            else if (IsPad(it->second.id) && !IsPad(id))
                it->second.id = id;
            std::string mapName = mapping.attribute("name").as_string();
            if (std::find(it->second.mappings.begin(), it->second.mappings.end(), mapName) == it->second.mappings.end())
                it->second.mappings.push_back(mapName);
        }
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

struct LabelFiles
{
    std::map<std::string, std::string> labels;   // overridableUI -> action label
    std::map<std::string, std::string> sections; // overridableUI -> section (mod) name
};

// Optional files shipped by mods: r6/config/modkeybinds/*.json,
// {"_section": "Mod Name", "overridableUI": "Action label", ...}. "_section" names the
// menu section of every action listed in that file.
LabelFiles ReadLabelFiles(const fs::path& aRoot, const Keybinds::LogFn& aLog)
{
    LabelFiles out;
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
        std::string section = data.value("_section", "");
        for (const auto& [name, label] : data.items())
        {
            if (name == "_section" || !label.is_string())
                continue;
            out.labels[name] = label.get<std::string>();
            if (!section.empty())
                out.sections[name] = section;
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

// Option names in the base options.json, leaving out our own groups so a stale copy
// doesn't hide actions.
std::set<std::string> OptionNames(const json& aOptions)
{
    std::set<std::string> out;
    if (!aOptions.contains("options"))
        return out;
    for (const auto& group : aOptions["options"])
    {
        if (!group.contains("options") || Keybinds::IsModGroup(group.value("group_name", "")))
            continue;
        for (const auto& opt : group["options"])
        {
            if (opt.contains("name") && opt["name"].is_string())
                out.insert(opt["name"].get<std::string>());
        }
    }
    return out;
}

// Letters and digits only, for use in a group path and a translation key.
std::string Slug(const std::string& aText)
{
    std::string out;
    for (unsigned char c : aText)
    {
        if (std::isalnum(c))
            out += static_cast<char>(c);
    }
    return out.empty() ? "Other" : out;
}
} // namespace

bool Keybinds::IsModGroup(const std::string& aGroup)
{
    return aGroup.rfind(kGroupPrefix, 0) == 0;
}

std::string Keybinds::LabelKey(const Action& aAction)
{
    // Labels a mod gave as LocKey# are already translation keys.
    if (aAction.label.rfind("LocKey#", 0) == 0)
        return aAction.label;
    return kLabelKeyPrefix + aAction.name;
}

std::string Keybinds::GroupPath(const Action& aAction)
{
    return kGroupPrefix + Slug(aAction.section);
}

std::string Keybinds::SectionKey(const Action& aAction)
{
    if (aAction.section.rfind("LocKey#", 0) == 0)
        return aAction.section;
    return std::string(kLabelKeyPrefix) + "Section-" + Slug(aAction.section);
}

std::string Keybinds::SectionText(const Action& aAction)
{
    return "Mod: " + aAction.section;
}

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

    auto sources = ReadSources(aRoot);
    auto files = ReadLabelFiles(aRoot, aLog);

    std::vector<std::pair<Button, Action>> found;
    for (const auto& [name, button] : merged)
    {
        if (base.contains(name) || listed.contains(name) || IsPad(button.id))
            continue;

        Action action{name, button.id};
        action.mappings = button.mappings;
        if (auto it = files.sections.find(name); it != files.sections.end())
            action.section = it->second;
        else if (auto src = sources.find(name); src != sources.end())
            action.section = src->second;
        else
            action.section = "Other";

        if (auto it = files.labels.find(name); it != files.labels.end())
            action.label = it->second;
        else if (action.section != "Other")
            action.label = ActionName(name, action.section);
        else
            action.label = Humanize(name);

        found.push_back({button, std::move(action)});
    }

    // One section per mod, sorted by name; actions keep their mod's own order.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.second.section != b.second.section)
            return a.second.section < b.second.section;
        return a.first.order < b.first.order;
    });

    std::vector<Action> out;
    for (auto& [button, action] : found)
        out.push_back(std::move(action));
    return out;
}

std::string Keybinds::MarkRebindable(const fs::path& aMerged, const std::vector<Action>& aActions)
{
    pugi::xml_document doc;
    if (!doc.load_file(aMerged.c_str()))
        return {};

    std::set<std::string> names;
    for (const auto& a : aActions)
        names.insert(a.name);

    auto groups = GroupIds(doc);
    bool changed = false;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        auto key = SingleKey(mapping, groups);
        if (key && names.contains(mapping.attribute("name").as_string()))
        {
            key.append_attribute("overridableUI") = mapping.attribute("name").value();
            changed = true;
        }
    }
    if (!changed)
        return {};

    std::ostringstream out;
    doc.save(out, "    ");
    return out.str();
}

std::string Keybinds::MergeOptions(const std::string& aBaseOptions, const std::vector<Action>& aActions)
{
    auto data = json::parse(aBaseOptions, nullptr, false);
    if (data.is_discarded() || !data.contains("groups") || !data.contains("options"))
        return {};

    auto& groups = data["groups"];
    auto& options = data["options"];
    auto ours = [](const json& g) { return IsModGroup(g.value("group_name", "")); };
    groups.erase(std::remove_if(groups.begin(), groups.end(), ours), groups.end());
    options.erase(std::remove_if(options.begin(), options.end(), ours), options.end());

    int order = 0;
    for (const auto& g : groups)
    {
        if (g.value("group_name", "").rfind("/key_bindings/", 0) == 0)
            order = std::max(order, g.value("order", 0));
    }

    // Actions arrive sorted by section, so each section is one run.
    std::string current;
    std::size_t list = 0;
    int index = 0;
    for (const auto& a : aActions)
    {
        auto path = GroupPath(a);
        if (path != current)
        {
            current = path;
            groups.push_back({{"group_name", path}, {"display_name", SectionKey(a)}, {"order", ++order}});
            options.push_back({{"group_name", path}, {"options", json::array()}});
            list = options.size() - 1;
            index = 0;
        }
        options[list]["options"].push_back(
            {{"name", a.name}, {"display_name", LabelKey(a)}, {"order", index++}, {"is_input", true}});
    }

    return data.dump(4);
}
