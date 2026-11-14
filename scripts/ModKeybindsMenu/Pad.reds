module ModKeybindsMenu

// Controller entries in Key Bindings. The game's own entries take keyboard keys only, so the
// plugin adds an entry for each controller button it can rebind. The menu shows them while a
// controller is in use and the keyboard entries otherwise, switching with the device like the
// game's button hints. An entry waiting for a key takes only keys of its own kind.

public abstract class ModKeybindsPad {
  public static func IsPadDevice(value: Uint32) -> Bool {
    return Cast<Int32>(value) % 256 == EnumInt(InputDevice.PAD);
  }

  // The control scheme in use changed: the game's controller entries take that scheme's keys (saved, else
  // its defaults) and apply at once.
  public static func FollowScheme(settings: ref<UserSettings>) -> Void {
    let scheme = settings.GetVar(n"/controls/controller", n"InputSchemesMode") as ConfigVarListString;
    if !IsDefined(scheme) {
      return;
    }
    // Native results go into a variable first: used inline the call doesn't run.
    let changed = ModKeybindsMenu_SetScheme(scheme.GetValue());
    if !changed {
      return;
    }
    let names = ModKeybindsMenu_Names();
    let groups = ModKeybindsMenu_Groups();
    let saved = ModKeybindsMenu_Saved();
    let defaults = ModKeybindsMenu_Defaults();
    let setNames: array<CName>;
    let setKeys: array<CName>;
    let i = 0;
    while i < ArraySize(names) {
      if StrBeginsWith(NameToString(groups[i]), "/key_bindings/SettingsModPad") {
        let key = Equals(saved[i], n"None") ? defaults[i] : saved[i];
        let v = settings.GetVar(groups[i], names[i]) as ConfigVarName;
        if IsDefined(v) && NotEquals(v.GetValue(), key) {
          v.SetValue(key);
        }
        ArrayPush(setNames, names[i]);
        ArrayPush(setKeys, key);
      }
      i += 1;
    }
    if settings.NeedsConfirmation() {
      settings.ConfirmChanges();
    }
    ModKeybindsMenu_SetKeys(setNames, setKeys);
    new inkMenuScenario().GetSystemRequestsHandler().RequestSaveUserSettings();
  }

  // True for a game option that has an entry of ours (MKKey_<option>), which takes its place.
  public static func IsReplaced(name: CName, names: array<CName>) -> Bool {
    return ArrayContains(names, StringToName("MKKey_" + NameToString(name)));
  }

  // Entries of the Key Bindings tab that fit the device. The game's options with an entry of ours
  // are left out.
  public static func Filter(category: SettingsCategory, padNames: array<CName>, names: array<CName>, padMode: Bool) -> SettingsCategory {
    let out = category;
    ArrayClear(out.options);
    for option in category.options {
      if Equals(ArrayContains(padNames, option.GetName()), padMode) && !ModKeybindsPad.IsReplaced(option.GetName(), names) {
        ArrayPush(out.options, option);
      }
    }
    ArrayClear(out.subcategories);
    for sub in category.subcategories {
      let kept = ModKeybindsPad.Filter(sub, padNames, names, padMode);
      if ArraySize(kept.options) > 0 || ArraySize(kept.subcategories) > 0 {
        ArrayPush(out.subcategories, kept);
      }
    }
    return out;
  }
}

// Reports the device in use to the plugin: hold-only controller buttons wait for the hold only
// while a controller is in use, keyboard keys of the same action don't. Also follows the control scheme.
public class ModKeybindsDevice extends ScriptableSystem {
  private let m_callback: ref<CallbackHandle>;
  private let m_scheme: ref<ModKeybindsSchemeListener>;

  private func OnAttach() -> Void {
    let defs = GetAllBlackboardDefs().InputSchemes;
    let bb = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs);
    ModKeybindsMenu_SetDevice(ModKeybindsPad.IsPadDevice(bb.GetUint(defs.Device)));
    this.m_callback = bb.RegisterListenerUint(defs.Device, this, n"OnDevice");
    this.m_scheme = new ModKeybindsSchemeListener();
    this.m_scheme.Start(this.GetGameInstance());
    ModKeybindsPad.FollowScheme(GameInstance.GetSettingsSystem(this.GetGameInstance()));
  }

  private func OnDetach() -> Void {
    if IsDefined(this.m_callback) {
      let defs = GetAllBlackboardDefs().InputSchemes;
      GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs).UnregisterListenerUint(defs.Device, this.m_callback);
      this.m_callback = null;
    }
  }

  protected cb func OnDevice(value: Uint32) -> Bool {
    ModKeybindsMenu_SetDevice(ModKeybindsPad.IsPadDevice(value));
    return true;
  }
}

// Watches the control scheme option (Settings > Controls).
public class ModKeybindsSchemeListener extends ConfigVarListener {
  private let m_game: GameInstance;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.Register(n"/controls/controller");
  }

  protected cb func OnVarModified(groupPath: CName, varName: CName, varType: ConfigVarType, reason: ConfigChangeReason) {
    if Equals(reason, ConfigChangeReason.Accepted) && Equals(varName, n"InputSchemesMode") {
      ModKeybindsPad.FollowScheme(GameInstance.GetSettingsSystem(this.m_game));
    }
  }
}

public class ModKeybindsSchemeEvent extends Event {}

@addField(SettingsMainGameController)
private let m_modKeybindsPadMode: Bool;

@addField(SettingsMainGameController)
private let m_modKeybindsDeviceCallback: ref<CallbackHandle>;

@wrapMethod(SettingsMainGameController)
protected cb func OnInitialize() -> Bool {
  let defs = GetAllBlackboardDefs().InputSchemes;
  let bb = this.GetBlackboardSystem().Get(defs);
  this.m_modKeybindsPadMode = ModKeybindsPad.IsPadDevice(bb.GetUint(defs.Device));
  this.m_modKeybindsDeviceCallback = bb.RegisterListenerUint(defs.Device, this, n"OnModKeybindsDevice");
  let result = wrappedMethod();
  // The keys as the menu opens: the conflict popup names only conflicts a later change makes.
  this.ModKeybindsSnapshot();
  ModKeybindsClickGuard.Get().skip = false;
  // The menu is built again when the Control Scheme screen closes; checked once it is up.
  this.QueueEvent(new ModKeybindsSchemeEvent());
  return result;
}

// Crouch, dodge and quick melee take their controller buttons from the control scheme: the entries
// follow a scheme changed here (also in the main menu, before a session's systems run).
@addMethod(SettingsMainGameController)
protected cb func OnModKeybindsScheme(e: ref<ModKeybindsSchemeEvent>) -> Bool {
  ModKeybindsPad.FollowScheme(this.m_settings);
  return true;
}

@wrapMethod(SettingsMainGameController)
protected cb func OnUninitialize() -> Bool {
  if IsDefined(this.m_modKeybindsDeviceCallback) {
    let defs = GetAllBlackboardDefs().InputSchemes;
    this.GetBlackboardSystem().Get(defs).UnregisterListenerUint(defs.Device, this.m_modKeybindsDeviceCallback);
    this.m_modKeybindsDeviceCallback = null;
  }
  return wrappedMethod();
}

@addMethod(SettingsMainGameController)
protected cb func OnModKeybindsDevice(value: Uint32) -> Bool {
  let padMode = ModKeybindsPad.IsPadDevice(value);
  if Equals(padMode, this.m_modKeybindsPadMode) {
    return false;
  }
  this.m_modKeybindsPadMode = padMode;
  // An entry waiting for a key is left alone; the list follows at the next change.
  if this.ModKeybindsOnKeyBindings() && !this.ModKeybindsListening() {
    this.PopulateCategorySettingsOptions(-1);
  }
  return true;
}

@addMethod(SettingsMainGameController)
private func ModKeybindsOnKeyBindings() -> Bool {
  let idx = this.m_groupsSelectorCtrl.GetToggledIndex();
  return idx >= 0 && idx < ArraySize(this.m_data) && Equals(this.m_data[idx].groupPath, n"/key_bindings");
}

@addMethod(SettingsMainGameController)
private func ModKeybindsListening() -> Bool {
  for element in this.m_settingsElements {
    let binding = element as SettingsSelectorControllerKeyBinding;
    if IsDefined(binding) && binding.IsListeningForInput() {
      return true;
    }
  }
  return false;
}

@wrapMethod(SettingsMainGameController)
private func PopulateCategorySettingsOptions(idx: Int32) -> Void {
  let i = idx < 0 ? this.m_groupsSelectorCtrl.GetToggledIndex() : idx;
  if i < 0 || i >= ArraySize(this.m_data) || NotEquals(this.m_data[i].groupPath, n"/key_bindings") {
    wrappedMethod(idx);
    return;
  }
  let all = this.m_data[i];
  let padNames = ModKeybindsMenu_PadNames();
  let names = ModKeybindsMenu_Names();
  this.m_data[i] = ModKeybindsPad.Filter(all, padNames, names, this.m_modKeybindsPadMode);
  wrappedMethod(idx);
  this.m_data[i] = all;
}

// The game's entries end their wait on any key; an entry of the other kind keeps waiting. A
// controller entry also keeps waiting for a trigger when it has a button, and the reverse: the game
// drops a button moved between the two.
@wrapMethod(SettingsSelectorControllerKeyBinding)
protected cb func OnKeyBindingEvent(e: ref<KeyBindingEvent>) -> Bool {
  if this.IsListeningForInput() && NotEquals(e.keyName, n"IK_Escape") {
    let v = this.m_SettingsEntry as ConfigVarName;
    // Native results go into a variable first: used inline as an argument the call doesn't run.
    let name = IsDefined(v) ? v.GetName() : n"";
    let fits = ModKeybindsMenu_Fits(name, e.keyName);
    let wrongKind = !fits;
    // The game stops listening once it hands out a key: start again for the next one.
    if wrongKind {
      this.StopListeningForInput();
      this.ListenForInput();
      return false;
    }
  }
  return wrappedMethod(e);
}

// Each control scheme keeps its own controller bindings. One that differs from its defaults is shown
// with a mark, e.g. "Dynamic*" (Controls and the Control Scheme screen).
@wrapMethod(SettingsSelectorControllerListString)
public func Refresh() -> Void {
  wrappedMethod();
  let value = this.m_SettingsEntry as ConfigVarListString;
  if IsDefined(value) && Equals(value.GetName(), n"InputSchemesMode") {
    // Native results go into a variable first: used inline the call doesn't run.
    let edited = ModKeybindsMenu_SchemeEdited(value.GetValue());
    if edited {
      inkTextRef.SetText(this.m_ValueText, inkTextRef.GetText(this.m_ValueText) + "*");
    }
  }
}
