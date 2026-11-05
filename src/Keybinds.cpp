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
    std::vector<std::string> combo;    // keys of a key combination (buttonGroup), else empty
    bool placeholder = false;          // an empty buttonGroup
    // Controller buttons without overridableUI in the mappings that carry the action, with those mappings.
    std::map<std::string, std::vector<std::string>> pads;
    std::string type = "Button";       // type of the first mapping
};

bool IsPad(const std::string& aKey)
{
    return aKey.rfind("IK_Pad", 0) == 0 || aKey.rfind("IK_Joy", 0) == 0;
}

using Groups = std::map<std::string, std::vector<std::string>>; // buttonGroup id -> its keys

// A single controller button: not a stick axis, not a buttonGroup.
bool IsPadButton(const std::string& aKey, const Groups& aGroups)
{
    return aKey.rfind("IK_Pad", 0) == 0 && aKey.find("Axis") == std::string::npos &&
           aKey.find("Fake") == std::string::npos && !aGroups.contains(aKey);
}

// The only keyboard or mouse button of a Button mapping that has no overridableUI, or an
// empty node. A keyboard key combination (buttonGroup) counts as one button.
pugi::xml_node SingleKey(pugi::xml_node aMapping, const Groups& aGroups)
{
    if (std::string(aMapping.attribute("type").as_string("Button")) != "Button" || !*aMapping.attribute("name").as_string())
        return {};
    pugi::xml_node key;
    for (auto button : aMapping.children("button"))
    {
        if (button.attribute("overridableUI"))
            return {};
        std::string id = button.attribute("id").as_string();
        auto group = aGroups.find(id);
        if (id.rfind("IK_", 0) != 0 || IsPad(id) || (group != aGroups.end() && group->second.empty()))
            continue;
        if (key)
            return {};
        key = button;
    }
    return key;
}

// Keys of each buttonGroup; empty for groups with a gamepad button.
Groups ReadGroups(const pugi::xml_document& aDoc)
{
    Groups out;
    for (auto group : aDoc.child("bindings").children("buttonGroup"))
    {
        std::vector<std::string> keys;
        for (auto button : group.children("button"))
        {
            std::string id = button.attribute("id").as_string();
            if (IsPad(id))
            {
                keys.clear();
                break;
            }
            keys.push_back(id);
        }
        out[group.attribute("id").as_string()] = keys;
    }
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

    auto groups = ReadGroups(doc);
    // buttonGroups without buttons: a placeholder for a key another mod puts in place.
    std::set<std::string> empty;
    for (auto group : doc.child("bindings").children("buttonGroup"))
    {
        if (!group.child("button"))
            empty.insert(group.attribute("id").as_string());
    }
    std::size_t order = 0;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        if (auto key = SingleKey(mapping, groups))
        {
            std::string name = mapping.attribute("name").as_string();
            std::string id = key.attribute("id").as_string();
            auto group = groups.find(id);
            out.emplace(name, Button{id, order++, {name}, group != groups.end() ? group->second : std::vector<std::string>{}});
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
            {
                it = out.emplace(name, Button{id, order++, {}, {}, empty.contains(id)}).first;
                it->second.type = mapping.attribute("type").as_string("Button");
            }
            else if (IsPad(it->second.id) && !IsPad(id))
            {
                it->second.id = id;
                it->second.placeholder = empty.contains(id);
            }
            std::string mapName = mapping.attribute("name").as_string();
            if (std::find(it->second.mappings.begin(), it->second.mappings.end(), mapName) == it->second.mappings.end())
                it->second.mappings.push_back(mapName);
            if (std::string(mapping.attribute("type").as_string("Button")) != "Button")
                continue;
            for (auto other : mapping.children("button"))
            {
                std::string padId = other.attribute("id").as_string();
                if (other.attribute("overridableUI") || !IsPadButton(padId, groups))
                    continue;
                auto& maps = it->second.pads[padId];
                if (std::find(maps.begin(), maps.end(), mapName) == maps.end())
                    maps.push_back(mapName);
            }
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
    if (!aAction.labelKey.empty())
        return aAction.labelKey;
    // Labels a mod gave as LocKey# are already translation keys.
    if (aAction.label.rfind("LocKey#", 0) == 0)
        return aAction.label;
    return kLabelKeyPrefix + aAction.name;
}

std::string Keybinds::StartKey(const Action& aAction, const std::map<std::string, std::string>& aSaved)
{
    if (!aAction.modId.empty() && !aAction.current.empty())
        return aAction.current;
    if (auto it = aSaved.find(aAction.name); it != aSaved.end())
        return it->second;
    return !aAction.current.empty() ? aAction.current : aAction.defaultKey;
}

std::string Keybinds::GroupPath(const Action& aAction)
{
    if (!aAction.group.empty())
        return aAction.group;
    return kGroupPrefix + Slug(aAction.section);
}

std::string Keybinds::SectionKey(const Action& aAction)
{
    if (!aAction.sectionKey.empty())
        return aAction.sectionKey;
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
        if (base.contains(name) || listed.contains(name))
            continue;
        // A controller binding of the mod's own, if it is a single button.
        bool pad = IsPad(button.id);
        if (pad && (button.type != "Button" || button.id.find("Axis") != std::string::npos ||
                    button.id.find("Fake") != std::string::npos || !button.combo.empty()))
            continue;

        Action action{name, button.id};
        action.mappings = button.mappings;
        action.combo = button.combo;
        action.placeholder = button.placeholder;
        action.pad = pad;
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
        // Only shown while a controller is in use, so the word adds nothing.
        if (pad)
        {
            for (const char* word : {" Gamepad", " Pad", " Controller"})
            {
                std::string w = word;
                if (action.label.size() > w.size() && action.label.compare(action.label.size() - w.size(), w.size(), w) == 0)
                    action.label.erase(action.label.size() - w.size());
            }
        }

        // The controller button next to the key, when it is the only one.
        Action padAction;
        bool addPad = !pad && !action.placeholder && button.pads.size() == 1;
        if (addPad)
        {
            const auto& [padId, padMaps] = *button.pads.begin();
            padAction.name = kPadPrefix + name;
            padAction.defaultKey = padId;
            padAction.mappings = padMaps;
            padAction.section = action.section;
            padAction.label = action.label;
            padAction.labelKey = LabelKey(action);
            padAction.pad = true;
        }
        found.push_back({button, std::move(action)});
        if (addPad)
            found.push_back({button, std::move(padAction)});
    }

    // One section per mod, sorted by name; actions keep their mod's own order.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.second.section != b.second.section)
            return a.second.section < b.second.section;
        return a.first.order < b.first.order;
    });

    // Vanilla controller buttons: the game's key binding entries take keyboard keys only, so each
    // gets a controller entry in a section of ours named like the game's.
    std::vector<Action> out;
    if (!baseOptions.is_discarded() && baseOptions.contains("groups") && baseOptions.contains("options"))
    {
        std::vector<std::pair<int, json>> groups;
        for (const auto& g : baseOptions["groups"])
        {
            std::string path = g.value("group_name", "");
            if (path.rfind("/key_bindings/", 0) == 0 && !IsModGroup(path))
                groups.push_back({g.value("order", 0), g});
        }
        std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [groupOrder, g] : groups)
        {
            std::string path = g.value("group_name", "");
            for (const auto& list : baseOptions["options"])
            {
                if (list.value("group_name", "") != path || !list.contains("options"))
                    continue;
                std::vector<json> options(list["options"].begin(), list["options"].end());
                std::stable_sort(options.begin(), options.end(),
                                 [](const json& a, const json& b) { return a.value("order", 0) < b.value("order", 0); });
                for (const auto& opt : options)
                {
                    std::string name = opt.value("name", "");
                    auto it = merged.find(name);
                    if (!opt.value("is_input", false) || !opt.value("is_visible", true) || it == merged.end() ||
                        it->second.pads.size() != 1)
                        continue;
                    const auto& [padId, padMaps] = *it->second.pads.begin();
                    Action a;
                    a.name = kPadPrefix + name;
                    a.defaultKey = padId;
                    a.mappings = padMaps;
                    a.section = path.substr(path.rfind('/') + 1);
                    a.label = name;
                    a.labelKey = opt.value("display_name", name);
                    if (opt.contains("display_name_keys") && opt["display_name_keys"].is_array())
                    {
                        for (const auto& k : opt["display_name_keys"])
                        {
                            if (k.is_string())
                                a.labelKeys.push_back(k.get<std::string>());
                        }
                    }
                    a.group = std::string(kGroupPrefix) + "Pad" + Slug(a.section);
                    a.sectionKey = g.value("display_name", a.section);
                    a.pad = true;
                    out.push_back(std::move(a));
                }
            }
        }
    }

    for (auto& [button, action] : found)
        out.push_back(std::move(action));
    return out;
}

namespace
{
// Most key combinations (buttonGroups) the game was seen to load: 30 ran, 33 crashed it.
constexpr std::size_t kMaxCombos = 30;
} // namespace

std::string Keybinds::MarkRebindable(const fs::path& aMerged, const std::vector<Action>& aActions,
                                     const std::map<std::string, std::string>& aCombos, const LogFn& aLog)
{
    pugi::xml_document doc;
    if (!doc.load_file(aMerged.c_str()))
        return {};

    std::set<std::string> names;
    // Controller entries of ours: mapping -> (button id -> entry name).
    std::map<std::string, std::map<std::string, std::string>> padNames;
    for (const auto& a : aActions)
    {
        names.insert(a.name);
        if (a.name.rfind(kPadPrefix, 0) == 0)
        {
            for (const auto& m : a.mappings)
                padNames[m][a.defaultKey] = a.name;
        }
    }

    auto groups = ReadGroups(doc);
    bool changed = false;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        auto key = SingleKey(mapping, groups);
        if (key && names.contains(mapping.attribute("name").as_string()))
        {
            key.append_attribute("overridableUI") = mapping.attribute("name").value();
            changed = true;
        }
        if (auto pads = padNames.find(mapping.attribute("name").as_string()); pads != padNames.end())
        {
            for (auto button : mapping.children("button"))
            {
                auto pad = pads->second.find(button.attribute("id").as_string());
                if (pad != pads->second.end() && !button.attribute("overridableUI"))
                {
                    button.append_attribute("overridableUI") = pad->second.c_str();
                    changed = true;
                }
            }
        }
    }

    // Key combinations picked in the menu go into the action's own mapping: the game only watches
    // combinations that an action uses when it loads them. It crashes with more than about 30
    // combinations in all, so beyond that an action keeps its key.
    auto bindings = doc.child("bindings");
    std::size_t count = groups.size();
    std::set<std::string> declared;
    for (auto mapping : bindings.children("mapping"))
    {
        for (auto button : mapping.children("button"))
        {
            auto combo = aCombos.find(button.attribute("overridableUI").as_string());
            std::string id = button.attribute("id").as_string();
            auto plus = combo != aCombos.end() ? combo->second.find('+') : std::string::npos;
            if (plus == std::string::npos || IsPad(id))
                continue;
            auto modifier = combo->second.substr(0, plus);
            auto key = combo->second.substr(plus + 1);
            auto groupId = "ModKeybindsMenu_" + modifier + "_" + key;
            if (!declared.contains(groupId))
            {
                if (count >= kMaxCombos)
                {
                    aLog("Too many key combinations (the game allows about " + std::to_string(kMaxCombos) + "), " +
                         combo->first + " keeps its key");
                    continue;
                }
                // Room to press the modifier a moment before the key.
                auto group = bindings.prepend_child("buttonGroup");
                group.append_attribute("id") = groupId.c_str();
                group.append_attribute("timeWindow") = "0.5f";
                group.append_child("button").append_attribute("id") = modifier.c_str();
                group.append_child("button").append_attribute("id") = key.c_str();
                declared.insert(groupId);
                ++count;
            }
            button.attribute("id") = groupId.c_str();
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
        json option = {{"name", a.name}, {"display_name", LabelKey(a)}, {"order", index++}, {"is_input", true}};
        if (!a.labelKeys.empty())
            option["display_name_keys"] = a.labelKeys;
        options[list]["options"].push_back(std::move(option));
    }

    return data.dump(4);
}
