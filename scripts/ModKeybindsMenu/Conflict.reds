module ModKeybindsMenu

// Keys used by two actions that react at the same time, e.g. two mods on F3 (the plugin decides
// from the input contexts, see ModKeybindsMenu_Conflicts). Listed in a popup when they change and
// in red in the side panel of each entry involved. A key shared by actions that are never active at
// the same time (Crouch and a menu's Cancel) is no conflict: the side panel names them in the
// panel's highlight color, the popup leaves them out.

@addField(SettingsMainGameController)
private let m_modKeybindsConflictNames: array<CName>;

@addField(SettingsMainGameController)
private let m_modKeybindsConflictLists: array<String>; // per name: side panel rows of the other entries

@addField(SettingsMainGameController)
private let m_modKeybindsSharedNames: array<CName>;

@addField(SettingsMainGameController)
private let m_modKeybindsSharedLists: array<String>; // per name: side panel rows of the other entries

@addField(SettingsMainGameController)
private let m_modKeybindsConflictKeys: String; // the keys the last check was made for

@addField(SettingsMainGameController)
private let m_modKeybindsConflictLines: String; // ...and its popup lines

// Every key binding entry, vanilla and mod, without the game's options an entry of ours replaces.
@addMethod(SettingsMainGameController)
private func ModKeybindsKeyVars() -> array<ref<ConfigVarName>> {
  let out: array<ref<ConfigVarName>>;
  let root = this.m_settings.GetGroup(n"/key_bindings");
  if !IsDefined(root) {
    return out;
  }
  let names = ModKeybindsMenu_Names();
  let groups = root.GetGroups(this.m_isPreGame);
  for group in groups {
    let groupVars = group.GetVars(this.m_isPreGame);
    for v in groupVars {
      let name = v as ConfigVarName;
      if IsDefined(name) && !ModKeybindsPad.IsReplaced(name.GetName(), names) {
        ArrayPush(out, name);
      }
    }
  }
  return out;
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
  return ModKeybindsComboText(keys, v);
}

// Finds the conflicts for the keys as they are now. Returns one line per key for the popup, for the
// device in view (controller buttons or keyboard keys): "[key icon]  two of its actions +how many more".
@addMethod(SettingsMainGameController)
private func ModKeybindsCheckConflicts() -> String {
  let vars = this.ModKeybindsKeyVars();
  let names: array<CName>;
  let keys: array<CName>;
  let groups: array<CName>;
  let atDefault: array<Bool>;
  // The side panel asks on every entry pointed at: the same keys give the same answer.
  let current = this.m_modKeybindsPadMode ? "P" : "K";
  for v in vars {
    ArrayPush(names, v.GetName());
    ArrayPush(keys, v.GetValue());
    ArrayPush(groups, v.GetGroupPath());
    ArrayPush(atDefault, Equals(v.GetValue(), v.GetDefaultValue()));
    current += NameToString(v.GetValue()) + ";";
  }
  if Equals(current, this.m_modKeybindsConflictKeys) {
    return this.m_modKeybindsConflictLines;
  }
  this.m_modKeybindsConflictKeys = current;
  ArrayClear(this.m_modKeybindsConflictNames);
  ArrayClear(this.m_modKeybindsConflictLists);
  ArrayClear(this.m_modKeybindsSharedNames);
  ArrayClear(this.m_modKeybindsSharedLists);
  // Native results go into a variable first: used inline the call doesn't run.
  let results = ModKeybindsMenu_Conflicts(names, keys, groups, atDefault);
  // Per entry "conflicts;shared".
  let lists: array<String>;
  let i = 0;
  while i < ArraySize(results) && i < ArraySize(vars) {
    let conflicts: String;
    let shared: String;
    if !StrSplitFirst(results[i], ";", conflicts, shared) {
      conflicts = results[i];
      shared = "";
    }
    ArrayPush(lists, conflicts);
    if StrLen(shared) > 0 {
      ArrayPush(this.m_modKeybindsSharedNames, names[i]);
      ArrayPush(this.m_modKeybindsSharedLists, this.ModKeybindsRows(shared, names, vars));
    }
    i += 1;
  }
  let sharedKeys: array<CName>;
  let sharedIcons: array<String>;
  let sharedLabels: array<String>;
  let sharedCounts: array<Int32>;
  i = 0;
  while i < ArraySize(lists) && i < ArraySize(vars) {
    if StrLen(lists[i]) > 0 {
      ArrayPush(this.m_modKeybindsConflictNames, names[i]);
      ArrayPush(this.m_modKeybindsConflictLists, this.ModKeybindsRows(lists[i], names, vars));
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
    // Native results go into a variable first: used inline the call doesn't run.
    let keyText = ModKeybindsMenu_KeyText(sharedKeys[i]);
    lines += "\n[" + sharedIcons[i] + "|" + keyText + "]  " + labels;
    if sharedCounts[i] > 2 {
      lines += ", +" + IntToString(sharedCounts[i] - 2) + " more";
    }
    i += 1;
  }
  this.m_modKeybindsConflictLines = lines;
  return lines;
}

// Side panel rows for the entries in a comma list of names: the action in white, its section in grey
// ("Mod: " left out), one per line. A small empty line before each row spaces them like the popup's.
@addMethod(SettingsMainGameController)
private func ModKeybindsRows(list: String, names: array<CName>, vars: array<ref<ConfigVarName>>) -> String {
  let rows = "";
  let others = StrSplit(list, ",");
  for other in others {
    let j = ArrayFindFirst(names, StringToName(other));
    if j >= 0 {
      let section = GetLocalizedTextByKey(vars[j].GetGroup().GetDisplayName());
      if StrBeginsWith(section, "Mod: ") {
        section = StrMid(section, 5);
      }
      rows += "\n<Rich size=\"12\"> </>\n<Rich color=\"MainColors.White\">" + ModKeybindsEntryLabel(vars[j], false) + "</>";
      if StrLen(section) > 0 {
        rows += "  <Rich color=\"MainColors.Grey\">" + section + "</>";
      }
    }
  }
  return rows;
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
  return "Conflicting keys:" + shown + "\n~Select a key to see what shares it.";
}

// The side panel lines for an entry: a heading for what it conflicts with (red) and for what it
// shares its key with (the panel's highlight color), each with its rows, or "".
@addMethod(SettingsMainGameController)
private func ModKeybindsConflictLine(name: CName) -> String {
  let text = "";
  let i = ArrayFindFirst(this.m_modKeybindsConflictNames, name);
  if i >= 0 {
    text = "<Rich color=\"MainColors.Red\">CONFLICTS WITH:</>" + this.m_modKeybindsConflictLists[i];
  }
  let j = ArrayFindFirst(this.m_modKeybindsSharedNames, name);
  if j >= 0 {
    text += (StrLen(text) > 0 ? "\n\n" : "") + "<Rich " + this.m_descriptionWarningRichColor + ">SHARED WITH:</>"
      + this.m_modKeybindsSharedLists[j];
  }
  return text;
}
