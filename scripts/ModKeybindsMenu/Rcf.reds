module ModKeybindsMenu

@if(ModuleExists("RedscriptConfigFramework"))
import RedscriptConfigFramework.*

// Keys that mods keep in Redscript Configuration Framework (RCF), like the DigitalVixen mods.
// The menu entry sets the key binding (RCF's own binding would be overridden by it), so a change
// in the menu is copied to RCF, where the mod checks its modifier keys and RCF's hub shows them,
// and a change in RCF is copied to the menu. The key both sides had last tells which one changed.
// RCF's classes can't be wrapped from here without making RCF a requirement, so RCF is checked
// every 2 seconds in a session, when a session starts and when the settings menu opens.

public abstract class ModKeybindsRcf {
  // Called when a menu entry changed.
  @if(ModuleExists("RedscriptConfigFramework"))
  public static func MenuChanged(gi: GameInstance, name: CName, value: CName) -> Void {
    let sync = ModKeybindsRcfSync.Get(gi);
    if IsDefined(sync) {
      sync.MenuChanged(name, value);
    }
  }

  @if(!ModuleExists("RedscriptConfigFramework"))
  public static func MenuChanged(gi: GameInstance, name: CName, value: CName) -> Void {}

  // RCF stores a single key per entry.
  public static func IsRcfKey(name: CName) -> Bool {
    let names = ModKeybindsMenu_Names();
    let mods = ModKeybindsMenu_RcfMods();
    let i = 0;
    while i < ArraySize(names) && i < ArraySize(mods) {
      if Equals(names[i], name) {
        return StrLen(mods[i]) > 0;
      }
      i += 1;
    }
    return false;
  }
}

@if(ModuleExists("RedscriptConfigFramework"))
public class ModKeybindsRcfSync extends ScriptableSystem {
  private let m_names: array<CName>;
  private let m_groups: array<CName>;
  private let m_mods: array<String>;
  private let m_synced: array<CName>;  // key both sides had last, n"" until known
  private let m_pending: array<Bool>;  // changed in the menu while the mod wasn't registered in RCF
  private let m_busy: Bool;
  private let m_session: Int32;        // ends the checks of an earlier session

  public static func Get(gi: GameInstance) -> ref<ModKeybindsRcfSync> {
    return GameInstance.GetScriptableSystemsContainer(gi).Get(n"ModKeybindsMenu.ModKeybindsRcfSync") as ModKeybindsRcfSync;
  }

  private func OnAttach() -> Void {
    let names = ModKeybindsMenu_Names();
    let groups = ModKeybindsMenu_Groups();
    let mods = ModKeybindsMenu_RcfMods();
    let i = 0;
    while i < ArraySize(names) && i < ArraySize(mods) {
      if StrLen(mods[i]) > 0 {
        ArrayPush(this.m_names, names[i]);
        ArrayPush(this.m_groups, groups[i]);
        ArrayPush(this.m_mods, mods[i]);
        ArrayPush(this.m_synced, n"");
        ArrayPush(this.m_pending, false);
      }
      i += 1;
    }
    if ArraySize(this.m_names) > 0 {
      GameInstance.GetCallbackSystem()
        .RegisterCallback(n"Session/Ready", this, n"OnSessionReady")
        .SetLifetime(CallbackLifetime.Forever);
    }
  }

  // Mods register with RCF when a session starts.
  protected cb func OnSessionReady(event: ref<GameSessionEvent>) -> Void {
    this.m_session += 1;
    this.Schedule(3.0);
  }

  private func Schedule(delay: Float) -> Void {
    let cb = new ModKeybindsRcfCallback();
    cb.system = this;
    cb.session = this.m_session;
    GameInstance.GetDelaySystem(this.GetGameInstance()).DelayCallback(cb, delay, false);
  }

  public func Tick(session: Int32) -> Void {
    if session != this.m_session {
      return;
    }
    this.FromRcf();
    this.Schedule(2.0);
  }

  private func IndexOf(name: CName) -> Int32 {
    let i = 0;
    while i < ArraySize(this.m_names) {
      if Equals(this.m_names[i], name) {
        return i;
      }
      i += 1;
    }
    return -1;
  }

  private func MenuVar(i: Int32) -> ref<ConfigVarName> {
    return GameInstance.GetSettingsSystem(this.GetGameInstance()).GetVar(this.m_groups[i], this.m_names[i]) as ConfigVarName;
  }

  private func Provider(i: Int32) -> ref<DVRCF_Provider> {
    let reg = DVRCF_Registry.Get(this.GetGameInstance());
    let entry = IsDefined(reg) ? reg.GetEntry(this.m_mods[i]) : null;
    return IsDefined(entry) ? entry.provider : null;
  }

  private func IsFramework(i: Int32) -> Bool {
    return Equals(this.m_mods[i], "_DVRCF_Framework");
  }

  // RCF's key, or n"" while the mod isn't registered.
  private func RcfKey(i: Int32) -> CName {
    let raw: Int32;
    if this.IsFramework(i) {
      let settings = new DVRCFSettings();
      settings.Load(this.GetGameInstance());
      raw = settings.openKey > 0 ? settings.openKey : EnumInt(EInputKey.IK_F8);
    } else {
      let provider = this.Provider(i);
      if !IsDefined(provider) {
        return n"";
      }
      raw = provider.GetInt(NameToString(this.m_names[i]));
    }
    return EnumValueToName(n"EInputKey", Cast<Int64>(raw));
  }

  // False while the mod isn't registered.
  private func SetRcfKey(i: Int32, key: CName) -> Bool {
    let value = EnumValueFromName(n"EInputKey", key);
    if NotEquals(EnumValueToName(n"EInputKey", value), key) {
      return true;
    }
    let gi = this.GetGameInstance();
    if this.IsFramework(i) {
      let settings = new DVRCFSettings();
      settings.Load(gi);
      settings.openKey = Cast<Int32>(value);
      settings.Save(gi);
      let hub = DVRCF_Hub.Get(gi);
      if IsDefined(hub) {
        hub.RefreshModifierWatch();
      }
      return true;
    }
    if !IsDefined(this.Provider(i)) {
      return false;
    }
    DVRCF.SetInt(gi, this.m_mods[i], NameToString(this.m_names[i]), Cast<Int32>(value));
    return true;
  }

  public func MenuChanged(name: CName, value: CName) -> Void {
    let i = this.IndexOf(name);
    if i < 0 || this.m_busy || Equals(this.m_synced[i], value) {
      return;
    }
    this.m_busy = true;
    this.m_synced[i] = value;
    this.m_pending[i] = NotEquals(this.RcfKey(i), value) && !this.SetRcfKey(i, value);
    this.m_busy = false;
  }

  // Copies keys changed in RCF to the menu, and menu changes RCF missed to RCF.
  public func FromRcf() -> Void {
    if this.m_busy {
      return;
    }
    this.m_busy = true;
    let settings = GameInstance.GetSettingsSystem(this.GetGameInstance());
    let names: array<CName>;
    let keys: array<CName>;
    let i = 0;
    while i < ArraySize(this.m_names) {
      let menu = this.MenuVar(i);
      let rcf = this.RcfKey(i);
      if IsDefined(menu) && IsNameValid(rcf) {
        if this.m_pending[i] {
          this.m_pending[i] = !this.SetRcfKey(i, menu.GetValue());
          this.m_synced[i] = menu.GetValue();
        } else {
          if !IsNameValid(this.m_synced[i]) {
            this.m_synced[i] = menu.GetValue();
          }
          if NotEquals(rcf, this.m_synced[i]) {
            this.m_synced[i] = rcf;
            if NotEquals(menu.GetValue(), rcf) {
              menu.SetValue(rcf);
              ArrayPush(names, this.m_names[i]);
              ArrayPush(keys, rcf);
            }
          }
        }
      }
      i += 1;
    }
    if ArraySize(names) > 0 {
      ModKeybindsMenu_SetKeys(names, keys);
      if settings.NeedsConfirmation() {
        settings.ConfirmChanges();
      }
      new inkMenuScenario().GetSystemRequestsHandler().RequestSaveUserSettings();
    }
    this.m_busy = false;
  }
}

@if(ModuleExists("RedscriptConfigFramework"))
public class ModKeybindsRcfCallback extends DelayCallback {
  public let system: wref<ModKeybindsRcfSync>;
  public let session: Int32;

  public func Call() -> Void {
    if IsDefined(this.system) {
      this.system.Tick(this.session);
    }
  }
}

// The settings menu shows RCF's current keys.
@if(ModuleExists("RedscriptConfigFramework"))
@wrapMethod(SettingsMainGameController)
protected cb func OnInitialize() -> Bool {
  let sync = ModKeybindsRcfSync.Get(GetGameInstance());
  if IsDefined(sync) {
    sync.FromRcf();
  }
  return wrappedMethod();
}
