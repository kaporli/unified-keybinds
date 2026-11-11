module ModKeybindsMenu

// Keys used by two actions that react at the same time, e.g. two mods on F3 (the plugin decides
// from the input contexts, see ModKeybindsMenu_Conflicts). Listed in a popup when they change and
// in red in the side panel of each entry involved.

@addField(SettingsMainGameController)
private let m_modKeybindsConflictNames: array<CName>;

@addField(SettingsMainGameController)
private let m_modKeybindsConflictLists: array<String>; // per name: the other entries' labels

// Every key binding entry, vanilla and mod.
@addMethod(SettingsMainGameController)
private func ModKeybindsKeyVars() -> array<ref<ConfigVarName>> {
  let out: array<ref<ConfigVarName>>;
  let root = this.m_settings.GetGroup(n"/key_bindings");
  if !IsDefined(root) {
    return out;
  }
  let groups = root.GetGroups(this.m_isPreGame);
  for group in groups {
    let groupVars = group.GetVars(this.m_isPreGame);
    for v in groupVars {
      let name = v as ConfigVarName;
      if IsDefined(name) {
        ArrayPush(out, name);
      }
    }
  }
  return out;
}

// A key binding entry by name, vanilla or mod.
@addMethod(SettingsMainGameController)
private func ModKeybindsKeyVar(name: CName) -> ref<ConfigVarName> {
  let vars = this.ModKeybindsKeyVars();
  for v in vars {
    if Equals(v.GetName(), name) {
      return v;
    }
  }
  return null;
}

// The label of an entry, with its section when asked: "Night Vision (Kiroshi Optics)".
private func ModKeybindsEntryLabel(v: ref<ConfigVarName>, section: Bool) -> String {
  let label = GetLocalizedTextByKey(v.GetDisplayName());
  if StrLen(label) == 0 {
    label = NameToString(v.GetDisplayName());
  }
  // Labels like "% / %" are filled with the keys the entry lists, as the game does.
  let i = 0;
  while i < v.GetDisplayNameKeysSize() && StrContains(label, "%") {
    label = StrReplace(label, "%", GetLocalizedTextByKey(v.GetDisplayNameKey(i)));
    i += 1;
  }
  let group = section ? GetLocalizedTextByKey(v.GetGroup().GetDisplayName()) : "";
  return StrLen(group) > 0 ? label + " (" + group + ")" : label;
}

// The key of an entry as the menu draws it: the icon of its key, or of both keys of a combination.
private func ModKeybindsKeyIcon(v: ref<ConfigVarName>) -> String {
  // Native results go into a variable first: used inline the call doesn't run.
  let keys = ModKeybindsMenu_ComboKeys(v.GetValue());
  if ArraySize(keys) == 0 {
    ArrayPush(keys, v.GetValue());
  }
  let group = v.GetGroup().GetName();
  let text = "";
  for key in keys {
    text += (StrLen(text) > 0 ? " + " : "") + SettingsSelectorControllerKeyBinding.PrepareInputTag(key, group, v.GetName());
  }
  return text;
}

// "IK_Pad_LeftShoulder" -> "LB", "IK_F3" -> "F3", a combination as "LB + A".
private func ModKeybindsKeyText(key: CName) -> String {
  let text = NameToString(key);
  let first: String;
  let second: String;
  if StrSplitFirst(text, "+", first, second) {
    return ModKeybindsKeyText(StringToName(first)) + " + " + ModKeybindsKeyText(StringToName(second));
  }
  switch key {
    case n"IK_Pad_A_CROSS": return "A";
    case n"IK_Pad_B_CIRCLE": return "B";
    case n"IK_Pad_X_SQUARE": return "X";
    case n"IK_Pad_Y_TRIANGLE": return "Y";
    case n"IK_Pad_LeftShoulder": return "LB";
    case n"IK_Pad_RightShoulder": return "RB";
    case n"IK_Pad_LeftTrigger": return "LT";
    case n"IK_Pad_RightTrigger": return "RT";
    case n"IK_Pad_LeftThumb": return "LS";
    case n"IK_Pad_RightThumb": return "RS";
    case n"IK_Pad_DigitUp": return "D-Pad Up";
    case n"IK_Pad_DigitDown": return "D-Pad Down";
    case n"IK_Pad_DigitLeft": return "D-Pad Left";
    case n"IK_Pad_DigitRight": return "D-Pad Right";
    case n"IK_Pad_Start": return "Menu";
    case n"IK_Pad_Back_Select": return "View";
  }
  return StrReplace(StrReplace(text, "IK_Pad_", ""), "IK_", "");
}

// Finds the conflicts for the keys as they are now. Returns one line per key for the popup, for the
// device in view (controller buttons or keyboard keys): "[key icon]  two of its actions +how many more".
@addMethod(SettingsMainGameController)
private func ModKeybindsCheckConflicts() -> String {
  ArrayClear(this.m_modKeybindsConflictNames);
  ArrayClear(this.m_modKeybindsConflictLists);
  let vars = this.ModKeybindsKeyVars();
  let names: array<CName>;
  let keys: array<CName>;
  let groups: array<CName>;
  let atDefault: array<Bool>;
  for v in vars {
    ArrayPush(names, v.GetName());
    ArrayPush(keys, v.GetValue());
    ArrayPush(groups, v.GetGroupPath());
    ArrayPush(atDefault, Equals(v.GetValue(), v.GetDefaultValue()));
  }
  // Native results go into a variable first: used inline the call doesn't run.
  let lists = ModKeybindsMenu_Conflicts(names, keys, groups, atDefault);
  let sharedKeys: array<CName>;
  let sharedIcons: array<String>;
  let sharedLabels: array<String>;
  let sharedCounts: array<Int32>;
  let i = 0;
  while i < ArraySize(lists) && i < ArraySize(vars) {
    if StrLen(lists[i]) > 0 {
      let labels = "";
      let others = StrSplit(lists[i], ",");
      for other in others {
        let j = ArrayFindFirst(names, StringToName(other));
        if j >= 0 {
          labels += (StrLen(labels) > 0 ? ", " : "") + ModKeybindsEntryLabel(vars[j], true);
        }
      }
      ArrayPush(this.m_modKeybindsConflictNames, names[i]);
      ArrayPush(this.m_modKeybindsConflictLists, labels);
      // For the popup: this entry under its key, when the key is of the device in view.
      if Equals(StrBeginsWith(NameToString(keys[i]), "IK_Pad"), this.m_modKeybindsPadMode) {
        let own = ModKeybindsEntryLabel(vars[i], false);
        let k = ArrayFindFirst(sharedKeys, keys[i]);
        if k < 0 {
          ArrayPush(sharedKeys, keys[i]);
          ArrayPush(sharedIcons, ModKeybindsKeyIcon(vars[i]));
          ArrayPush(sharedLabels, own);
          ArrayPush(sharedCounts, 1);
        } else {
          if !StrContains(", " + sharedLabels[k] + ", ", ", " + own + ", ") {
            if sharedCounts[k] < 2 {
              sharedLabels[k] += ", " + own;
            }
            sharedCounts[k] += 1;
          }
        }
      }
    }
    i += 1;
  }
  let lines = "";
  i = 0;
  while i < ArraySize(sharedKeys) {
    // Long action names are cut: the row has the key at its right end.
    let labels = sharedLabels[i];
    if StrLen(labels) > 32 {
      labels = StrLeft(labels, 30) + "...";
    }
    lines += "\n[" + sharedIcons[i] + "|" + ModKeybindsKeyText(sharedKeys[i]) + "]  " + labels;
    if sharedCounts[i] > 2 {
      lines += ", +" + IntToString(sharedCounts[i] - 2) + " more";
    }
    i += 1;
  }
  return lines;
}

// Popup text when the conflicts changed since the last popup, else "".
@addMethod(SettingsMainGameController)
private func ModKeybindsConflictPopup() -> String {
  let lines = this.ModKeybindsCheckConflicts();
  // Told once per device view: the first letter says which.
  let notice = ModKeybindsMenu_ConflictNotice((this.m_modKeybindsPadMode ? "P" : "K") + lines);
  if !notice {
    return "";
  }
  // The popup has no scrolling: a few keys here, the side panel of each key names what shares it.
  let keys = StrSplit(lines, "\n");
  let shown = "";
  let count = 0;
  for key in keys {
    if StrLen(key) > 0 {
      if count < 5 {
        shown += "\n" + key;
      }
      count += 1;
    }
  }
  if count > 5 {
    shown += "\n~+" + IntToString(count - 5) + " more keys";
  }
  return "Keys with more than one action:" + shown + "\n~Select a key to see what shares it.";
}

// The red side panel line for an entry, or "".
@addMethod(SettingsMainGameController)
private func ModKeybindsConflictLine(name: CName) -> String {
  let i = ArrayFindFirst(this.m_modKeybindsConflictNames, name);
  if i < 0 {
    return "";
  }
  return "<Rich color=\"MainColors.Red\">Also used by " + this.m_modKeybindsConflictLists[i] + ".</>";
}
