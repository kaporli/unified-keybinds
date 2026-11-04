-- Native Mod Keybinds: lists key bindings that mods add through Native Settings UI in
-- Settings > Controls > Key Bindings, and keeps both menus in sync.
--
-- Those bindings only exist here, after the menu sections are built, so they are reported to the
-- plugin each launch and listed from the next one. A key changed in Key Bindings goes to the mod
-- through Native Settings UI when changes are applied or the settings menu closes (the mod saves
-- and uses it right away). A key changed elsewhere (Native Settings UI, Unified Mod Settings, the
-- mod itself) goes to the Key Bindings entry within a second, or when the settings menu closes.

local prefix = "NSUI_"
local groupPrefix = "/key_bindings/SettingsMod"

local nativeSettings = nil
local entries = {}   -- name -> { option, group }
local synced = {}    -- name -> key both menus had at the last sync
local reported = false
local elapsed = 0
local settingsOpen = false
local saveWanted = false

-- Letters and digits only, as the plugin does.
local function slug(text)
    local out = tostring(text or ""):gsub("[^%w]", "")
    return out
end

local function text(label)
    label = tostring(label or "")
    if label:find("^LocKey#") then
        local localized = GetLocalizedText(label)
        if localized and localized ~= "" then return localized end
    end
    return label
end

local function isPad(key)
    return tostring(key or ""):find("^IK_Pad") ~= nil
end

-- The Key Bindings menu is for keyboard and mouse.
local function isController(option, sub)
    local words = (text(option.label) .. " " .. (sub and text(sub.label) or "")):lower()
    return isPad(option.value) or isPad(option.defaultValue) or words:find("controller") or words:find("gamepad")
end

local function collect()
    entries = {}
    local list = {}
    local seen = {}
    local function add(tab, option, sub)
        if option.type ~= "keyBinding" or type(option.label) ~= "string" or isController(option, sub) then return end
        local name = prefix .. slug(option.fullPath) .. "_" .. slug(option.label)
        if seen[name] then return end
        seen[name] = true
        local section = text(tab.label or tab.path)
        local label = text(option.label)
        if sub and sub.label then
            label = text(sub.label) .. ": " .. label
        end
        local group = groupPrefix .. (slug(section) ~= "" and slug(section) or "Other")
        entries[name] = { option = option, group = group }
        table.insert(list, { name = name, section = section, label = label, option = option })
    end
    for _, tab in pairs(nativeSettings.data) do
        for _, option in ipairs(tab.options or {}) do add(tab, option) end
        for _, key in ipairs(tab.keys or {}) do
            local sub = tab.subcategories[key]
            if sub then
                for _, option in ipairs(sub.options or {}) do add(tab, option, sub) end
            end
        end
    end
    return list
end

local function report(list)
    local names, sections, labels, keys, defaults = {}, {}, {}, {}, {}
    for _, e in ipairs(list) do
        table.insert(names, e.name)
        table.insert(sections, e.section)
        table.insert(labels, e.label)
        table.insert(keys, tostring(e.option.value or "IK_None"))
        table.insert(defaults, tostring(e.option.defaultValue or "IK_None"))
    end
    Game.ModKeybindsMenu_SetNativeSettings(names, sections, labels, keys, defaults)
end

local function menuVar(entry, name)
    local settings = Game.GetSettingsSystem()
    local group, var = CName.new(entry.group), CName.new(name)
    -- GetVar fails an engine assert for a group the menu doesn't have (listed from the next launch)
    if not settings or not settings:HasVar(group, var) then return nil end
    return settings:GetVar(group, var)
end

-- Passes on whichever side changed since the last sync.
local function sync()
    local changedMenu = false
    for name, entry in pairs(entries) do
        local var = menuVar(entry, name)
        if var then
            local menuKey = NameToString(var:GetValue())
            local modKey = tostring(entry.option.value)
            local last = synced[name]
            if menuKey ~= modKey then
                -- At startup the mod's key wins: a Key Bindings change was passed on when made.
                if last == nil or menuKey == last then
                    -- Changed in Native Settings UI, or outside the game
                    var:SetValue(StringToName(modKey))
                    changedMenu = true
                    menuKey = modKey
                else
                    -- Changed in Key Bindings
                    nativeSettings.setOption(entry.option, menuKey)
                end
            end
            synced[name] = menuKey
        end
    end
    if changedMenu then
        local settings = Game.GetSettingsSystem()
        if settings:NeedsConfirmation() then
            settings:ConfirmChanges()
        end
        saveWanted = true
    end
end

local function refresh()
    if not nativeSettings then return end
    local list = collect()
    sync()
    report(list)
end

-- A mod's key changed outside the settings menu (Unified Mod Settings, the mod itself). Read
-- again each time: some mods replace their options when a key changes.
local function modChanged()
    collect()
    for name, entry in pairs(entries) do
        if synced[name] ~= nil and tostring(entry.option.value) ~= synced[name] then return true end
    end
    return false
end

registerForEvent("onInit", function()
    nativeSettings = GetMod("nativeSettings")
    if not nativeSettings then return end

    ObserveAfter("SettingsMainGameController", "OnSettingsNotify", function()
        refresh()
    end)
    ObserveAfter("SettingsMainGameController", "OnInitialize", function()
        settingsOpen = true
    end)
    ObserveAfter("SettingsMainGameController", "OnUninitialize", function()
        settingsOpen = false
        refresh()
    end)
end)

-- Mods add their options during their own onInit, so look once they all ran.
-- After that, check once a second; while the settings menu is open, closing it syncs.
registerForEvent("onUpdate", function(delta)
    if not nativeSettings then return end
    elapsed = elapsed + delta
    if not reported then
        if elapsed < 3 then return end
        reported = true
        elapsed = 0
        refresh()
    elseif elapsed >= 1 then
        elapsed = 0
        if not settingsOpen and modChanged() then refresh() end
        -- A save asked for while another is running is dropped, so ask until it is done.
        if saveWanted and not settingsOpen then
            if Game.GetSettingsSystem():WasModifiedSinceLastSave() then
                inkMenuScenario.new():GetSystemRequestsHandler():RequestSaveUserSettings()
            else
                saveWanted = false
            end
        end
    end
end)
