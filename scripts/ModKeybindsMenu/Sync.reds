module ModKeybindsMenu

// Keeps the Controls > Key Bindings > Mods entries and Mod Settings key options in sync.
// Mod Settings applies its saved key at startup, so it is the source when a session starts.
// After that, a change on either side is copied to the other.

public class ModKeybindsSystem extends ScriptableSystem {
  private let m_listener: ref<ModKeybindsListener>;

  private func OnAttach() -> Void {
    this.m_listener = new ModKeybindsListener();
    this.m_listener.Start(this.GetGameInstance());
  }

  private func OnDetach() -> Void {
    if IsDefined(this.m_listener) {
      this.m_listener.Stop();
    }
  }
}

public class ModKeybindsListener extends ConfigVarListener {
  private let m_game: GameInstance;
  private let m_busy: Bool;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.CopyModSettingsToMenu();
    this.Register(n"/key_bindings/SettingsMods");
    ModSettings.RegisterListenerToModifications(this);
  }

  public func Stop() -> Void {
    ModSettings.UnregisterListenerToModifications(this);
  }

  private func MenuVar(name: CName) -> ref<ConfigVarName> {
    let group = GameInstance.GetSettingsSystem(this.m_game).GetGroup(n"/key_bindings/SettingsMods");
    if !IsDefined(group) {
      return null;
    }
    return group.GetVar(name) as ConfigVarName;
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
    let settings = GameInstance.GetSettingsSystem(this.m_game);
    let changed = false;
    let names = ModKeybindsMenu_Names();
    let defaults = ModKeybindsMenu_Defaults();
    let i = 0;
    while i < ArraySize(names) {
      let menu = this.MenuVar(names[i]);
      let kb = this.ModSettingsVar(names[i]);
      if IsDefined(menu) {
        // The game has no default for added entries, so a new entry starts as None
        let wanted = IsDefined(kb) ? kb.GetValueName() : menu.GetValue();
        if Equals(wanted, n"None") {
          wanted = defaults[i];
        }
        if NotEquals(menu.GetValue(), wanted) {
          menu.SetValue(wanted);
          changed = true;
        }
      }
      i += 1;
    }
    if changed {
      if settings.NeedsConfirmation() {
        settings.ConfirmChanges();
      }
      new inkMenuScenario().GetSystemRequestsHandler().RequestSaveUserSettings();
    }
    this.m_busy = false;
  }

  protected cb func OnVarModified(groupPath: CName, varName: CName, varType: ConfigVarType, reason: ConfigChangeReason) {
    if this.m_busy || !Equals(reason, ConfigChangeReason.Accepted) || !Equals(groupPath, n"/key_bindings/SettingsMods") {
      return;
    }
    let menu = this.MenuVar(varName);
    let kb = this.ModSettingsVar(varName);
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
