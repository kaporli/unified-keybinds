module ModKeybindsMenu

// Unbinding. The game only empties an entry when its key goes to another action, and then won't
// keep the changes while an action has no key. Here any entry in Key Bindings, vanilla or mod,
// keyboard or controller, is cleared by pointing at it and pressing the Unbind key shown in the
// hints (X, Square on a controller). An unbound entry holds IK_None, which the plugin keeps as the
// player's choice across save loads and restarts.

// Allow Unbound Actions replaces this method. A wrap works with or without it.
@wrapMethod(SettingsMainGameController)
private final func IsBlockedByActionWithoutAssignedKey() -> Bool {
  return false;
}

@wrapMethod(SettingsSelectorControllerKeyBinding)
protected cb func OnRelease(e: ref<inkPointerEvent>) -> Bool {
  // The game's Controller button on the Controls tab; it does nothing on Key Bindings.
  if e.IsAction(n"controller_settings") && !this.IsListeningForInput() {
    let v = this.m_SettingsEntry as ConfigVarName;
    if IsDefined(v) && NotEquals(v.GetValue(), n"IK_None") {
      v.SetValue(n"IK_None");
    }
  }
  return wrappedMethod(e);
}

@wrapMethod(SettingsMainGameController)
private final func SetSideButtonsVisiblity(i: Int32) -> Void {
  wrappedMethod(i);
  this.ModKeybindsUnbindHint(i);
}

@wrapMethod(SettingsMainGameController)
private final func PopulateHints() -> Void {
  wrappedMethod();
  this.ModKeybindsUnbindHint(this.m_settings.GetMenuIndex());
}

@addMethod(SettingsMainGameController)
private func ModKeybindsUnbindHint(i: Int32) -> Void {
  if !IsDefined(this.m_buttonHintsController) {
    return;
  }
  if IsDefined(this.m_buttonHintsController.RemoveItem(n"controller_settings")) {
    this.m_buttonHintsController.RemoveButtonHint(n"controller_settings");
  }
  if i >= 0 && i < ArraySize(this.m_menusList) && Equals(this.m_menusList[i], this.m_settingGroupName_KeyBindings) {
    this.m_buttonHintsController.AddButtonHint(n"controller_settings", "Unbind");
  }
}
