module ModKeybindsMenu

// Keeps the mod sections in Controls > Key Bindings and Mod Settings key options in sync, and
// hands the keys to the plugin, which applies them to the input mappings.
// Mod Settings applies its saved key at startup, so it is the source when a session starts.
// After that, a change on either side is copied to the other.

// Mod Settings is optional: its classes are only named in the variant compiled when it is installed.
@if(ModuleExists("ModSettingsModule"))
public abstract class ModKeybindsModSettings {
  public static func Register(listener: ref<IScriptable>) -> Void {
    ModSettings.RegisterListenerToModifications(listener);
  }

  public static func Unregister(listener: ref<IScriptable>) -> Void {
    ModSettings.UnregisterListenerToModifications(listener);
  }

  private static func Option(name: CName) -> ref<ModConfigVarKeyBinding> {
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

  // True when Mod Settings has a key option of this name; key receives its key.
  public static func Key(name: CName, out key: CName) -> Bool {
    let kb = ModKeybindsModSettings.Option(name);
    if !IsDefined(kb) {
      return false;
    }
    key = kb.GetValueName();
    return true;
  }

  public static func SetKey(name: CName, key: CName) -> Void {
    let kb = ModKeybindsModSettings.Option(name);
    if IsDefined(kb) {
      kb.SetValueName(key);
      ModSettings.AcceptChanges();
    }
  }
}

@if(!ModuleExists("ModSettingsModule"))
public abstract class ModKeybindsModSettings {
  public static func Register(listener: ref<IScriptable>) -> Void {}

  public static func Unregister(listener: ref<IScriptable>) -> Void {}

  public static func Key(name: CName, out key: CName) -> Bool {
    return false;
  }

  public static func SetKey(name: CName, key: CName) -> Void {}
}

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
    ModKeybindsModSettings.Register(this);
  }

  private func OnDetach() -> Void {
    ModKeybindsModSettings.Unregister(this);
    ArrayClear(this.m_listeners);
  }

  private func MenuVar(group: CName, name: CName) -> ref<ConfigVarName> {
    let settings = GameInstance.GetSettingsSystem(this.GetGameInstance()).GetGroup(group);
    if !IsDefined(settings) {
      return null;
    }
    return settings.GetVar(name) as ConfigVarName;
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
      if IsDefined(menu) {
        // Mod sections are kept out of UserSettings.json between sessions (see the plugin),
        // so a session starts with them empty: take Mod Settings' key, else the saved one,
        // else the key from the mod's input file. IK_None (unbound by the player) is a key
        // like any other here; None means never set.
        let wanted = menu.GetValue();
        let modKey: CName;
        if ModKeybindsModSettings.Key(names[i], modKey) {
          wanted = modKey;
        }
        // Mod Settings holds one key: a key combination picked in this menu stays.
        let savedCombo = StrContains(NameToString(saved[i]), "+");
        if (Equals(wanted, n"None") || savedCombo) && NotEquals(saved[i], n"None") {
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
      ModKeybindsRcf.MenuChanged(this.GetGameInstance(), name, menu.GetValue());
    }
    let modKey: CName;
    // A key combination can't go into Mod Settings.
    if IsDefined(menu) && ModKeybindsModSettings.Key(name, modKey) && NotEquals(menu.GetValue(), modKey)
      && !StrContains(NameToString(menu.GetValue()), "+") {
      this.m_busy = true;
      ModKeybindsModSettings.SetKey(name, menu.GetValue());
      this.m_busy = false;
    }
  }

  // Called by Mod Settings when one of its options changes.
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
