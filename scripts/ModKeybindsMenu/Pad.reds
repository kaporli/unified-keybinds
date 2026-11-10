module ModKeybindsMenu

// Controller entries in Key Bindings. The game's own entries take keyboard keys only, so the
// plugin adds an entry for each controller button it can rebind. The menu shows them while a
// controller is in use and the keyboard entries otherwise, switching with the device like the
// game's button hints. An entry waiting for a key takes only keys of its own kind.

public abstract class ModKeybindsPad {
  public static func IsPadKey(key: CName) -> Bool {
    return StrBeginsWith(NameToString(key), "IK_Pad");
  }

  public static func IsTrigger(key: CName) -> Bool {
    return Equals(key, n"IK_Pad_LeftTrigger") || Equals(key, n"IK_Pad_RightTrigger");
  }

  // Default key of an entry, or None.
  public static func Default(name: CName) -> CName {
    let names = ModKeybindsMenu_Names();
    let defaults = ModKeybindsMenu_Defaults();
    let i = 0;
    while i < ArraySize(names) {
      if Equals(names[i], name) {
        return defaults[i];
      }
      i += 1;
    }
    return n"None";
  }

  public static func IsPadDevice(value: Uint32) -> Bool {
    return Cast<Int32>(value) % 256 == EnumInt(InputDevice.PAD);
  }

  // Entries of the Key Bindings tab that fit the device.
  public static func Filter(category: SettingsCategory, padNames: array<CName>, padMode: Bool) -> SettingsCategory {
    let out = category;
    ArrayClear(out.options);
    for option in category.options {
      if Equals(ArrayContains(padNames, option.GetName()), padMode) {
        ArrayPush(out.options, option);
      }
    }
    ArrayClear(out.subcategories);
    for sub in category.subcategories {
      let kept = ModKeybindsPad.Filter(sub, padNames, padMode);
      if ArraySize(kept.options) > 0 || ArraySize(kept.subcategories) > 0 {
        ArrayPush(out.subcategories, kept);
      }
    }
    return out;
  }
}

// Reports the device in use to the plugin: hold-only controller buttons wait for the hold only
// while a controller is in use, keyboard keys of the same action don't.
public class ModKeybindsDevice extends ScriptableSystem {
  private let m_callback: ref<CallbackHandle>;

  private func OnAttach() -> Void {
    let defs = GetAllBlackboardDefs().InputSchemes;
    let bb = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs);
    ModKeybindsMenu_SetDevice(ModKeybindsPad.IsPadDevice(bb.GetUint(defs.Device)));
    this.m_callback = bb.RegisterListenerUint(defs.Device, this, n"OnDevice");
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
  // The menu is built again when the Control Scheme screen closes; checked once it is up.
  this.QueueEvent(new ModKeybindsSchemeEvent());
  return result;
}

// Crouch, dodge and quick melee take their controller buttons from the control scheme. The
// controller entries are made at startup, for the scheme in use then.
@addMethod(SettingsMainGameController)
protected cb func OnModKeybindsScheme(e: ref<ModKeybindsSchemeEvent>) -> Bool {
  let scheme = this.m_settings.GetVar(n"/controls/controller", n"InputSchemesMode") as ConfigVarListString;
  if IsDefined(scheme) {
    let value = scheme.GetValue();
    let changed = ModKeybindsMenu_SchemeNotice(value);
    if changed {
      this.ModKeybindsShowPopup("The controller bindings in Key Bindings follow the new control scheme after you restart the game.");
    }
  }
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
  this.m_data[i] = ModKeybindsPad.Filter(all, padNames, this.m_modKeybindsPadMode);
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
    let padNames = ModKeybindsMenu_PadNames();
    let padEntry = IsDefined(v) && ArrayContains(padNames, v.GetName());
    let wrongKind = NotEquals(ModKeybindsPad.IsPadKey(e.keyName), padEntry);
    if padEntry && !wrongKind {
      wrongKind = NotEquals(ModKeybindsPad.IsTrigger(e.keyName), ModKeybindsPad.IsTrigger(ModKeybindsPad.Default(v.GetName())));
    }
    // The game stops listening once it hands out a key: start again for the next one.
    if wrongKind {
      this.StopListeningForInput();
      this.ListenForInput();
      return false;
    }
  }
  return wrappedMethod(e);
}
