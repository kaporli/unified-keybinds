#include "NativeSettings.hpp"

#include <fstream>
#include <sstream>

#include <json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace
{
fs::path CachePath(const fs::path& aRoot)
{
    return aRoot / "r6" / "cache" / "modkeybinds" / "nativesettings.json";
}

// Without CET, Native Settings UI or the companion the entries would do nothing.
bool Available(const fs::path& aRoot)
{
    auto mods = aRoot / "bin" / "x64" / "plugins" / "cyber_engine_tweaks" / "mods";
    std::error_code ec;
    return fs::exists(mods / "nativeSettings" / "init.lua", ec) && fs::exists(mods / "ModKeybindsMenu" / "init.lua", ec);
}
} // namespace

std::vector<Keybinds::Action> NativeSettings::Read(const fs::path& aRoot)
{
    if (!Available(aRoot))
        return {};
    std::ifstream in(CachePath(aRoot), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    auto data = json::parse(ss.str(), nullptr, false);
    if (!data.is_array())
        return {};

    std::vector<Keybinds::Action> out;
    for (const auto& e : data)
    {
        if (!e.is_object())
            continue;
        Keybinds::Action a;
        a.name = e.value("name", "");
        a.section = e.value("section", "");
        a.label = e.value("label", "");
        a.defaultKey = e.value("default", "");
        a.current = e.value("key", "");
        if (a.name.rfind(kNamePrefix, 0) != 0 || a.section.empty() || a.label.empty())
            continue;
        if (a.defaultKey.empty())
            a.defaultKey = "IK_None";
        out.push_back(std::move(a));
    }
    return out;
}

bool NativeSettings::Write(const fs::path& aRoot, const std::vector<Entry>& aEntries)
{
    json data = json::array();
    for (const auto& e : aEntries)
    {
        data.push_back(
            {{"name", e.name}, {"section", e.section}, {"label", e.label}, {"key", e.key}, {"default", e.defaultKey}});
    }
    auto path = CachePath(aRoot);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data.dump(4);
    out.close();
    return static_cast<bool>(out);
}
