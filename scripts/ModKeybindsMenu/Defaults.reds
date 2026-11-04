module ModKeybindsMenu

// Defaults in Key Bindings resets an entry to the key its input mapping had at startup, which the
// game only knows for its own actions: mod entries came out empty. This sets them to the mod's key.

@addField(SettingsMainGameController)
private let m_modKeybindsRestoring: CName;

@wrapMethod(SettingsMainGameController)
private func RequestRestoreDefaults() -> Void {
  this.m_modKeybindsRestoring = this.m_data[this.m_groupsSelectorCtrl.GetToggledIndex()].groupPath;
  wrappedMethod();
}

@wrapMethod(SettingsMainGameController)
public func OnSettingsNotify(status: ConfigNotificationType) -> Void {
  // The reset ends with a refresh; other notifications (a Cancel) drop the request.
  let restoring = Equals(status, ConfigNotificationType.Refresh) ? this.m_modKeybindsRestoring : n"";
  this.m_modKeybindsRestoring = n"";
  if IsNameValid(restoring) {
    let path = NameToString(restoring);
    let names = ModKeybindsMenu_Names();
    let defaults = ModKeybindsMenu_Defaults();
    let groups = ModKeybindsMenu_Groups();
    let i = 0;
    while i < ArraySize(names) {
      let v = this.m_settings.GetVar(groups[i], names[i]) as ConfigVarName;
      let value = IsDefined(v) ? v.GetValue() : n"";
      // Only entries the reset emptied.
      if StrBeginsWith(NameToString(groups[i]), path) && (Equals(value, n"None") || Equals(value, n"IK_None")) {
        v.SetValue(defaults[i]);
      }
      i += 1;
    }
  }
  wrappedMethod(status);
}
