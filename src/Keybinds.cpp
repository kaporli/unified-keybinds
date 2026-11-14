#include "Keybinds.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
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

// Controller schemes (Settings > Controls > Control Scheme). A mapping with a copy for every scheme
// (Name_Legacy, Name_Agile, Name_Alternative) gets its controller buttons from the copy of the scheme
// in use; its own are ignored. Its keyboard keys stay. Mapping -> copy for aScheme.
using Copies = std::map<std::string, std::string>;

Copies SchemeCopies(const pugi::xml_document& aDoc, const std::string& aScheme)
{
    std::set<std::string> names;
    for (auto mapping : aDoc.child("bindings").children("mapping"))
        names.insert(mapping.attribute("name").as_string());
    Copies out;
    if (aScheme.empty())
        return out;
    for (const auto& n : names)
    {
        if (names.contains(n + "_Legacy") && names.contains(n + "_Agile") && names.contains(n + "_Alternative") &&
            names.contains(n + "_" + aScheme))
            out[n] = n + "_" + aScheme;
    }
    return out;
}

// overridableUI -> first keyboard button id, in document order. A mapping with a single key
// and no overridableUI is listed by its own name, it gets one in the copy the game reads.
// aCopies receives the mappings whose controller buttons come from a scheme's copy.
std::map<std::string, Button> ReadMappings(const fs::path& aFile, const std::string& aScheme = {},
                                           Copies* aCopies = nullptr)
{
    std::map<std::string, Button> out;
    pugi::xml_document doc;
    if (!doc.load_file(aFile.c_str()))
        return out;

    auto copies = SchemeCopies(doc, aScheme);
    std::map<std::string, pugi::xml_node> byName;
    for (auto mapping : doc.child("bindings").children("mapping"))
        byName.emplace(mapping.attribute("name").as_string(), mapping);
    if (aCopies)
        *aCopies = copies;

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
            auto& b = out.emplace(name, Button{id, order++, {name}, group != groups.end() ? group->second : std::vector<std::string>{}})
                          .first->second;
            // Its controller button, as for an action with overridableUI: it gets a controller entry.
            for (auto other : mapping.children("button"))
            {
                std::string padId = other.attribute("id").as_string();
                if (IsPadButton(padId, groups))
                    b.pads[padId] = {name};
            }
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
            auto padSource = mapping;
            if (auto copy = copies.find(mapName); copy != copies.end())
            {
                padSource = byName[copy->second];
                mapName = copy->second;
            }
            for (auto other : padSource.children("button"))
            {
                std::string padId = other.attribute("id").as_string();
                // A button with no overridableUI, or with the action's own (the mod gave the key and the
                // button one name): the action's controller button.
                std::string own = other.attribute("overridableUI").as_string();
                if ((!own.empty() && own != name) || !IsPadButton(padId, groups))
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
        // "_section" of another type than text is ignored.
        auto sectionNode = data.find("_section");
        std::string section = sectionNode != data.end() && sectionNode->is_string() ? sectionNode->get<std::string>() : std::string();
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
// Where a mapping is in use: its actions and contexts. Contexts that never run during play are left
// out: the Key Bindings menu's conflict checks (Settings*), arcade minigames and tutorials.
struct Use
{
    std::set<std::string> actions;
    std::set<std::string> contexts;
};
using Uses = std::map<std::string, Use>; // by mapping name

bool IsLiveContext(const std::string& aContext)
{
    static const std::set<std::string> skipped = {"UIShooterMinigame", "UIRoachRaceMinigame", "UITankMinigame",
                                                  "UIArcadeMinigames", "MotionPatternTutorial"};
    return aContext.rfind("Settings", 0) != 0 && !skipped.contains(aContext);
}

Uses ReadUses(const fs::path& aContexts)
{
    Uses out;
    pugi::xml_document doc;
    if (!doc.load_file(aContexts.c_str()))
        return out;
    for (auto context : doc.child("bindings").children("context"))
    {
        std::string name = context.attribute("name").as_string();
        if (!IsLiveContext(name))
            continue;
        for (auto action : context.children("action"))
        {
            std::string a = action.attribute("name").as_string();
            const std::string tutorial = "_Tutorial";
            if (a.size() > tutorial.size() && a.compare(a.size() - tutorial.size(), tutorial.size(), tutorial) == 0)
                continue;
            auto& use = out[action.attribute("map").as_string()];
            use.actions.insert(a);
            use.contexts.insert(name);
        }
    }
    return out;
}

// Menu navigation: moving between items, confirming, going back, switching tabs or options. Rebinding
// these on a controller changes how every menu is used, so they get no controller entry.
bool IsNavigation(const Use& aUse)
{
    static const std::set<std::string> navigation = {
        "UI_MoveUp", "UI_MoveDown", "UI_MoveLeft", "UI_MoveRight", "popup_moveUp", "popup_moveDown",
        "popup_moveLeft", "popup_moveRight", "up_button", "down_button", "left_button", "right_button",
        "navigate_up", "navigate_down", "navigate_left", "navigate_right", "popup_navigate_up",
        "popup_navigate_down", "popup_navigate_left", "popup_navigate_right", "UI_DialogFocus", "click",
        "proceed", "UI_Apply", "UI_Cancel", "back", "cancel", "prior_menu", "next_menu", "popup_prior",
        "popup_next", "option_switch_prev", "option_switch_next", "option_switch_prev_settings",
        "option_switch_next_settings", "sub_option_switch_prev", "sub_option_switch_next", "mod_prior_menu",
        "mod_next_menu", "mod_prior_page", "mod_next_page", "ChoiceScrollUp", "ChoiceScrollDown",
        "PhotoMode_Left_Button", "PhotoMode_Right_Button", "PhotoMode_Prior_Menu", "PhotoMode_Next_Menu"};
    for (const auto& a : aUse.actions)
    {
        if (navigation.contains(a))
            return true;
    }
    return false;
}

// Contexts of full-screen menus and popups. A mapping used only there gets no controller entry:
// one keyboard key covers unrelated buttons there (Unequip Item: R3 zooms the map, Y sells a perk).
bool IsMenuOnly(const Use& aUse)
{
    static const std::set<std::string> menus = {
        "UI", "UIBase", "UIMenu", "UIMenuShared", "UIShared", "UINotifications", "UIFrameSwitcher",
        "UIVehicleVisualCustomization", "UIVehicleVisualCustomizationOverride", "UIZoomDevice", "UI_DPad_Wheel",
        "UI_Phone", "ModBrowserUI"};
    return std::all_of(aUse.contexts.begin(), aUse.contexts.end(), [](const auto& c) { return menus.contains(c); });
}

// Where a group of mappings applies, as a label suffix. Empty when it applies on foot.
std::string Category(const std::set<std::string>& aContexts)
{
    std::map<std::string, int> count;
    for (const auto& c : aContexts)
    {
        std::string cat;
        if (c.rfind("UI_QuickHackPanel", 0) == 0)
            cat = "Scanner";
        else if (c == "UIToggles" || c == "UIExploration" || c == "UI_DPad")
            cat = "";
        else if (c.find("Braindance") != std::string::npos)
            cat = "Braindance";
        else if (c.find("PhotoMode") != std::string::npos)
            cat = "Photo Mode";
        else if (c.find("Vehicle") != std::string::npos || c == "DelamainTaxi")
            cat = c.find("Combat") != std::string::npos ? "Vehicle Combat" : "Vehicle";
        else if (c.rfind("UI", 0) == 0 || c == "ModBrowserUI" || c == "InputBlocked")
            cat = "Menus";
        else if (c == "DeviceControl")
            cat = "Devices";
        else if (c.rfind("Vision", 0) == 0)
            cat = "Scanner";
        ++count[cat];
    }
    if (count[""] > 0)
        return "";
    std::string best;
    int most = -1;
    for (const char* cat : {"", "Vehicle", "Vehicle Combat", "Scanner", "Devices", "Menus", "Photo Mode", "Braindance"})
    {
        if (count[cat] > most)
        {
            most = count[cat];
            best = cat;
        }
    }
    return best;
}

// A controller button of an action and the mappings it sits in.
struct PadGroup
{
    std::string button;
    std::vector<std::string> mappings;
    std::set<std::string> contexts;
    bool navigation = false;
};

// The controller buttons of an action that are in use during play, one group per button. A mapping's
// button that an earlier action took (aClaimed) is left to that action.
std::vector<PadGroup> PadGroups(const Button& aButton, const Uses& aUses,
                                std::set<std::pair<std::string, std::string>>& aClaimed, bool aShowNavigation)
{
    std::vector<PadGroup> out;
    for (const auto& [padId, mappings] : aButton.pads)
    {
        PadGroup group{padId};
        for (const auto& m : mappings)
        {
            auto use = aUses.find(m);
            if (use == aUses.end() || aClaimed.contains({m, padId}))
                continue;
            bool navigation = IsNavigation(use->second) || IsMenuOnly(use->second);
            if (navigation && !aShowNavigation)
                continue;
            group.navigation = group.navigation || navigation;
            group.mappings.push_back(m);
            group.contexts.insert(use->second.contexts.begin(), use->second.contexts.end());
        }
        if (group.mappings.empty())
            continue;
        for (const auto& m : group.mappings)
            aClaimed.insert({m, padId});
        out.push_back(std::move(group));
    }
    // The on-foot group first, it keeps the plain label.
    std::stable_sort(out.begin(), out.end(), [](const PadGroup& a, const PadGroup& b) {
        return Category(a.contexts).empty() && !Category(b.contexts).empty();
    });
    return out;
}

// Label suffix for each group: none for a single group or the first on-foot one, else where it applies,
// or the button too when two groups apply in the same place.
std::vector<std::string> Suffixes(const std::vector<PadGroup>& aGroups)
{
    std::vector<std::string> out;
    std::map<std::string, int> seen;
    for (const auto& g : aGroups)
        ++seen[Category(g.contexts)];
    for (std::size_t i = 0; i < aGroups.size(); ++i)
    {
        auto cat = Category(aGroups[i].contexts);
        if (aGroups.size() == 1 || (i == 0 && cat.empty() && seen[cat] == 1))
            out.push_back("");
        else if (seen[cat] == 1)
            out.push_back(cat);
        else
            out.push_back((cat.empty() ? "" : cat + ", ") + Keybinds::KeyText(aGroups[i].button));
    }
    return out;
}

// Vanilla controller buttons that do something else than their keyboard entry's label says, by
// the mapping they are in.
std::string OwnLabel(const PadGroup& aGroup)
{
    static const std::map<std::string, std::string> labels = {
        {"CancelCharging_Button", "Cancel Gadget Throw"},
        {"PickUpBodyFromTakedown_Button", "Pick Up Body (After Takedown)"},
        {"NotificationLvlup_Button", "Open Level-Up Notification"},
        {"ExitPhotoModeButton", "Exit Photo Mode"},
        {"ResetCurrentLightButton", "Reset Light (Photo Mode)"},
        {"LockCameraButton", "Lock Camera (Photo Mode)"},
        {"OpenQuickHackPanel_Button", "Open Quickhacks (Scanner)"}};
    for (const auto& m : aGroup.mappings)
    {
        if (auto it = labels.find(m); it != labels.end())
            return it->second;
    }
    return {};
}

// The game's "(Hold)" key binding entries: sprintHold, crouchHold, visionHold.
bool IsHoldVariant(const std::string& aName)
{
    return aName.size() > 4 && aName.compare(aName.size() - 4, 4, "Hold") == 0;
}

// Entries whose controller button may be shared (tap for the other action, hold for this one): the
// "(Hold)" entries, and Scan (Toggle), which the game gives no controller button.
bool IsShareVariant(const std::string& aName)
{
    return IsHoldVariant(aName) || aName == "visionToggle";
}

bool ShowNavigation()
{
#ifdef MKM_TEST
    char* value = nullptr;
    std::size_t length = 0;
    bool on = _dupenv_s(&value, &length, "MKM_SHOW_NAV") == 0 && value && value[0] == '1';
    free(value);
    return on;
#else
    return false;
#endif
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
    if (auto it = aSaved.find(aAction.name); it != aSaved.end() && Fits(aAction, it->second))
        return it->second;
    return !aAction.current.empty() ? aAction.current : aAction.defaultKey;
}

bool Keybinds::TakesCombo(const Action& aAction)
{
    bool trigger = aAction.defaultKey == "IK_Pad_LeftTrigger" || aAction.defaultKey == "IK_Pad_RightTrigger";
    return aAction.buttonMapping && aAction.name.rfind("NSUI_", 0) != 0 && aAction.modId.empty() && !(aAction.pad && trigger);
}

bool Keybinds::Fits(const Action& aAction, const std::string& aKey)
{
    if (aKey == kUnbound || aKey == aAction.defaultKey)
        return true;
    auto trigger = [](const std::string& k) { return k == "IK_Pad_LeftTrigger" || k == "IK_Pad_RightTrigger"; };
    // A key of the entry's own kind: a controller button for a controller entry, else a keyboard or mouse key.
    auto own = [&](const std::string& k) { return k.rfind("IK_", 0) == 0 && IsPad(k) == aAction.pad; };
    // A combination: two keys of the entry's kind, no triggers (an axis to the game). Native Settings
    // UI and RCF read a single key.
    if (auto plus = aKey.find('+'); plus != std::string::npos)
    {
        auto held = aKey.substr(0, plus), pressed = aKey.substr(plus + 1);
        return TakesCombo(aAction) && own(held) && own(pressed) && held != pressed && !trigger(held) && !trigger(pressed);
    }
    return own(aKey) && (!aAction.pad || trigger(aKey) == trigger(aAction.defaultKey));
}

std::string Keybinds::GroupPath(const Action& aAction)
{
    if (!aAction.group.empty())
        return aAction.group;
    // "Pad..." is taken by the game's controller sections (SettingsModPad<section>).
    auto slug = Slug(aAction.section);
    // "Pad..." and "Key..." are taken by the sections of the game's actions (SettingsModPad<section>,
    // SettingsModKey<section>).
    return kGroupPrefix + (slug.rfind("Pad", 0) == 0 || slug.rfind("Key", 0) == 0 ? "Mod" + slug : slug);
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

std::string Keybinds::KeyText(const std::string& aKey)
{
    if (auto plus = aKey.find('+'); plus != std::string::npos)
        return KeyText(aKey.substr(0, plus)) + " + " + KeyText(aKey.substr(plus + 1));
    static const std::map<std::string, std::string> names = {
        {"IK_Pad_A_CROSS", "A"}, {"IK_Pad_B_CIRCLE", "B"}, {"IK_Pad_X_SQUARE", "X"}, {"IK_Pad_Y_TRIANGLE", "Y"},
        {"IK_Pad_DigitUp", "D-Pad Up"}, {"IK_Pad_DigitDown", "D-Pad Down"}, {"IK_Pad_DigitLeft", "D-Pad Left"},
        {"IK_Pad_DigitRight", "D-Pad Right"}, {"IK_Pad_LeftShoulder", "LB"}, {"IK_Pad_RightShoulder", "RB"},
        {"IK_Pad_LeftTrigger", "LT"}, {"IK_Pad_RightTrigger", "RT"}, {"IK_Pad_LeftThumb", "LS"},
        {"IK_Pad_RightThumb", "RS"}, {"IK_Pad_Start", "Menu"}, {"IK_Pad_Back_Select", "View"}};
    if (auto it = names.find(aKey); it != names.end())
        return it->second;
    if (aKey.rfind("IK_Pad_", 0) == 0)
        return Humanize(aKey.substr(7));
    return aKey.rfind("IK_", 0) == 0 ? aKey.substr(3) : aKey;
}

std::string Keybinds::Humanize(const std::string& aText)
{
    auto words = Words(aText);
    for (auto& w : words)
        w = Capitalize(w);
    return Join(words);
}

std::vector<Keybinds::Action> Keybinds::Collect(const fs::path& aRoot, const std::string& aBaseOptions,
                                                const std::string& aScheme, const LogFn& aLog)
{
    Copies copies;
    auto merged = ReadMappings(aRoot / "r6" / "cache" / "inputUserMappings.xml", aScheme, &copies);
    if (merged.empty())
    {
        aLog("No merged input mappings in r6/cache. Is Input Loader installed?");
        return {};
    }

    auto base = ReadMappings(aRoot / "r6" / "config" / "inputUserMappings.xml", aScheme);
    auto baseOptions = json::parse(aBaseOptions, nullptr, false);
    auto listed = baseOptions.is_discarded() ? std::set<std::string>{} : OptionNames(baseOptions);

    auto sources = ReadSources(aRoot);
    auto files = ReadLabelFiles(aRoot, aLog);
    auto uses = ReadUses(aRoot / "r6" / "cache" / "inputContexts.xml");
    std::set<std::string> ownEvents; // actions with accepted events of their own
    {
        pugi::xml_document contexts;
        if (contexts.load_file((aRoot / "r6" / "cache" / "inputContexts.xml").c_str()))
        {
            for (auto e : contexts.child("bindings").children("acceptedEvents"))
                ownEvents.insert(e.attribute("action").as_string());
        }
    }
    // A scheme's copy takes the place of its mapping wherever that is used.
    for (const auto& [mapping, copy] : copies)
    {
        if (auto use = uses.find(mapping); use != uses.end())
        {
            auto from = use->second;
            uses[copy].actions.insert(from.actions.begin(), from.actions.end());
            uses[copy].contexts.insert(from.contexts.begin(), from.contexts.end());
        }
    }
    bool showNavigation = ShowNavigation();
    // Where a keyboard entry acts, as for the controller entries: empty on foot, else "Vehicle", "Menus", ...
    auto PlaceOf = [&](const std::vector<std::string>& aMappings) {
        std::set<std::string> contexts;
        for (const auto& m : aMappings)
        {
            if (auto use = uses.find(m); use != uses.end())
                contexts.insert(use->second.contexts.begin(), use->second.contexts.end());
        }
        return contexts.empty() ? std::string() : Category(contexts);
    };
    // A mapping's controller button belongs to one entry: vanilla ones first, then mods.
    std::set<std::pair<std::string, std::string>> claimed;
    auto padEntries = [&](const Button& aButton, const std::string& aName, const Action& aBase) {
        std::vector<Action> out;
        auto groups = PadGroups(aButton, uses, claimed, showNavigation);
        // Groups with a label of their own; the others are told apart by where they apply.
        std::vector<std::string> own;
        std::vector<PadGroup> rest;
        for (const auto& g : groups)
        {
            own.push_back(groups.size() > 1 ? OwnLabel(g) : std::string());
            if (own.back().empty())
                rest.push_back(g);
        }
        auto restSuffixes = Suffixes(rest);
        std::vector<std::string> suffixes;
        for (std::size_t i = 0, r = 0; i < groups.size(); ++i)
            suffixes.push_back(own[i].empty() ? restSuffixes[r++] : std::string());
        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            Action p = aBase;
            p.name = kPadPrefix + aName + (groups.size() > 1 ? "_" + groups[i].button.substr(7) : "");
            p.defaultKey = groups[i].button;
            p.mappings = groups[i].mappings;
            p.pad = true;
            p.place = Category(groups[i].contexts);
            for (const auto& [mapping, copy] : copies)
                p.scheme = p.scheme || std::find(p.mappings.begin(), p.mappings.end(), copy) != p.mappings.end();
            if (!own[i].empty())
            {
                p.label = own[i];
                p.labelKey.clear();
                p.labelKeys.clear();
            }
            std::string suffix = suffixes[i];
            if (groups[i].navigation)
                suffix += std::string(suffix.empty() ? "" : ", ") + "NAV";
            if (!suffix.empty())
            {
                if (!p.labelKeys.empty() || p.labelKey.rfind("LocKey#", 0) == 0)
                {
                    // The game fills translated parts into a format like "% / %".
                    p.labelFormat = (p.labelKeys.empty() ? std::string("%") : p.labelKey) + " (" + suffix + ")";
                    if (p.labelKeys.empty())
                        p.labelKeys.push_back(p.labelKey);
                }
                else
                {
                    p.label += " (" + suffix + ")";
                    p.labelKey.clear();
                }
            }
            out.push_back(std::move(p));
        }
        return out;
    };

    std::vector<Action> vanillaKeys;
    std::vector<Action> vanillaPads;
    // The game's actions: each gets a keyboard entry and a controller entry in sections of ours named
    // like the game's, so both devices are set, stored and applied the same way. The game's own
    // entries take keyboard keys only and no combinations.
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
                    if (!opt.value("is_input", false) || !opt.value("is_visible", true) || it == merged.end())
                        continue;
                    Action a;
                    a.section = path.substr(path.rfind('/') + 1);
                    a.label = name;
                    a.labelKey = opt.value("display_name", name);
                    // Some input mods give plain text rather than a translation key.
                    if (a.labelKey.rfind("LocKey#", 0) != 0 && a.labelKey.find('%') == std::string::npos)
                    {
                        a.label = a.labelKey;
                        a.labelKey.clear();
                    }
                    if (opt.contains("display_name_keys") && opt["display_name_keys"].is_array())
                    {
                        for (const auto& k : opt["display_name_keys"])
                        {
                            if (k.is_string())
                                a.labelKeys.push_back(k.get<std::string>());
                        }
                    }
                    a.sectionKey = g.value("display_name", a.section);
                    a.gameOption = name;
                    a.gameGroup = path;
                    if (IsShareVariant(name))
                    {
                        a.holdOption = name;
                        // The action the option names: sprintHold > Sprint, visionHold > VisionHold,
                        // visionToggle > VisionToggle. A mapping can feed others too (Sprint_Button:
                        // SprintMultitap, Ping).
                        auto stem = Lower(name.substr(0, name.size() - (IsHoldVariant(name) ? 4 : 6)));
                        for (const auto& m : it->second.mappings)
                        {
                            auto use = uses.find(m);
                            if (use == uses.end())
                                continue;
                            for (const auto& act : use->second.actions)
                            {
                                auto lower = Lower(act);
                                if ((lower == stem || lower == Lower(name)) &&
                                    std::find(a.gameActions.begin(), a.gameActions.end(), act) == a.gameActions.end())
                                    a.gameActions.push_back(act);
                            }
                        }
                        // Its key can be shared when the long press can be added: not for an action
                        // with accepted events of its own (Scanner (Hold)).
                        a.holdShare = !a.gameActions.empty() &&
                                      std::none_of(a.gameActions.begin(), a.gameActions.end(),
                                                   [&](const auto& act) { return ownEvents.contains(act); });
                    }
                    // The keyboard entry: the option's keyboard key (or the id of its key combination).
                    if (!it->second.id.empty() && !IsPad(it->second.id))
                    {
                        Action k = a;
                        k.name = kKeyPrefix + name;
                        k.group = std::string(kGroupPrefix) + "Key" + Slug(a.section);
                        k.defaultKey = it->second.id;
                        k.combo = it->second.combo;
                        k.mappings = it->second.mappings;
                        k.buttonMapping = it->second.type == "Button";
                        k.place = PlaceOf(k.mappings);
                        vanillaKeys.push_back(std::move(k));
                    }
                    a.group = std::string(kGroupPrefix) + "Pad" + Slug(a.section);
                    auto pads = padEntries(it->second, name, a);
                    // A hold variant (Sprint (Hold), Crouch (Hold)) or Scan (Toggle) without a controller
                    // button gets an unbound entry: the keyboard has both variants, the controller should
                    // too. Only mappings used during play (not the arcade minigames).
                    if (pads.empty() && IsShareVariant(name))
                    {
                        Action p = a;
                        p.name = kPadPrefix + name;
                        p.defaultKey = kUnbound;
                        p.pad = true;
                        for (const auto& m : it->second.mappings)
                        {
                            if (uses.contains(m))
                                p.mappings.push_back(m);
                        }
                        if (!p.mappings.empty())
                            pads.push_back(std::move(p));
                    }
                    for (auto& p : pads)
                    {
                        // Sharing is for the entry on foot.
                        if (!p.place.empty())
                        {
                            p.holdOption.clear();
                            p.gameActions.clear();
                            p.holdShare = false;
                        }
                        vanillaPads.push_back(std::move(p));
                    }
                }
            }
        }
    }

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

        // Controller buttons next to the key.
        std::vector<Action> pads;
        if (!pad && !action.placeholder)
        {
            Action base;
            base.section = action.section;
            base.label = action.label;
            base.labelKey = LabelKey(action);
            pads = padEntries(button, name, base);
        }
        action.buttonMapping = button.type == "Button";
        if (!pad)
            action.place = PlaceOf(action.mappings);
        found.push_back({button, std::move(action)});
        for (auto& p : pads)
            found.push_back({button, std::move(p)});
    }

    // One section per mod, sorted by name; actions keep their mod's own order.
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.second.section != b.second.section)
            return a.second.section < b.second.section;
        return a.first.order < b.first.order;
    });

    // An entry following the control scheme is in the copies of all three schemes, so a scheme changed
    // during play takes its key: the button of each copy, and the scheme's default.
    {
        pugi::xml_document doc;
        std::map<std::string, std::string> padButton; // mapping -> its first controller button
        if (doc.load_file((aRoot / "r6" / "cache" / "inputUserMappings.xml").c_str()))
        {
            for (auto mapping : doc.child("bindings").children("mapping"))
            {
                for (auto button : mapping.children("button"))
                {
                    std::string id = button.attribute("id").as_string();
                    if (IsPad(id))
                    {
                        padButton.emplace(mapping.attribute("name").as_string(), id);
                        break;
                    }
                }
            }
        }
        for (auto& p : vanillaPads)
        {
            if (!p.scheme)
                continue;
            auto own = p.mappings;
            for (const auto& [mapping, copy] : copies)
            {
                if (std::find(own.begin(), own.end(), copy) == own.end())
                    continue;
                for (const char* s : {"Legacy", "Agile", "Alternative"})
                {
                    auto other = mapping + "_" + s;
                    auto button = padButton.find(other);
                    if (button == padButton.end())
                        continue;
                    p.schemeButtons[other] = button->second;
                    p.schemeDefaults.emplace(s, button->second);
                    if (std::find(p.mappings.begin(), p.mappings.end(), other) == p.mappings.end())
                        p.mappings.push_back(other);
                }
            }
        }
    }

    // The game's actions first (keyboard, then controller), then the mods'.
    std::vector<Action> out = std::move(vanillaKeys);
    out.insert(out.end(), std::make_move_iterator(vanillaPads.begin()), std::make_move_iterator(vanillaPads.end()));
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
                                     const std::map<std::string, std::string>& aSaved, const LogFn& aLog)
{
    pugi::xml_document doc;
    if (!doc.load_file(aMerged.c_str()))
        return {};
    std::map<std::string, std::string> aCombos;
    for (const auto& [name, key] : aSaved)
    {
        if (key.find('+') != std::string::npos)
            aCombos[name] = key;
    }

    std::set<std::string> names;
    // Controller entries of ours: mapping -> (button id -> entry name).
    std::map<std::string, std::map<std::string, std::string>> padNames;
    // Keyboard entries of ours for the game's options: option -> entry name.
    std::map<std::string, std::string> keyNames;
    for (const auto& a : aActions)
    {
        names.insert(a.name);
        if (a.name.rfind(kKeyPrefix, 0) == 0)
            keyNames[a.gameOption] = a.name;
        if (a.name.rfind(kPadPrefix, 0) == 0)
        {
            for (const auto& m : a.mappings)
            {
                auto copy = a.schemeButtons.find(m);
                padNames[m][copy != a.schemeButtons.end() ? copy->second : a.defaultKey] = a.name;
            }
        }
    }
    // Controller entries without a default button get a row of their own, so the key binding
    // overrides can change it like any other: with the saved button, or while unbound with a key no
    // device sends (a row without a key crashed the game's input handling). The overrides then set
    // it to unbound, and a button bound later applies at once.
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> rows; // mapping -> (entry, button)
    for (const auto& a : aActions)
    {
        // Only entries the plugin made for a game option (MKPad_...): another source's controller key
        // has its own row.
        if (!a.pad || a.defaultKey != kUnbound || a.name.rfind(kPadPrefix, 0) != 0)
            continue;
        auto saved = aSaved.find(a.name);
        bool bound = saved != aSaved.end() && saved->second.rfind("IK_Pad", 0) == 0;
        for (const auto& m : a.mappings)
            rows[m].push_back({a.name, bound ? saved->second.substr(0, saved->second.find('+')) : std::string(kNoKey)});
    }

    auto groups = ReadGroups(doc);
    bool changed = false;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        for (auto button : mapping.children("button"))
        {
            // A keyboard button of a game option is set through our entry: the option no longer reaches it.
            auto key = keyNames.find(button.attribute("overridableUI").as_string());
            if (key != keyNames.end() && !IsPad(button.attribute("id").as_string()))
            {
                button.attribute("overridableUI") = key->second.c_str();
                changed = true;
            }
        }
        if (auto added = rows.find(mapping.attribute("name").as_string()); added != rows.end())
        {
            for (const auto& [entry, button] : added->second)
            {
                auto row = mapping.append_child("button");
                row.append_attribute("id") = button.c_str();
                row.append_attribute("overridableUI") = entry.c_str();
            }
            changed = true;
        }
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
                // Also a button carrying the action's own name: the controller entry gets its own.
                std::string own = button.attribute("overridableUI").as_string();
                if (pad != pads->second.end() && (own.empty() || pad->second.rfind(std::string(kPadPrefix) + own, 0) == 0))
                {
                    if (own.empty())
                        button.append_attribute("overridableUI") = pad->second.c_str();
                    else
                        button.attribute("overridableUI") = pad->second.c_str();
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
            // Keyboard combinations go on keyboard buttons, controller ones on controller buttons.
            if (plus == std::string::npos || IsPad(id) != IsPad(combo->second.substr(0, plus)))
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
                // Room to press the modifier (or held button) a moment before the key.
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
        json option = {{"name", a.name}, {"display_name", a.labelFormat.empty() ? LabelKey(a) : a.labelFormat},
                       {"order", index++}, {"is_input", true}};
        if (!a.labelKeys.empty())
            option["display_name_keys"] = a.labelKeys;
        options[list]["options"].push_back(std::move(option));
    }

    return data.dump(4);
}

std::map<std::string, std::map<std::string, std::string>> Keybinds::GameKeys(const fs::path& aUserSettings)
{
    std::map<std::string, std::map<std::string, std::string>> keyboard;
    std::ifstream in(aUserSettings);
    auto settings = json::parse(in, nullptr, false);
    std::function<void(const json&)> walk = [&](const json& aNode) {
        if (aNode.is_object())
        {
            if (aNode.contains("group_name") && aNode["group_name"].is_string() && aNode.contains("options") &&
                aNode["options"].is_array() && aNode["group_name"].get<std::string>().rfind("/key_bindings/", 0) == 0)
            {
                auto& group = keyboard[aNode["group_name"].get<std::string>()];
                for (const auto& o : aNode["options"])
                {
                    if (o.contains("name") && o["name"].is_string() && o.contains("value") && o["value"].is_string())
                        group[o["name"].get<std::string>()] = o["value"].get<std::string>();
                }
            }
            for (const auto& [k, v] : aNode.items())
                walk(v);
        }
        else if (aNode.is_array())
        {
            for (const auto& v : aNode)
                walk(v);
        }
    };
    if (!settings.is_discarded())
        walk(settings);
    return keyboard;
}

std::vector<Keybinds::HoldOnly> Keybinds::HoldOnlyActions(const std::vector<Action>& aActions,
                                                          const std::map<std::string, std::string>& aSaved, bool aAll,
                                                          const fs::path& aContexts)
{
    // One per (Hold) option, its entries of both devices together.
    std::map<std::string, std::pair<HoldOnly, std::vector<std::string>>> byOption; // option -> (setup, game actions)
    std::set<std::string> share;
    for (const auto& a : aActions)
    {
        if (a.holdOption.empty() || a.gameActions.empty())
            continue;
        auto& [h, acts] = byOption[a.holdOption];
        h.option = a.holdOption;
        for (const auto& act : a.gameActions)
        {
            if (std::find(acts.begin(), acts.end(), act) == acts.end())
                acts.push_back(act);
        }
        if (a.holdShare)
            share.insert(a.holdOption);
        auto& side = h.Side(a.pad);
        side.entry = a.name;
        auto key = StartKey(a, aSaved);
        if (key == kUnbound || key == "None")
            continue;
        for (const auto& b : aActions)
        {
            if (&b == &a || b.pad != a.pad || !b.place.empty() || StartKey(b, aSaved) != key)
                continue;
            // On the keyboard only the game's own options count (as up to 1.8.2): a mod's key or an
            // Input Loader edit on the same key keeps firing with it, without the long press.
            if (!a.pad && b.gameOption.empty())
                continue;
            // Only an action that can react at the same time (one context, one section of the game).
            if (!aContexts.empty() && !ActTogether(aContexts, a.mappings, b.mappings))
                continue;
            side.shared = true;
            side.mappings.insert(side.mappings.end(), b.mappings.begin(), b.mappings.end());
        }
    }
    std::vector<HoldOnly> out;
    for (auto& [option, entry] : byOption)
    {
        auto& [h, acts] = entry;
        if (!h.keyboard.shared && !h.controller.shared && !(aAll && share.contains(option)))
            continue;
        for (const auto& act : acts)
        {
            h.action = act;
            out.push_back(h);
        }
    }
    return out;
}

std::string Keybinds::MarkHoldOnly(const fs::path& aMerged, const std::vector<HoldOnly>& aHoldOnly, bool aLive, std::vector<EventRule>& aRules,
                                   const LogFn& aLog)
{
    aRules.clear();
    if (aHoldOnly.empty())
        return {};
    pugi::xml_document doc;
    if (!doc.load_file(aMerged.c_str()))
        return {};
    auto bindings = doc.child("bindings");

    // What each action reacts to, and which actions a mapping feeds during play.
    std::map<std::string, pugi::xml_node> lists;
    std::set<std::string> held;
    std::map<std::string, std::set<std::string>> feeds; // mapping -> actions
    for (auto e : bindings.children("acceptedEvents"))
        lists.emplace(e.attribute("action").as_string(), e);
    for (auto h : bindings.children("hold"))
        held.insert(h.attribute("action").as_string());
    for (auto context : bindings.children("context"))
    {
        if (!IsLiveContext(context.attribute("name").as_string()))
            continue;
        for (auto action : context.children("action"))
            feeds[action.attribute("map").as_string()].insert(action.attribute("name").as_string());
    }
    // Actions of each live context with the contexts it includes: actions that can be active together.
    std::map<std::string, pugi::xml_node> contexts;
    for (auto context : bindings.children("context"))
        contexts[context.attribute("name").as_string()] = context;
    std::map<std::string, std::set<std::string>> together; // context -> its actions and included ones
    std::function<const std::set<std::string>&(const std::string&, std::set<std::string>&)> collect =
        [&](const std::string& aName, std::set<std::string>& aSeen) -> const std::set<std::string>& {
        auto& out = together[aName];
        if (!aSeen.insert(aName).second)
            return out;
        auto node = contexts.find(aName);
        if (node == contexts.end())
            return out;
        for (auto action : node->second.children("action"))
            out.insert(action.attribute("name").as_string());
        for (auto include : node->second.children("include"))
        {
            auto inner = collect(include.attribute("name").as_string(), aSeen);
            out.insert(inner.begin(), inner.end());
        }
        return out;
    };
    for (const auto& [name, node] : contexts)
    {
        if (!IsLiveContext(name))
            continue;
        std::set<std::string> seen;
        collect(name, seen);
    }
    // Actions listed only in interaction contexts (InteractionActions): they belong to an on-screen
    // prompt. They keep their hold; the (Hold) action gives way while their prompt shows instead.
    auto promptAction = [&](const std::string& aAction) {
        bool any = false;
        for (const auto& [name, node] : contexts)
        {
            for (auto action : node.children("action"))
            {
                if (aAction != action.attribute("name").as_string())
                    continue;
                if (Lower(name).find("interaction") == std::string::npos)
                    return false;
                any = true;
            }
        }
        return any;
    };
    auto activeTogether = [&](const std::string& a, const std::string& b) {
        for (const auto& [name, actions] : together)
        {
            if (IsLiveContext(name) && actions.contains(a) && actions.contains(b))
                return true;
        }
        return false;
    };
    // In one section of the game (its Settings contexts: on foot, vehicle, menus, ...), as the menu's
    // conflict check counts them: an action of the menus never shares a key with one on foot.
    std::vector<std::set<std::string>> sections;
    for (const auto& [name, node] : contexts)
    {
        if (name.rfind("Settings", 0) != 0)
            continue;
        std::set<std::string> seen;
        sections.push_back(collect(name, seen));
    }
    auto sameSection = [&](const std::string& a, const std::string& b) {
        return std::any_of(sections.begin(), sections.end(), [&](const auto& s) { return s.contains(a) && s.contains(b); });
    };

    // The mappings of the game's key binding options.

    static const char* kHoldEvents[] = {"BUTTON_HOLD_PROGRESS", "BUTTON_HOLD_COMPLETE", "PATTERN_HOLD_PROGRESS",
                                        "PATTERN_HOLD_PASSED"};
    static const char* kOtherEvents[] = {"BUTTON_PRESSED",    "BUTTON_RELEASED",    "BUTTON_MULTITAP_BEGIN_LAST",
                                         "BUTTON_MULTITAP_END_LAST", "AXIS_CHANGE", "RELATIVE_CHANGE",
                                         "TOGGLE_PRESSED",    "TOGGLE_RELEASED",    "PATTERN_ACTIVATED",
                                         "PATTERN_DEACTIVATED", "REPEAT"};
    auto reactsToHold = [&](const std::string& aAction, bool& aOwnList) {
        aOwnList = false;
        auto list = lists.find(aAction);
        if (list == lists.end())
            return held.contains(aAction);
        aOwnList = true;
        for (auto e : list->second.children("event"))
        {
            for (auto h : kHoldEvents)
            {
                if (std::string(e.attribute("name").as_string()) == h)
                    return true;
            }
        }
        return false;
    };
    auto rule = [&](const std::string& aAction) -> EventRule& {
        for (auto& r : aRules)
        {
            if (r.action == aAction)
                return r;
        }
        aRules.push_back({aAction});
        return aRules.back();
    };

    bool changed = false;
    for (const auto& h : aHoldOnly)
    {
        if (lists.contains(h.action))
        {
            aLog("Hold-only: " + h.action + " already has accepted events, left as it is");
            continue;
        }
        if (!held.contains(h.action))
        {
            // The game's own hold time for sprint and crouch.
            auto hold = bindings.append_child("hold");
            hold.append_attribute("action") = h.action.c_str();
            hold.append_attribute("timeout") = "0.3";
            held.insert(h.action);
        }
        auto events = bindings.append_child("acceptedEvents");
        events.append_attribute("action") = h.action.c_str();
        events.append_child("event").append_attribute("name") = "BUTTON_HOLD_COMPLETE";
        events.append_child("event").append_attribute("name") = "BUTTON_RELEASED";
        lists.emplace(h.action, events);
        changed = true;
        auto& own = rule(h.action);
        own.keyboard = h.keyboard.shared ? EventMode::Keep : EventMode::All;
        own.controller = h.controller.shared ? EventMode::Keep : EventMode::All;
        aLog("Hold-only: " + h.action + " (controller " + (h.controller.shared ? "yes" : "no") + ", keyboard " +
             (h.keyboard.shared ? "yes" : "no") + ")");

        // The actions it shares a key with give up the hold on that device. Sprint (Toggle) starts on the
        // press, which the game sends before the hold completes (holding would sprint, then slide): it
        // gets nothing from that device, and the scripts toggle sprint when the key is let go before the
        // hold (Tap.reds). With aLive every action that could share the key gets this set up now, at its
        // own events while unshared, so a key shared later counts at once (UpdateHoldOnly switches it).
        // Prompt actions keep their hold: the (Hold) action gives way while their prompt shows.
        auto setUp = [&](const std::string& aOther) -> EventRule* {
            bool holdOnly = std::any_of(aHoldOnly.begin(), aHoldOnly.end(), [&](const auto& x) { return x.action == aOther; });
            if (aOther == h.action || holdOnly || !activeTogether(aOther, h.action) || !sameSection(aOther, h.action))
                return nullptr;
            bool sprint = aOther == "ToggleSprint";
            bool ownList = false;
            bool reacts = reactsToHold(aOther, ownList);
            if (!sprint && (!reacts || promptAction(aOther)))
                return nullptr;
            bool known = std::any_of(aRules.begin(), aRules.end(), [&](const auto& r) { return r.action == aOther; });
            auto& r = rule(aOther);
            if (!known)
            {
                // Without a list of its own: one with every event but the hold ones, taken as every event
                // while unshared.
                if (sprint ? !lists.contains(aOther) : !ownList)
                {
                    auto list = bindings.append_child("acceptedEvents");
                    list.append_attribute("action") = aOther.c_str();
                    for (auto e : kOtherEvents)
                        list.append_child("event").append_attribute("name") = e;
                    lists.emplace(aOther, list);
                    changed = true;
                    r.keyboard = r.controller = r.keyboardBase = r.controllerBase = EventMode::All;
                }
                r.giveUp = sprint ? EventMode::None : EventMode::NoHold;
            }
            if (std::find(r.partnerOf.begin(), r.partnerOf.end(), h.action) == r.partnerOf.end())
                r.partnerOf.push_back(h.action);
            return &r;
        };
        std::set<std::string> candidates;
        if (aLive)
        {
            for (const auto& [mapping, acts] : feeds)
                candidates.insert(acts.begin(), acts.end());
        }
        std::set<std::string> fedBy[2]; // keyboard, controller
        for (bool pad : {false, true})
        {
            for (const auto& m : h.Side(pad).mappings)
            {
                if (auto fed = feeds.find(m); fed != feeds.end())
                {
                    fedBy[pad].insert(fed->second.begin(), fed->second.end());
                    candidates.insert(fed->second.begin(), fed->second.end());
                }
            }
        }
        for (const auto& other : candidates)
        {
            auto* r = setUp(other);
            if (!r)
            {
                if ((fedBy[0].contains(other) || fedBy[1].contains(other)) && promptAction(other))
                    aLog("Hold-only: " + other + " keeps its hold (prompt action): " + h.action +
                         " gives way while its prompt shows");
                continue;
            }
            for (bool pad : {false, true})
            {
                if (!fedBy[pad].contains(other))
                    continue;
                (pad ? r->controller : r->keyboard) = r->giveUp;
                aLog("Hold-only: " + other + (r->giveUp == EventMode::None ? " gets no event from the " : " gives up the hold on the ") +
                     (pad ? "controller" : "keyboard") + " (shares a key with " + h.action + ")");
            }
        }
        auto sharedActions = [&](const std::vector<std::string>& aMaps) {
            std::vector<std::string> out;
            for (const auto& m : aMaps)
            {
                auto fed = feeds.find(m);
                if (fed == feeds.end())
                    continue;
                for (const auto& a : fed->second)
                {
                    if (a != h.action && std::find(out.begin(), out.end(), a) == out.end())
                        out.push_back(a);
                }
            }
            return out;
        };
        auto& self = rule(h.action);
        self.controllerShared = sharedActions(h.controller.mappings);
        self.keyboardShared = sharedActions(h.keyboard.mappings);
    }
    std::string partners;
    for (const auto& r : aRules)
    {
        if (!r.partnerOf.empty())
            partners += (partners.empty() ? "" : ", ") + r.action;
    }
    if (!partners.empty())
        aLog("Hold-only: may give up their hold when a key is shared: " + partners);
    if (!changed)
        return {};
    std::ostringstream out;
    doc.save(out, "    ");
    return out.str();
}

std::set<std::string> Keybinds::ShareableActions(const std::vector<Action>& aActions)
{
    std::set<std::string> out;
    for (const auto& a : aActions)
    {
        if (a.holdShare)
            out.insert(a.gameActions.begin(), a.gameActions.end());
    }
    return out;
}

std::string Keybinds::MarkShareable(const std::string& aXml, const std::set<std::string>& aActions, const LogFn& aLog)
{
    if (aActions.empty())
        return {};
    pugi::xml_document doc;
    if (!doc.load_string(aXml.c_str()))
        return {};
    auto bindings = doc.child("bindings");
    std::map<std::string, pugi::xml_node> contexts;
    for (auto context : bindings.children("context"))
        contexts[context.attribute("name").as_string()] = context;
    auto holds = [&](pugi::xml_node aContext) {
        for (auto action : aContext.children("action"))
        {
            if (aActions.contains(action.attribute("name").as_string()))
                return true;
        }
        return false;
    };
    bool changed = false;
    for (auto& [name, settings] : contexts)
    {
        if (name.rfind("Settings", 0) != 0)
            continue;
        // Its own actions first.
        for (auto action = settings.child("action"); action;)
        {
            auto next = action.next_sibling("action");
            if (aActions.contains(action.attribute("name").as_string()))
            {
                settings.remove_child(action);
                changed = true;
            }
            action = next;
        }
        // Included contexts that hold one: written out here without it.
        for (auto include = settings.child("include"); include;)
        {
            auto next = include.next_sibling("include");
            auto inner = contexts.find(include.attribute("name").as_string());
            if (inner != contexts.end() && holds(inner->second))
            {
                for (auto child : inner->second.children())
                {
                    std::string tag = child.name();
                    if ((tag == "action" && aActions.contains(child.attribute("name").as_string())) ||
                        (tag != "action" && tag != "include" && tag != "exclude"))
                        continue;
                    settings.insert_copy_before(child, include);
                }
                aLog("Shareable keys: " + name + " gets " + inner->first + " without the (Hold) actions");
                settings.remove_child(include);
                changed = true;
            }
            include = next;
        }
    }
    if (!changed)
        return {};
    std::ostringstream out;
    doc.save(out, "    ");
    return out.str();
}

namespace
{
// Which actions a mapping feeds during play, and the actions of each live context with the contexts
// it includes (actions that can be active together).
struct ContextIndex
{
    std::map<std::string, std::set<std::string>> feeds;      // mapping -> actions
    std::map<std::string, std::set<std::string>> together;   // context -> its actions and those it includes
    std::map<std::string, std::set<std::string>> direct;     // action -> contexts that list it
    std::map<std::string, std::set<std::string>> includedBy; // context -> contexts that include it
    std::vector<std::set<std::string>> sections;             // actions of each Settings context with its includes
};

ContextIndex ReadContextIndex(const fs::path& aContexts)
{
    ContextIndex out;
    pugi::xml_document doc;
    if (!doc.load_file(aContexts.c_str()))
        return out;
    std::map<std::string, pugi::xml_node> contexts;
    for (auto context : doc.child("bindings").children("context"))
    {
        std::string name = context.attribute("name").as_string();
        contexts[name] = context;
        if (!IsLiveContext(name))
            continue;
        for (auto action : context.children("action"))
        {
            out.feeds[action.attribute("map").as_string()].insert(action.attribute("name").as_string());
            out.direct[action.attribute("name").as_string()].insert(name);
        }
        for (auto include : context.children("include"))
            out.includedBy[include.attribute("name").as_string()].insert(name);
    }
    std::function<std::set<std::string>(const std::string&, std::set<std::string>&)> collect =
        [&](const std::string& aName, std::set<std::string>& aSeen) {
            std::set<std::string> actions;
            if (!aSeen.insert(aName).second)
                return actions;
            auto node = contexts.find(aName);
            if (node == contexts.end())
                return actions;
            for (auto action : node->second.children("action"))
                actions.insert(action.attribute("name").as_string());
            for (auto include : node->second.children("include"))
            {
                auto inner = collect(include.attribute("name").as_string(), aSeen);
                actions.insert(inner.begin(), inner.end());
            }
            return actions;
        };
    for (const auto& [name, node] : contexts)
    {
        if (!IsLiveContext(name))
            continue;
        std::set<std::string> seen;
        out.together[name] = collect(name, seen);
    }
    for (const auto& [name, node] : contexts)
    {
        if (name.rfind("Settings", 0) != 0)
            continue;
        std::set<std::string> seen;
        out.sections.push_back(collect(name, seen));
    }
    return out;
}
// The merged contexts don't change after startup: read once per file (the menu asks on every change).
const ContextIndex& CachedIndex(const fs::path& aContexts)
{
    static std::mutex mutex;
    static std::map<fs::path, ContextIndex> read;
    std::lock_guard lock(mutex);
    auto found = read.find(aContexts);
    if (found == read.end())
        found = read.emplace(aContexts, ReadContextIndex(aContexts)).first;
    return found->second;
}

// The actions a mapping feeds during play. A control scheme's copy (Name_Legacy, Name_Agile,
// Name_Alternative) is not in the contexts: the game uses it in place of its mapping, so it feeds that one's.
const std::set<std::string>& Fed(const ContextIndex& aIndex, const std::string& aMapping)
{
    static const std::set<std::string> none;
    if (auto fed = aIndex.feeds.find(aMapping); fed != aIndex.feeds.end())
        return fed->second;
    for (const char* s : {"_Legacy", "_Agile", "_Alternative"})
    {
        std::string suffix = s;
        if (aMapping.size() > suffix.size() && aMapping.compare(aMapping.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            if (auto fed = aIndex.feeds.find(aMapping.substr(0, aMapping.size() - suffix.size())); fed != aIndex.feeds.end())
                return fed->second;
        }
    }
    return none;
}
} // namespace

bool Keybinds::ActTogether(const fs::path& aContexts, const std::vector<std::string>& aMappingsA,
                           const std::vector<std::string>& aMappingsB)
{
    const auto& index = CachedIndex(aContexts);
    std::set<std::string> a, b;
    for (const auto& m : aMappingsA)
    {
        const auto& fed = Fed(index, m);
        a.insert(fed.begin(), fed.end());
    }
    for (const auto& m : aMappingsB)
    {
        const auto& fed = Fed(index, m);
        b.insert(fed.begin(), fed.end());
    }
    auto both = [&](const std::set<std::string>& aList) {
        return std::any_of(a.begin(), a.end(), [&](const auto& x) { return aList.contains(x); }) &&
               std::any_of(b.begin(), b.end(), [&](const auto& x) { return aList.contains(x); });
    };
    bool together = std::any_of(index.together.begin(), index.together.end(), [&](const auto& t) { return both(t.second); });
    return together && std::any_of(index.sections.begin(), index.sections.end(), both);
}

std::vector<std::string> Keybinds::FedActions(const fs::path& aContexts, const std::vector<std::string>& aMappings)
{
    std::vector<std::string> out;
    const auto& index = CachedIndex(aContexts);
    for (const auto& m : aMappings)
    {
        for (const auto& a : Fed(index, m))
        {
            if (std::find(out.begin(), out.end(), a) == out.end())
                out.push_back(a);
        }
    }
    return out;
}

Keybinds::SharedKeys Keybinds::Conflicts(const fs::path& aContexts, const std::vector<BindingEntry>& aEntries)
{
    SharedKeys out;
    out.conflicts.resize(aEntries.size());
    out.shared.resize(aEntries.size());
    const auto& index = CachedIndex(aContexts);
    const std::set<std::string> none;
    auto at = [&](const std::map<std::string, std::set<std::string>>& aMap,
                  const std::string& aKey) -> const std::set<std::string>& {
        auto it = aMap.find(aKey);
        return it != aMap.end() ? it->second : none;
    };

    // Actions of each entry. Raw: the mod reads the key itself, so it reacts anywhere.
    std::vector<std::set<std::string>> actions(aEntries.size());
    std::vector<bool> raw(aEntries.size());
    for (std::size_t i = 0; i < aEntries.size(); ++i)
    {
        raw[i] = aEntries[i].mappings.empty();
        for (const auto& m : aEntries[i].mappings)
        {
            const auto& fed = Fed(index, m);
            actions[i].insert(fed.begin(), fed.end());
        }
    }

    // Where each entry acts: the game's sections (its Settings contexts: on foot, vehicle, menus).
    // The game's actions have theirs. A mod's action takes the section most of the game's actions in
    // its contexts have, or else that of the contexts that include its own. Empty: anywhere.
    std::map<std::string, std::map<std::string, int>> anchors; // context -> section -> game actions
    for (std::size_t i = 0; i < aEntries.size(); ++i)
    {
        if (aEntries[i].section.empty())
            continue;
        for (const auto& a : actions[i])
        {
            for (const auto& c : at(index.direct, a))
                ++anchors[c][aEntries[i].section];
        }
    }
    std::map<std::string, std::set<std::string>> contextScope;
    std::function<std::set<std::string>(const std::string&, std::set<std::string>&)> scopeOf =
        [&](const std::string& aContext, std::set<std::string>& aSeen) {
            if (auto known = contextScope.find(aContext); known != contextScope.end())
                return known->second;
            std::set<std::string> scope;
            if (!aSeen.insert(aContext).second)
                return scope;
            if (auto counts = anchors.find(aContext); counts != anchors.end())
            {
                int top = 0;
                for (const auto& [section, n] : counts->second)
                    top = std::max(top, n);
                for (const auto& [section, n] : counts->second)
                {
                    if (n == top)
                        scope.insert(section);
                }
            }
            else
            {
                for (const auto& parent : at(index.includedBy, aContext))
                {
                    auto inner = scopeOf(parent, aSeen);
                    scope.insert(inner.begin(), inner.end());
                }
            }
            contextScope[aContext] = scope;
            return scope;
        };
    std::vector<std::set<std::string>> scopes(aEntries.size());
    for (std::size_t i = 0; i < aEntries.size(); ++i)
    {
        if (!aEntries[i].section.empty())
        {
            scopes[i].insert(aEntries[i].section);
            continue;
        }
        for (const auto& a : actions[i])
        {
            for (const auto& c : at(index.direct, a))
            {
                std::set<std::string> seen;
                auto s = scopeOf(c, seen);
                scopes[i].insert(s.begin(), s.end());
            }
        }
    }
    auto sameSection = [&](std::size_t a, std::size_t b) {
        if (raw[a] || raw[b] || scopes[a].empty() || scopes[b].empty())
            return true;
        return std::any_of(scopes[a].begin(), scopes[a].end(), [&](const auto& s) { return scopes[b].contains(s); });
    };
    auto activeTogether = [&](std::size_t a, std::size_t b) {
        if (raw[a] || raw[b])
            return true;
        for (const auto& [name, list] : index.together)
        {
            bool hasA = std::any_of(actions[a].begin(), actions[a].end(), [&](const auto& x) { return list.contains(x); });
            bool hasB = std::any_of(actions[b].begin(), actions[b].end(), [&](const auto& x) { return list.contains(x); });
            if (hasA && hasB)
                return true;
        }
        return false;
    };

    for (std::size_t a = 0; a < aEntries.size(); ++a)
    {
        const auto& ea = aEntries[a];
        if (ea.key.empty() || ea.key == kUnbound || ea.key == "None")
            continue;
        for (std::size_t b = a + 1; b < aEntries.size(); ++b)
        {
            const auto& eb = aEntries[b];
            // Not a pair: another key, the same entry, a (Hold) entry with another (tap / hold, it has a
            // panel line of its own), entries feeding the same action, or one no context uses.
            if (eb.key != ea.key || eb.name == ea.name || ea.shareable != eb.shareable)
                continue;
            if (std::any_of(actions[a].begin(), actions[a].end(), [&](const auto& x) { return actions[b].contains(x); }))
                continue;
            if ((!raw[a] && actions[a].empty()) || (!raw[b] && actions[b].empty()))
                continue;
            bool conflict = sameSection(a, b) && activeTogether(a, b);
            // Two of the game's own entries (none of ours replaces them): its menu checks those itself.
            if (ea.mod.empty() && eb.mod.empty() && !(ea.game && eb.game))
                conflict = false;
            // Two keys as the game or one mod set them: made to be shared.
            if (ea.atDefault && eb.atDefault && (ea.mod.empty() || eb.mod.empty() || ea.mod == eb.mod))
                conflict = false;
            auto& list = conflict ? out.conflicts : out.shared;
            list[a].push_back(eb.name);
            list[b].push_back(ea.name);
        }
    }
    return out;
}

std::string Keybinds::AddClassicDodge(const std::string& aXml, const std::string& aMapping)
{
    pugi::xml_document doc;
    if (!doc.load_string(aXml.c_str()))
        return {};
    bool changed = false;
    for (auto context : doc.child("bindings").children("context"))
    {
        if (!IsLiveContext(context.attribute("name").as_string()))
            continue;
        for (auto action : context.children("action"))
        {
            if (std::string(action.attribute("name").as_string()) != "Dodge")
                continue;
            auto added = context.insert_child_after("action", action);
            added.append_attribute("name") = kClassicDodge;
            added.append_attribute("map") = aMapping.c_str();
            changed = true;
            break;
        }
    }
    if (!changed)
        return {};
    std::ostringstream out;
    doc.save(out, "    ");
    return out.str();
}

std::map<std::string, std::vector<std::string>> Keybinds::OptionMappings(const fs::path& aMappings)
{
    std::map<std::string, std::vector<std::string>> out;
    pugi::xml_document doc;
    if (!doc.load_file(aMappings.c_str()))
        return out;
    for (auto mapping : doc.child("bindings").children("mapping"))
    {
        for (auto button : mapping.children("button"))
        {
            if (auto ui = button.attribute("overridableUI"))
                out[ui.as_string()].push_back(mapping.attribute("name").as_string());
        }
    }
    return out;
}
