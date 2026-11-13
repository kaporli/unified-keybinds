module ModKeybindsMenu

// The Key Bindings menu holds one key per action. For the mod sections it also holds key
// combinations: a mod's own (its buttonGroup id, like F2 then 2) and two keys picked here
// ("IK_LControl+IK_K"). They are shown as their keys.
//
// A key pressed while another is held makes the combination (held first); a key pressed and
// released on its own is bound alone. The game binds a key only once every key is released, so the
// plugin watches the keyboard and mouse while the entry waits. Controller entries work the same with
// two buttons ("IK_Pad_LeftShoulder+IK_Pad_A_CROSS"), triggers left out.

@addField(SettingsSelectorControllerKeyBinding)
private let m_modKeybindsBinding: Bool;

@addField(SettingsSelectorControllerKeyBinding)
private let m_modKeybindsCombo: CName;

@addField(SettingsSelectorControllerKeyBinding)
private let m_modKeybindsPadBinding: Bool;

public class ModKeybindsComboEvent extends Event {}

@wrapMethod(SettingsSelectorControllerKeyBinding)
protected cb func OnRelease(e: ref<inkPointerEvent>) -> Bool {
  // A (Cross) can be part of a controller combination: pressed while waiting it must not restart the watch.
  if (e.IsAction(n"click") || e.IsAction(n"tap")) && !this.IsListeningForInput() {
    let v = this.m_SettingsEntry as ConfigVarName;
    if ModKeybindsTakesCombo(v) {
      let padNames = ModKeybindsMenu_PadNames();
      let padEntry = ArrayContains(padNames, v.GetName());
      this.m_modKeybindsBinding = true;
      this.m_modKeybindsPadBinding = padEntry;
      ModKeybindsMenu_Watch(padEntry);
    }
  }
  let result = wrappedMethod(e);
  // The game's prompt while the entry waits says to press a key: this one says a combination works too.
  if this.m_modKeybindsBinding && this.IsListeningForInput() {
    inkTextRef.SetText(this.m_text, this.m_modKeybindsPadBinding ? "Press a button or combination" : "Press a key or combination");
  }
  return result;
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
    let combo = StringToName(ModKeybindsMenu_Combo());
    this.m_modKeybindsPadBinding = false;
    let comboKeys = ModKeybindsMenu_ComboKeys(combo);
    // Only when the game bound one of its keys, not after a cancel. The game is still storing its
    // key: the combination is set once it is done.
    if ArraySize(comboKeys) == 2 && (Equals(value, comboKeys[0]) || Equals(value, comboKeys[1])) {
      this.m_modKeybindsCombo = combo;
      this.QueueEvent(new ModKeybindsComboEvent());
    }
  }
  if ArraySize(keys) >= 2 {
    inkRichTextBoxRef.SetText(this.m_text, ModKeybindsComboText(keys, v));
  }
}

// The side panel says how to bind a combination, and that a (Hold) binding can share its key. The
// game shows one word ("Bind.") for every key binding, whatever options.json gives, so it is set here.
@wrapMethod(SettingsMainGameController)
protected cb func OnSettingHoverOver(evt: ref<inkPointerEvent>) -> Bool {
  let result = wrappedMethod(evt);
  let item = evt.GetCurrentTarget().GetController() as SettingsSelectorControllerKeyBinding;
  if !IsDefined(item) {
    return result;
  }
  let v = item.GetVar() as ConfigVarName;
  if !IsDefined(v) {
    return result;
  }
  // Native results go into a variable first: used inline as an argument the call doesn't run.
  let padNames = ModKeybindsMenu_PadNames();
  let sharing = ModKeybindsMenu_HoldShareNames();
  let pad = ArrayContains(padNames, v.GetName());
  let combo = ModKeybindsTakesCombo(v);
  let share = ArrayContains(sharing, v.GetName());
  this.ModKeybindsCheckConflicts();
  let conflict = this.ModKeybindsConflictLine(v.GetName());
  if !combo && !share {
    if StrLen(conflict) > 0 {
      inkTextRef.SetText(this.m_descriptionText, inkTextRef.GetText(this.m_descriptionText) + "\n\n" + conflict);
    }
    return result;
  }
  // In place of the game's "Bind.", laid out like its own panels: the description, the confirmation
  // note, and a restart line in the panel's highlight color.
  let thing = pad ? "button" : "key";
  let text = "Bind a " + thing + ".";
  if combo {
    text += "\nCombination: hold a " + thing + ", then press another.";
  }
  if share {
    text += "\nShared " + thing + ": tap for the other action, hold for this one.";
  }
  if Equals(item.GetVarUpdatePolicy(), ConfigVarUpdatePolicy.ConfirmationRequired) {
    text += "\n\n" + GetLocalizedText("LocKey#76947");
  }
  // A shared key counts at once, on both devices.
  let restart = combo ? "Combinations apply after a restart." : "";
  if StrLen(restart) > 0 {
    text += "\n\n<Rich " + this.m_descriptionWarningRichColor + ">" + restart + "</>";
  }
  if StrLen(conflict) > 0 {
    text += "\n\n" + conflict;
  }
  inkTextRef.SetText(this.m_descriptionText, text);
  return result;
}

@addField(SettingsMainGameController)
private let m_modKeybindsPopup: ref<inkGameNotificationToken>;

// The game applies key combinations from its input files at startup only. It also drops one of two
// unbound buttons of a mapping (an action's key and controller button) until it restarts.
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
  // The entries that only change once the game restarts, by name.
  let pending = ModKeybindsMenu_RestartNotice(names, keys);
  let text = "";
  let listed = 0;
  for name in pending {
    let at = ArrayFindFirst(names, name);
    let entry = at >= 0 ? this.m_settings.GetVar(groups[at], name) as ConfigVarName : null;
    if IsDefined(entry) {
      if listed < 5 {
        text += "\n" + ModKeybindsEntryLabel(entry, false);
      }
      listed += 1;
    }
  }
  if listed > 5 {
    text += "\n~+" + IntToString(listed - 5) + " more";
  }
  if listed > 0 {
    text = "Restart the game to apply:" + text;
  }
  // One message at a time: the conflicts come when Key Bindings opens next, and stay in the side panel.
  if StrLen(text) == 0 && !IsDefined(this.m_modKeybindsPopup) {
    text = this.ModKeybindsConflictPopup();
  }
  if StrLen(text) > 0 {
    this.ModKeybindsShowPopup(text);
  }
}

@addMethod(SettingsMainGameController)
private func ModKeybindsShowPopup(text: String) -> Void {
  // One at a time: a new popup would replace the open one's token and close it.
  if IsDefined(this.m_modKeybindsPopup) {
    return;
  }
  // The token has to be kept, or the popup closes at once.
  this.m_modKeybindsPopup = GenericMessageNotification.Show(this, "Unified Keybinds", text, GenericMessageNotificationType.OK);
  this.m_modKeybindsPopup.RegisterListener(this, n"OnModKeybindsPopupClosed");
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

// True for entries that take a key combination, keyboard and controller alike (see the plugin's
// TakesCombo: a button mapping, not a Native Settings UI or RCF key, not a trigger).
private func ModKeybindsTakesCombo(v: ref<ConfigVarName>) -> Bool {
  if !IsDefined(v) || !ModKeybindsIsModGroup(v) {
    return false;
  }
  // Native results go into a variable first: used inline the call doesn't run.
  let takes = ModKeybindsMenu_TakesCombo(v.GetName());
  return takes;
}

private func ModKeybindsIsModGroup(v: ref<ConfigVarName>) -> Bool {
  // GetName gives the last part of the group path, e.g. SettingsModEnhancedVehicleSystem
  return StrContains(NameToString(v.GetGroup().GetName()), "SettingsMod");
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
