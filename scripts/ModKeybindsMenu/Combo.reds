module ModKeybindsMenu

// The Key Bindings menu holds one key per action. For the mod sections it also holds key
// combinations: a mod's own (its buttonGroup id, like F2 then 2) and modifier + key picked here
// ("IK_LControl+IK_K"). They are shown as their keys.
//
// A key pressed while holding a modifier (Shift, Ctrl, Alt) makes the combination; a modifier
// pressed and released on its own is bound alone. The game binds a key only once every key is
// released, so the plugin watches the keyboard while the entry waits.

@addField(SettingsSelectorControllerKeyBinding)
private let m_modKeybindsBinding: Bool;

@addField(SettingsSelectorControllerKeyBinding)
private let m_modKeybindsCombo: CName;

public class ModKeybindsComboEvent extends Event {}

@wrapMethod(SettingsSelectorControllerKeyBinding)
protected cb func OnRelease(e: ref<inkPointerEvent>) -> Bool {
  if e.IsAction(n"click") || e.IsAction(n"tap") {
    let v = this.m_SettingsEntry as ConfigVarName;
    // Native Settings UI and RCF mods read a single key.
    if IsDefined(v) && ModKeybindsIsModGroup(v) && !StrBeginsWith(NameToString(v.GetName()), "NSUI_")
      && !ModKeybindsRcf.IsRcfKey(v.GetName()) {
      this.m_modKeybindsBinding = true;
      ModKeybindsMenu_WatchModifier();
    }
  }
  return wrappedMethod(e);
}

@wrapMethod(SettingsSelectorControllerKeyBinding)
public func Refresh() -> Void {
  wrappedMethod();
  let v = this.m_SettingsEntry as ConfigVarName;
  if !IsDefined(v) || !ModKeybindsIsModGroup(v) {
    return;
  }
  let value = v.GetValue();
  // Native results go into a variable first: used inline in ArraySize the call doesn't run.
  let keys = ModKeybindsMenu_ComboKeys(value);
  if this.m_modKeybindsBinding && !this.IsListeningForInput() {
    this.m_modKeybindsBinding = false;
    let held = ModKeybindsMenu_HeldModifier();
    if IsNameValid(held) && !ModKeybindsIsModifier(value) && ArraySize(keys) == 0 {
      // The game is still storing its key: set the combination once it is done.
      this.m_modKeybindsCombo = StringToName(NameToString(held) + "+" + NameToString(value));
      this.QueueEvent(new ModKeybindsComboEvent());
    }
  }
  if ArraySize(keys) >= 2 {
    inkRichTextBoxRef.SetText(this.m_text, ModKeybindsComboText(keys, v));
  }
}

@addField(SettingsMainGameController)
private let m_modKeybindsPopup: ref<inkGameNotificationToken>;

// The game applies key combinations from its input files at startup only.
@wrapMethod(SettingsMainGameController)
public func OnSettingsNotify(status: ConfigNotificationType) -> Void {
  wrappedMethod(status);
  if !Equals(status, ConfigNotificationType.Saved) && !Equals(status, ConfigNotificationType.ChangesApplied) {
    return;
  }
  let names = ModKeybindsMenu_Names();
  let groups = ModKeybindsMenu_Groups();
  let keys: array<CName>;
  let i = 0;
  while i < ArraySize(names) {
    let v = this.m_settings.GetVar(groups[i], names[i]) as ConfigVarName;
    ArrayPush(keys, IsDefined(v) ? v.GetValue() : n"None");
    i += 1;
  }
  let notice = ModKeybindsMenu_RestartNotice(names, keys);
  if notice {
    // The token has to be kept, or the popup closes at once.
    this.m_modKeybindsPopup = GenericMessageNotification.Show(this, "Native Mod Keybinds", "New key combinations apply after you restart the game. Single keys apply right away.", GenericMessageNotificationType.OK);
    this.m_modKeybindsPopup.RegisterListener(this, n"OnModKeybindsPopupClosed");
  }
}

@addMethod(SettingsMainGameController)
protected cb func OnModKeybindsPopupClosed(data: ref<inkGameNotificationData>) -> Bool {
  this.m_modKeybindsPopup = null;
  return true;
}

@addMethod(SettingsSelectorControllerKeyBinding)
protected cb func OnModKeybindsSetCombo(e: ref<ModKeybindsComboEvent>) -> Bool {
  let v = this.m_SettingsEntry as ConfigVarName;
  if IsDefined(v) && IsNameValid(this.m_modKeybindsCombo) {
    v.SetValue(this.m_modKeybindsCombo);
  }
  this.m_modKeybindsCombo = n"";
  return true;
}

private func ModKeybindsIsModGroup(v: ref<ConfigVarName>) -> Bool {
  // GetName gives the last part of the group path, e.g. SettingsModEnhancedVehicleSystem
  return StrContains(NameToString(v.GetGroup().GetName()), "SettingsMod");
}

private func ModKeybindsIsModifier(key: CName) -> Bool {
  return Equals(key, n"IK_LShift") || Equals(key, n"IK_RShift") || Equals(key, n"IK_LControl")
    || Equals(key, n"IK_RControl") || Equals(key, n"IK_Alt");
}

private func ModKeybindsComboText(keys: array<CName>, v: ref<ConfigVarName>) -> String {
  let group = v.GetGroup().GetName();
  let text = "";
  for key in keys {
    if StrLen(text) > 0 {
      text += " + ";
    }
    text += SettingsSelectorControllerKeyBinding.PrepareInputTag(key, group, v.GetName());
  }
  return text;
}
