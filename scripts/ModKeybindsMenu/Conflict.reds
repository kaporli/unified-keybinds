module ModKeybindsMenu

// Keys used by two actions that react at the same time, e.g. two mods on F3 (the plugin decides
// from the input contexts, see ModKeybindsMenu_Conflicts). A popup names a new one when a changed key
// is applied; the side panel lists the hovered entry's in red and every conflicting key below. A key shared by actions that are never active at
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
private let m_modKeybindsConflictRaw: array<String>; // per name: the other entries' names, comma separated

@addField(SettingsMainGameController)
private let m_modKeybindsPanelKeys: array<CName>; // every conflicting key in view, for the side panel

@addField(SettingsMainGameController)
private let m_modKeybindsPanelIcons: array<String>; // ...its key icon (not inside a color tag: it shows as text there)

@addField(SettingsMainGameController)
private let m_modKeybindsPanelRows: array<String>; // ...its actions

@addField(SettingsMainGameController)
private let m_modKeybindsPanelMore: array<Int32>; // ...and how many actions it leaves out

@addField(SettingsMainGameController)
private let m_modKeybindsSnapNames: array<CName>; // the keys at the last apply (or menu open)

@addField(SettingsMainGameController)
private let m_modKeybindsSnapKeys: array<CName>;

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
  ArrayClear(this.m_modKeybindsConflictRaw);
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
      ArrayPush(this.m_modKeybindsConflictRaw, lists[i]);
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
  ArrayClear(this.m_modKeybindsPanelKeys);
  ArrayClear(this.m_modKeybindsPanelIcons);
  ArrayClear(this.m_modKeybindsPanelRows);
  ArrayClear(this.m_modKeybindsPanelMore);
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
    // Side panel: the key's icon and its actions, the color set when an entry is hovered.
    ArrayPush(this.m_modKeybindsPanelKeys, sharedKeys[i]);
    ArrayPush(this.m_modKeybindsPanelIcons, sharedIcons[i]);
    ArrayPush(this.m_modKeybindsPanelRows, sharedLabels[i]);
    ArrayPush(this.m_modKeybindsPanelMore, sharedCounts[i] > 2 ? sharedCounts[i] - 2 : 0);
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

// Notes the keys as they are now: the next apply compares with them.
@addMethod(SettingsMainGameController)
private func ModKeybindsSnapshot() -> Void {
  ArrayClear(this.m_modKeybindsSnapNames);
  ArrayClear(this.m_modKeybindsSnapKeys);
  let vars = this.ModKeybindsKeyVars();
  for v in vars {
    ArrayPush(this.m_modKeybindsSnapNames, v.GetName());
    ArrayPush(this.m_modKeybindsSnapKeys, v.GetValue());
  }
}

// Popup text for the entries whose key changed since the snapshot and now conflicts: each with its key
// and what it conflicts with, or "".
@addMethod(SettingsMainGameController)
private func ModKeybindsNewConflicts() -> String {
  this.ModKeybindsCheckConflicts();
  let vars = this.ModKeybindsKeyVars();
  let names: array<CName>;
  for v in vars {
    ArrayPush(names, v.GetName());
  }
  let rows = "";
  let count = 0;
  for v in vars {
    let s = ArrayFindFirst(this.m_modKeybindsSnapNames, v.GetName());
    let changed = s < 0 || NotEquals(this.m_modKeybindsSnapKeys[s], v.GetValue());
    let c = ArrayFindFirst(this.m_modKeybindsConflictNames, v.GetName());
    if changed && c >= 0 {
      if count < 3 {
        // Native results go into a variable first: used inline the call doesn't run.
        let keyText = ModKeybindsMenu_KeyText(v.GetValue());
        rows += "\n[" + ModKeybindsKeyIcon(v) + "|" + keyText + "]  " + ModKeybindsEntryLabel(v, false);
        let others = StrSplit(this.m_modKeybindsConflictRaw[c], ",");
        let shown = 0;
        for other in others {
          let j = ArrayFindFirst(names, StringToName(other));
          if j >= 0 {
            if shown < 3 {
              // The row has no wrapping: long names are cut.
              let label = ModKeybindsEntryLabel(vars[j], false);
              if StrLen(label) > 36 {
                label = StrLeft(label, 34) + "...";
              }
              rows += "\n    with " + label;
            }
            shown += 1;
          }
        }
        if shown > 3 {
          rows += "\n~    +" + IntToString(shown - 3) + " more";
        }
      }
      count += 1;
    }
  }
  if count == 0 {
    return "";
  }
  if count > 3 {
    rows += "\n~+" + IntToString(count - 3) + " more keys";
  }
  return "New key conflict:" + rows + "\n~The side panel lists every conflict.";
}

// The side panel lines for an entry: what its key conflicts with (red) and is shared with (the panel's
// highlight color), headed by the key's icon; then, below a rule, every conflicting key of the device
// in view, the hovered entry's key in red. Or "".
@addMethod(SettingsMainGameController)
private func ModKeybindsConflictLine(v: ref<ConfigVarName>) -> String {
  let name = v.GetName();
  let icon = ModKeybindsKeyIcon(v);
  let text = "";
  let i = ArrayFindFirst(this.m_modKeybindsConflictNames, name);
  if i >= 0 {
    text = icon + "  <Rich color=\"MainColors.Red\">CONFLICTS WITH</>" + this.m_modKeybindsConflictLists[i];
  }
  let j = ArrayFindFirst(this.m_modKeybindsSharedNames, name);
  if j >= 0 {
    text += (StrLen(text) > 0 ? "\n\n" : "") + icon + "  <Rich " + this.m_descriptionWarningRichColor + ">IS SHARED WITH</>"
      + this.m_modKeybindsSharedLists[j];
  }
  let count = ArraySize(this.m_modKeybindsPanelKeys);
  if count > 0 {
    text += (StrLen(text) > 0 ? "\n\n" : "") + "<Rich color=\"MainColors.Grey\">________________________</>\n\n"
      + "<Rich color=\"MainColors.Grey\" size=\"30\">ALL CONFLICTS (" + IntToString(count) + (count == 1 ? " KEY)" : " KEYS)") + "</>";
    // A few one-line rows, the hovered entry's key first: the panel has no scrolling and ends above the
    // button hints.
    // In red when the hovered entry is part of that key's conflict, not only on the key.
    let own = i >= 0 ? ArrayFindFirst(this.m_modKeybindsPanelKeys, v.GetValue()) : -1;
    let order: array<Int32>;
    if own >= 0 {
      ArrayPush(order, own);
    }
    let k = 0;
    while k < count {
      if k != own {
        ArrayPush(order, k);
      }
      k += 1;
    }
    let shown = 0;
    for at in order {
      if shown < 4 {
        // One line per key: the second name moves into "+N more" when both don't fit, and a single long
        // name is cut.
        let row = this.m_modKeybindsPanelRows[at];
        let left = this.m_modKeybindsPanelMore[at];
        let first: String;
        let second: String;
        if StrLen(row) + (left > 0 ? 9 : 0) > 36 && StrSplitFirst(row, ", ", first, second) {
          row = first;
          left += 1;
        }
        let more = left > 0 ? ", +" + IntToString(left) + " more" : "";
        if StrLen(row) + StrLen(more) > 36 {
          row = StrLeft(row, 33 - StrLen(more)) + "...";
        }
        row += more;
        text += "\n<Rich size=\"12\"> </>\n" + this.m_modKeybindsPanelIcons[at] + "  <Rich color=\""
          + (at == own ? "MainColors.Red" : "MainColors.Grey") + "\">" + row + "</>";
      }
      shown += 1;
    }
    if count > 4 {
      text += "\n<Rich size=\"12\"> </>\n<Rich color=\"MainColors.Grey\">+" + IntToString(count - 4) + " more keys</>";
    }
  }
  return text;
}
