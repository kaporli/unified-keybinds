module ModKeybindsMenu

// The plugin's popups with a list: the game's message popup shows one plain text. The first line stays
// as the message; the lines after it are drawn here like the menu's own rows, smaller, the action
// names in the title's red and the key's icon at the right.
//   "[<key icon tag>|LB]  Dodge/Dash, Roll Left"   a row with its key: the icon, and its name as text
//   "Crouch (Hold) / Dive"                      a row without one
//   "~+8 more keys"                             a note, dimmed

@wrapMethod(GenericMessageNotification)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  let message = inkWidgetRef.Get(this.m_message) as inkText;
  let title = inkWidgetRef.Get(this.m_title) as inkText;
  let parent = IsDefined(message) ? message.GetParentWidget() as inkCompoundWidget : null;
  let first: String;
  let rest: String;
  if !IsDefined(this.m_data) || !IsDefined(parent) || !IsDefined(title) || NotEquals(this.m_data.title, "Unified Keybinds")
    || !StrSplitFirst(this.m_data.message, "\n", first, rest) {
    return result;
  }
  message.SetText(first);
  let list = new inkVerticalPanel();
  list.SetName(n"modKeybindsList");
  let margin = message.GetMargin();
  list.SetMargin(margin.left, 14.0, margin.right, margin.bottom);
  margin.bottom = 0.0;
  message.SetMargin(margin);
  list.SetHAlign(message.GetHAlign());
  // Right after the message.
  let index = 0;
  let i = 0;
  while i < parent.GetNumChildren() {
    if Equals(parent.GetWidgetByIndex(i), message) {
      index = i + 1;
    }
    i += 1;
  }
  list.Reparent(parent, index);
  let size = Cast<Int32>(Cast<Float>(message.GetFontSize()) * 0.8);
  let lines = StrSplit(rest, "\n");
  let noted = false;
  for line in lines {
    let row = new inkCanvas();
    row.SetSize(820.0, Cast<Float>(size) * 1.9);
    row.Reparent(list, -1);
    let key: String;
    let label = line;
    let note = StrBeginsWith(line, "~");
    if note {
      label = StrMid(line, 1);
      // The notes stand a little apart from the rows.
      if !noted {
        row.SetMargin(0.0, 10.0, 0.0, 0.0);
      }
      noted = true;
    } else {
      if StrBeginsWith(line, "[") && StrSplitFirst(line, "]  ", key, label) {
        let icon: String;
        let name: String;
        StrSplitFirst(StrMid(key, 1), "|", icon, name);
        // Key icons need the rich text widget (as the menu's key boxes use); without one, the key's name.
        let keyText = Reflection.GetClass(n"inkRichTextBoxWidget").MakeHandle() as inkText;
        if !IsDefined(keyText) {
          keyText = new inkText();
          icon = name;
        }
        // The cyan of the menu's key icons.
        keyText.SetTintColor(new HDRColor(0.368, 0.964, 1.0, 1.0));
        keyText.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily", message.GetFontStyle());
        keyText.SetFontSize(size);
        keyText.SetAnchor(inkEAnchor.CenterRight);
        keyText.SetAnchorPoint(1.0, 0.5);
        keyText.SetText(icon);
        keyText.Reparent(row, -1);
      }
    }
    let text = new inkText();
    text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily", message.GetFontStyle());
    text.SetFontSize(size);
    text.SetTintColor(note ? message.GetTintColor() : title.GetTintColor());
    text.SetOpacity(note ? 0.55 : 1.0);
    text.SetAnchor(inkEAnchor.CenterLeft);
    text.SetAnchorPoint(0.0, 0.5);
    text.SetText(label);
    text.Reparent(row, -1);
  }
  return result;
}
