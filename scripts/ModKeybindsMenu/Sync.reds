module ModKeybindsMenu

// Keeps the mod sections in Controls > Key Bindings and Mod Settings key options in sync, and
// hands the keys to the plugin, which applies them to the input mappings.
// Mod Settings applies its saved key at startup, so it is the source when a session starts.
// After that, a change on either side is copied to the other.

public class ModKeybindsSystem extends ScriptableSystem {
  private let m_listeners: array<ref<ModKeybindsListener>>;
  private let m_busy: Bool;

  private func OnAttach() -> Void {
    let seen: array<CName>;
    for group in ModKeybindsMenu_Groups() {
      if !ArrayContains(seen, group) {
        ArrayPush(seen, group);
        let listener = new ModKeybindsListener();
        listener.Start(this, group);
        ArrayPush(this.m_listeners, listener);
      }
    }
    this.CopyModSettingsToMenu();
    ModSettings.RegisterListenerToModifications(this);
  }

  private func OnDetach() -> Void {
    ModSettings.UnregisterListenerToModifications(this);
    ArrayClear(this.m_listeners);
  }

  private func MenuVar(group: CName, name: CName) -> ref<ConfigVarName> {
    let settings = GameInstance.GetSettingsSystem(this.GetGameInstance()).GetGroup(group);
    if !IsDefined(settings) {
      return null;
    }
    return settings.GetVar(name) as ConfigVarName;
  }

  private func ModSettingsVar(name: CName) -> ref<ModConfigVarKeyBinding> {
    for mod in ModSettings.GetMods() {
      // GetCategories leaves out options without a category
      let categories = ModSettings.GetCategories(mod);
      if !ArrayContains(categories, n"None") {
        ArrayPush(categories, n"None");
      }
      for category in categories {
        for v in ModSettings.GetVars(mod, category) {
          if Equals(v.GetName(), name) {
            let kb = v as ModConfigVarKeyBinding;
            if IsDefined(kb) {
              return kb;
            }
          }
        }
      }
    }
    return null;
  }

  private func CopyModSettingsToMenu() -> Void {
    if this.m_busy {
      return;
    }
    this.m_busy = true;
    let settings = GameInstance.GetSettingsSystem(this.GetGameInstance());
    let names = ModKeybindsMenu_Names();
    let defaults = ModKeybindsMenu_Defaults();
    let groups = ModKeybindsMenu_Groups();
    let saved = ModKeybindsMenu_Saved();
    let changed = false;
    let keys: array<CName>;
    let i = 0;
    while i < ArraySize(names) {
      let menu = this.MenuVar(groups[i], names[i]);
      let kb = this.ModSettingsVar(names[i]);
      if IsDefined(menu) {
        // Mod sections are kept out of UserSettings.json between sessions (see the plugin),
        // so a session starts with them empty: take Mod Settings' key, else the saved one,
        // else the key from the mod's input file.
        let wanted = IsDefined(kb) ? kb.GetValueName() : menu.GetValue();
        if Equals(wanted, n"None") && NotEquals(saved[i], n"None") {
          wanted = saved[i];
        }
        if Equals(wanted, n"None") {
          wanted = defaults[i];
        }
        if NotEquals(menu.GetValue(), wanted) {
          menu.SetValue(wanted);
          changed = true;
        }
        ArrayPush(keys, wanted);
      } else {
        ArrayPush(keys, defaults[i]);
      }
      i += 1;
    }
    ModKeybindsMenu_SetKeys(names, keys);
    if changed {
      if settings.NeedsConfirmation() {
        settings.ConfirmChanges();
      }
      new inkMenuScenario().GetSystemRequestsHandler().RequestSaveUserSettings();
    }
    this.m_busy = false;
  }

  public func OnMenuChanged(group: CName, name: CName) -> Void {
    if this.m_busy {
      return;
    }
    let menu = this.MenuVar(group, name);
    if IsDefined(menu) {
      ModKeybindsMenu_SetKeys([name], [menu.GetValue()]);
    }
    let kb = this.ModSettingsVar(name);
    if IsDefined(menu) && IsDefined(kb) && NotEquals(menu.GetValue(), kb.GetValueName()) {
      this.m_busy = true;
      kb.SetValueName(menu.GetValue());
      ModSettings.AcceptChanges();
      this.m_busy = false;
    }
  }

  public func OnModSettingsChange() -> Void {
    this.CopyModSettingsToMenu();
  }
}

// Watches one mod section of the Key Bindings menu.
public class ModKeybindsListener extends ConfigVarListener {
  private let m_system: wref<ModKeybindsSystem>;
  private let m_group: CName;

  public func Start(system: ref<ModKeybindsSystem>, group: CName) -> Void {
    this.m_system = system;
    this.m_group = group;
    this.Register(group);
  }

  protected cb func OnVarModified(groupPath: CName, varName: CName, varType: ConfigVarType, reason: ConfigChangeReason) {
    if Equals(reason, ConfigChangeReason.Accepted) && Equals(groupPath, this.m_group) && IsDefined(this.m_system) {
      this.m_system.OnMenuChanged(groupPath, varName);
    }
  }
}
