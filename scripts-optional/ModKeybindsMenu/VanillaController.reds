module ModKeybindsMenu

// Optional file Vanilla Controller Rebinding. The plugin lists the vanilla controller entries only
// while this file is installed.
//
// Settings > Controls > Control Scheme draws a controller with what each button does. The drawing
// follows the controller bindings from Key Bindings: a row whose button was changed moves to that
// button's place and shows its icon. An entry without a row (Sprint (Hold), Crouch (Hold),
// Scan (Toggle)) gets one once it has a button. Rows are listed per tab as
// "path|controller entry|default button|hold|entry of the texts after a /": the path from the
// screen's root; no entry for rows that can't be rebound. Only the scheme the controller entries
// were made for (the one in use at startup) is redrawn.

public class ModKeybindsSchemeRow {
  public let widget: wref<inkCompoundWidget>;
  public let entry: CName;
  public let button: CName;
  public let current: CName;
  public let hold: Bool;
  public let split: CName;
  public let moved: Bool;
}

public abstract class ModKeybindsScheme {
  // PlayStation controllers get their own drawing, with other panel and row names. A row name
  // used twice in a panel is told apart by its position: "L2#1" is the second "L2".
  public static func PlayStationRows(scheme: String) -> array<String> {
    let rows = [
      "Input/arrows/up||IK_Pad_DigitUp|0|",
      "Input/arrows/up_2|MKPad_useConsumable|IK_Pad_DigitUp|0|",
      "Input/arrows/up_3|MKPad_zoomIn|IK_Pad_DigitUp|0|",
      "Input/arrows/right|MKPad_callVehicle|IK_Pad_DigitRight|0|",
      "Input/arrows/right_hold|MKPad_callVehicle|IK_Pad_DigitRight|1|",
      "Input/arrows/down||IK_Pad_DigitDown|0|",
      "Input/arrows/down_1|MKPad_openNotifiation_DigitDown|IK_Pad_DigitDown|0|",
      "Input/arrows/down_2|MKPad_zoomOut|IK_Pad_DigitDown|0|",
      "Input/arrows/down_2#1|MKPad_cycleObjectives|IK_Pad_DigitDown|0|",
      "Input/arrows/down_hold|MKPad_openPhone|IK_Pad_DigitDown|1|",
      "Input/arrows/left_hold|MKPad_pocketRadio|IK_Pad_DigitLeft|1|",
      "Input/LS/L3_2|MKPad_sprintToggle|IK_Pad_LeftThumb|0|",
      "Input/R2/R2|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|0|",
      "Input/R2/R2_2|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|0|",
      "Input/R2/R2_hold|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|1|",
      "Input/R1/R2|MKPad_combatGadget|IK_Pad_RightShoulder|0|",
      "Input/R1/R2_hold|MKPad_combatGadget|IK_Pad_RightShoulder|1|",
      "Vehicles/L1/L1_hold|MKPad_visionHold|IK_Pad_LeftShoulder|1|",
      "Vehicles/arrows/left_hold|MKPad_crystalCoat|IK_Pad_DigitLeft|1|MKPad_vehicleCycleLights",
      "Vehicles/arrows/right|MKPad_vehicleToggleCam|IK_Pad_DigitRight|0|",
      "Vehicles/arrows/down|MKPad_cycleObjectives|IK_Pad_DigitDown|0|",
      "Vehicles/LS/L3|MKPad_vehicleCameraReset|IK_Pad_LeftThumb|0|",
      "Vehicles/R1/R1|MKPad_radio|IK_Pad_RightShoulder|0|",
      "Vehicles/R1/R1_hold|MKPad_radio|IK_Pad_RightShoulder|1|",
      "Vehicles/XYAB/square|MKPad_vehicleHorn|IK_Pad_X_SQUARE|0|",
      "Vehicles/XYAB/triangle|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "Vehicles/XYAB/triangle_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      "Vehicles/XYAB/circle_hold|MKPad_exitVehicle|IK_Pad_B_CIRCLE|1|",
      "Vehicles/XYAB/cross|MKPad_vehicleHandbrake|IK_Pad_A_CROSS|0|",
      "Vehicles/RS/R3_hold|MKPad_vehicleReverseCam|IK_Pad_RightThumb|1|",
      "VehicleCombat/L1/L1|MKPad_aim_LeftShoulder|IK_Pad_LeftShoulder|0|",
      "VehicleCombat/L1/L1_hold|MKPad_aim_LeftShoulder|IK_Pad_LeftShoulder|1|",
      "VehicleCombat/arrows/up|MKPad_useConsumable|IK_Pad_DigitUp|0|",
      "VehicleCombat/arrows/right|MKPad_vehicleToggleCam|IK_Pad_DigitRight|0|",
      "VehicleCombat/LS/L3|MKPad_vehicleCameraReset|IK_Pad_LeftThumb|0|",
      "VehicleCombat/R1/R1|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|0|",
      "VehicleCombat/R1/R1_2|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|0|",
      "VehicleCombat/R1/R1_hold|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|1|",
      "VehicleCombat/XYAB/square|MKPad_reload_X_SQUARE|IK_Pad_X_SQUARE|0|",
      "VehicleCombat/XYAB/triangle|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "VehicleCombat/XYAB/triangle_2|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "VehicleCombat/XYAB/triangle_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      "VehicleCombat/XYAB/circle_hold|MKPad_exitVehicle|IK_Pad_B_CIRCLE|1|",
      "VehicleCombat/XYAB/cross|MKPad_vehicleHandbrake|IK_Pad_A_CROSS|0|",
      "VehicleCombat/RS/R3_hold|MKPad_vehicleReverseCam|IK_Pad_RightThumb|1|"
    ];
    let s = "Input/" + scheme + "Scheme/";
    let common = [
      s + "XYAB/square|MKPad_selectChoice_X_SQUARE|IK_Pad_X_SQUARE|0|",
      s + "XYAB/square_2|MKPad_reload_X_SQUARE|IK_Pad_X_SQUARE|0|",
      s + "XYAB/triangle|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      s + "XYAB/triangle_2|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      s + "XYAB/triangle_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      s + "XYAB/circle|MKPad_dodgeDash|IK_Pad_B_CIRCLE|0|",
      s + "XYAB/cross|MKPad_jump|IK_Pad_A_CROSS|0|",
      s + "RS/R3_2|MKPad_tag|IK_Pad_RightThumb|0|"
    ];
    for row in common {
      ArrayPush(rows, row);
    }
    if Equals(scheme, "Legacy") {
      ArrayPush(rows, s + "L2/L2|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|");
      ArrayPush(rows, s + "L2/L2_2|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|");
      ArrayPush(rows, s + "L1/L1|MKPad_visionHold|IK_Pad_LeftShoulder|0|");
      ArrayPush(rows, s + "L1/L1_hold|MKPad_visionHold|IK_Pad_LeftShoulder|1|");
      ArrayPush(rows, s + "XYAB/circle_2|MKPad_crouchToggle|IK_Pad_B_CIRCLE|0|");
      ArrayPush(rows, s + "RS/R3_1|MKPad_quickmelee_RightThumb|IK_Pad_RightThumb|0|");
    } else {
      ArrayPush(rows, s + "L2/L1|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|");
      ArrayPush(rows, s + "L2/L1_2|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|");
      ArrayPush(rows, s + "L1/L2#1|MKPad_visionHold|IK_Pad_LeftShoulder|1|");
      if Equals(scheme, "Agile") {
        ArrayPush(rows, s + "L1/L2|MKPad_visionHold|IK_Pad_LeftShoulder|0|");
        ArrayPush(rows, s + "XYAB/circle_hold|MKPad_crouchToggle|IK_Pad_B_CIRCLE|1|");
        ArrayPush(rows, s + "RS/R3_1|MKPad_quickmelee_RightThumb|IK_Pad_RightThumb|0|");
      } else {
        ArrayPush(rows, s + "L1/L2|MKPad_quickmelee|IK_Pad_LeftShoulder|0|");
        ArrayPush(rows, s + "RS/R3_1|MKPad_crouchToggle|IK_Pad_RightThumb|0|");
      }
    }
    return rows;
  }

  public static func Rows(scheme: String) -> array<String> {
    let rows = [
      "Input/options|MKPad_hub|IK_Pad_Start|0|",
      "Input/LS/L3|MKPad_sprintToggle|IK_Pad_LeftThumb|0|",
      "Input/arrows/up|MKPad_useConsumable|IK_Pad_DigitUp|0|",
      "Input/arrows/up_2||IK_Pad_DigitUp|0|",
      "Input/arrows/up_3|MKPad_zoomIn|IK_Pad_DigitUp|0|",
      "Input/arrows/right|MKPad_callVehicle|IK_Pad_DigitRight|0|",
      "Input/arrows/down|MKPad_cycleObjectives|IK_Pad_DigitDown|0|",
      "Input/arrows/down1|MKPad_openNotifiation_DigitDown|IK_Pad_DigitDown|0|",
      "Input/arrows/down_2||IK_Pad_DigitDown|0|",
      "Input/arrows/down_3|MKPad_zoomOut|IK_Pad_DigitDown|0|",
      "Input/arrows_2/down_hold|MKPad_openPhone|IK_Pad_DigitDown|1|",
      "Input/arrows_2/right_hold|MKPad_callVehicle|IK_Pad_DigitRight|1|",
      "Input/arrows_2/left_hold|MKPad_pocketRadio|IK_Pad_DigitLeft|1|",
      "Input/RT/RT_1|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|0|",
      "Input/RT/RT_2|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|0|",
      "Input/RT/RT_hold|MKPad_attack_RightTrigger|IK_Pad_RightTrigger|1|",
      "Input/RB/RB_1|MKPad_combatGadget|IK_Pad_RightShoulder|0|",
      "Input/RB/RB_hold|MKPad_combatGadget|IK_Pad_RightShoulder|1|",
      "Vehicles/options|MKPad_hub|IK_Pad_Start|0|",
      "Vehicles/LB/L2|MKPad_visionHold|IK_Pad_LeftShoulder|1|",
      "Vehicles/LS/L3|MKPad_vehicleCameraReset|IK_Pad_LeftThumb|0|",
      "Vehicles/LS/LS_Hold|MKPad_vehicleAutodrive|IK_Pad_LeftThumb|1|",
      "Vehicles/arrows/right|MKPad_vehicleToggleCam|IK_Pad_DigitRight|0|",
      "Vehicles/arrows/down|MKPad_cycleObjectives|IK_Pad_DigitDown|0|",
      "Vehicles/arrows_2/left|MKPad_crystalCoat|IK_Pad_DigitLeft|1|MKPad_vehicleCycleLights",
      "Vehicles/arrows_2/down_hold|MKPad_openPhone|IK_Pad_DigitDown|1|",
      "Vehicles/RB/RB_1|MKPad_radio|IK_Pad_RightShoulder|0|",
      "Vehicles/RB/RB_hold|MKPad_radio|IK_Pad_RightShoulder|1|",
      "Vehicles/XYAB/X|MKPad_vehicleHorn|IK_Pad_X_SQUARE|0|",
      "Vehicles/XYAB/Y|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "Vehicles/XYAB/Y_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      "Vehicles/XYAB/A|MKPad_vehicleHandbrake|IK_Pad_A_CROSS|0|",
      "Vehicles/XYAB/B_hold|MKPad_exitVehicle|IK_Pad_B_CIRCLE|1|",
      "Vehicles/RS/R3_hold|MKPad_vehicleReverseCam|IK_Pad_RightThumb|1|",
      "VehicleCombat/options|MKPad_hub|IK_Pad_Start|0|",
      "VehicleCombat/LB/LB|MKPad_aim_LeftShoulder|IK_Pad_LeftShoulder|0|",
      "VehicleCombat/LB/LB_hold|MKPad_aim_LeftShoulder|IK_Pad_LeftShoulder|1|",
      "VehicleCombat/RB/RB|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|0|",
      "VehicleCombat/RB/RB_2|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|0|",
      "VehicleCombat/RB/RB_hold|MKPad_attack_RightShoulder|IK_Pad_RightShoulder|1|",
      "VehicleCombat/XYAB/X|MKPad_reload_X_SQUARE|IK_Pad_X_SQUARE|0|",
      "VehicleCombat/XYAB/Y|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "VehicleCombat/XYAB/Y_2|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      "VehicleCombat/XYAB/Y_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      "VehicleCombat/XYAB/A|MKPad_vehicleHandbrake|IK_Pad_A_CROSS|0|",
      "VehicleCombat/XYAB/B_hold|MKPad_exitVehicle|IK_Pad_B_CIRCLE|1|",
      "VehicleCombat/RS/R3_hold|MKPad_vehicleReverseCam|IK_Pad_RightThumb|1|",
      "VehicleCombat/LS/L3|MKPad_vehicleCameraReset|IK_Pad_LeftThumb|0|",
      "VehicleCombat/arrows/up|MKPad_useConsumable|IK_Pad_DigitUp|0|",
      "VehicleCombat/arrows/right|MKPad_vehicleToggleCam|IK_Pad_DigitRight|0|"
    ];
    let s = "Input/" + scheme + "Scheme/";
    let common = [
      s + "LT/LT_1|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|",
      s + "LT/LT_2|MKPad_aim_LeftTrigger|IK_Pad_LeftTrigger|0|",
      s + "LB/LB_hold|MKPad_visionHold|IK_Pad_LeftShoulder|1|",
      s + "XYAB/X|MKPad_selectChoice_X_SQUARE|IK_Pad_X_SQUARE|0|",
      s + "XYAB/X_2|MKPad_reload_X_SQUARE|IK_Pad_X_SQUARE|0|",
      s + "XYAB/Y|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      // A double tap of the weapon button (no mapping of its own).
      s + "XYAB/Y_2|MKPad_switchItem|IK_Pad_Y_TRIANGLE|0|",
      s + "XYAB/Y_hold|MKPad_switchItem|IK_Pad_Y_TRIANGLE|1|",
      s + "XYAB/B|MKPad_dodgeDash|IK_Pad_B_CIRCLE|0|",
      s + "XYAB/A|MKPad_jump|IK_Pad_A_CROSS|0|"
    ];
    for row in common {
      ArrayPush(rows, row);
    }
    if Equals(scheme, "Alternative") {
      ArrayPush(rows, s + "LB/LB_1|MKPad_quickmelee|IK_Pad_LeftShoulder|0|");
      ArrayPush(rows, s + "RS/RS|MKPad_crouchToggle|IK_Pad_RightThumb|0|");
      ArrayPush(rows, s + "RS/RS_2|MKPad_tag|IK_Pad_RightThumb|0|");
    } else {
      // Legacy and Agile: LB tap scans, B crouches (Agile: held), RS is quick melee.
      ArrayPush(rows, s + (Equals(scheme, "Agile") ? "LB/LB" : "LB/LB_1") + "|MKPad_visionHold|IK_Pad_LeftShoulder|0|");
      ArrayPush(rows, s + (Equals(scheme, "Agile") ? "XYAB/B_hold|MKPad_crouchToggle|IK_Pad_B_CIRCLE|1|" : "XYAB/B_2|MKPad_crouchToggle|IK_Pad_B_CIRCLE|0|"));
      ArrayPush(rows, s + "RS/R|MKPad_quickmelee_RightThumb|IK_Pad_RightThumb|0|");
      ArrayPush(rows, s + "RS/R2|MKPad_tag|IK_Pad_RightThumb|0|");
    }
    return rows;
  }

  // "a||b" -> ["a", "", "b"] (StrSplit leaves out empty fields), always five fields.
  public static func Fields(spec: String) -> array<String> {
    let out: array<String>;
    let rest = spec;
    let left: String;
    let right: String;
    while StrSplitFirst(rest, "|", left, right) {
      ArrayPush(out, left);
      rest = right;
    }
    ArrayPush(out, rest);
    while ArraySize(out) < 5 {
      ArrayPush(out, "");
    }
    return out;
  }

  public static func Child(parent: wref<inkCompoundWidget>, name: String) -> wref<inkWidget> {
    let wanted = name;
    let skip = 0;
    let number: String;
    if StrSplitFirst(name, "#", wanted, number) {
      skip = StringToInt(number);
    }
    let n = parent.GetNumChildren();
    let i = 0;
    while i < n {
      let w = parent.GetWidgetByIndex(i);
      if Equals(NameToString(w.GetName()), wanted) {
        if skip == 0 {
          return w;
        }
        skip -= 1;
      }
      i += 1;
    }
    return null;
  }

  public static func Find(root: wref<inkCompoundWidget>, path: String) -> wref<inkWidget> {
    let w: wref<inkWidget> = root;
    let rest = path;
    let part: String;
    let more = true;
    while more && IsDefined(w) {
      let tail: String;
      more = StrSplitFirst(rest, "/", part, tail);
      if !more {
        part = rest;
      }
      rest = tail;
      let compound = w as inkCompoundWidget;
      w = IsDefined(compound) ? ModKeybindsScheme.Child(compound, part) : null;
    }
    return w;
  }

  // Panel of a button on the drawing; the scheme's own panels first.
  public static func Group(tab: wref<inkCompoundWidget>, scheme: wref<inkCompoundWidget>, button: CName, hold: Bool, playStation: Bool) -> wref<inkCompoundWidget> {
    let name: String;
    switch button {
      case n"IK_Pad_LeftTrigger": name = playStation ? "L2" : "LT"; break;
      case n"IK_Pad_LeftShoulder": name = playStation ? "L1" : "LB"; break;
      case n"IK_Pad_RightTrigger": name = playStation ? "R2" : "RT"; break;
      case n"IK_Pad_RightShoulder": name = playStation ? "R1" : "RB"; break;
      case n"IK_Pad_LeftThumb": name = "LS"; break;
      case n"IK_Pad_RightThumb": name = "RS"; break;
      case n"IK_Pad_DigitUp":
      case n"IK_Pad_DigitDown":
      case n"IK_Pad_DigitLeft":
      case n"IK_Pad_DigitRight": name = hold && !playStation ? "arrows_2" : "arrows"; break;
      case n"IK_Pad_A_CROSS":
      case n"IK_Pad_B_CIRCLE":
      case n"IK_Pad_X_SQUARE":
      case n"IK_Pad_Y_TRIANGLE": name = "XYAB"; break;
      default: return null;
    }
    let group: wref<inkCompoundWidget>;
    if IsDefined(scheme) {
      group = ModKeybindsScheme.Child(scheme, name) as inkCompoundWidget;
    }
    if !IsDefined(group) {
      group = ModKeybindsScheme.Child(tab, name) as inkCompoundWidget;
    }
    return group;
  }

  // Panels on the left of the drawing put the icon after the text.
  public static func IsLeft(group: wref<inkCompoundWidget>) -> Bool {
    let name = NameToString(group.GetName());
    return Equals(name, "LT") || Equals(name, "LB") || Equals(name, "L2") || Equals(name, "L1") || Equals(name, "LS")
      || Equals(name, "arrows") || Equals(name, "arrows_2") || Equals(name, "share");
  }

  public static func Part(button: CName, hold: Bool) -> CName {
    let part: String;
    switch button {
      case n"IK_Pad_A_CROSS": part = "a"; break;
      case n"IK_Pad_B_CIRCLE": part = "b"; break;
      case n"IK_Pad_X_SQUARE": part = "x"; break;
      case n"IK_Pad_Y_TRIANGLE": part = "y"; break;
      case n"IK_Pad_LeftShoulder": part = "left_shoulder"; break;
      case n"IK_Pad_RightShoulder": part = "right_shoulder"; break;
      case n"IK_Pad_LeftTrigger": part = "left_trigger"; break;
      case n"IK_Pad_RightTrigger": part = "right_trigger"; break;
      // The stick buttons' hold icons are named differently.
      case n"IK_Pad_LeftThumb": return hold ? n"ls_filled" : n"left_thumb";
      case n"IK_Pad_RightThumb": return hold ? n"rs_filled" : n"right_thumb";
      case n"IK_Pad_DigitUp": part = "dpad_up"; break;
      case n"IK_Pad_DigitDown": part = "dpad_down"; break;
      case n"IK_Pad_DigitLeft": part = "dpad_left"; break;
      case n"IK_Pad_DigitRight": part = "dpad_right"; break;
      case n"IK_Pad_Start": part = "start"; break;
      case n"IK_Pad_Back_Select": part = "back"; break;
      default: return n"";
    }
    return StringToName(hold ? part + "_hold" : part);
  }

  // The row's icon: an image, or a "combo" panel (button + stick) whose first image is the button.
  public static func IconBlock(row: wref<inkCompoundWidget>) -> wref<inkWidget> {
    let n = row.GetNumChildren();
    let i = 0;
    while i < n {
      let w = row.GetWidgetByIndex(i);
      if IsDefined(w as inkImage) || Equals(w.GetName(), n"combo") {
        return w;
      }
      i += 1;
    }
    return null;
  }

  public static func FirstImage(block: wref<inkWidget>) -> wref<inkImage> {
    let image = block as inkImage;
    let compound = block as inkCompoundWidget;
    if !IsDefined(image) && IsDefined(compound) {
      let n = compound.GetNumChildren();
      let i = 0;
      while i < n && !IsDefined(image) {
        image = compound.GetWidgetByIndex(i) as inkImage;
        i += 1;
      }
    }
    return image;
  }

  public static func HasIcon(row: wref<inkCompoundWidget>) -> Bool {
    let block = ModKeybindsScheme.IconBlock(row);
    return IsDefined(block) && block.IsVisible();
  }

  public static func FirstText(row: wref<inkCompoundWidget>) -> wref<inkWidget> {
    let n = row.GetNumChildren();
    let i = 0;
    while i < n {
      let w = row.GetWidgetByIndex(i);
      if IsDefined(w as inkText) && w.IsVisible() {
        return w;
      }
      i += 1;
    }
    return null;
  }

  // Makes the row a full row: icon for its button, on the side its panel uses.
  public static func SetIcon(row: wref<inkCompoundWidget>, part: CName, left: Bool, template: wref<inkImage>) -> Void {
    let block = ModKeybindsScheme.IconBlock(row);
    if !IsDefined(block) && IsDefined(template) {
      // A new image like the drawing's own: same atlas (the controller's), size and color.
      let image = new inkImage();
      image.SetName(n"modKeybindsIcon");
      image.textureAtlas = template.textureAtlas;
      image.SetSize(template.GetSize());
      image.SetTintColor(template.GetTintColor());
      image.Reparent(row, -1);
      block = image;
    }
    if !IsDefined(block) {
      return;
    }
    block.SetVisible(true);
    let image = ModKeybindsScheme.FirstImage(block);
    if IsDefined(image) {
      image.SetTexturePart(part);
    }
    if left {
      row.ReorderChild(block, row.GetNumChildren() - 1);
      block.SetMargin(20.0, 0.0, 0.0, 0.0);
    } else {
      row.ReorderChild(block, 0);
      block.SetMargin(0.0, 0.0, 0.0, 0.0);
    }
    let text = ModKeybindsScheme.FirstText(row);
    if IsDefined(text) {
      text.SetMargin(left ? 0.0 : 20.0, 7.0, 0.0, 0.0);
    }
    row.SetMargin(0.0, -4.0, 0.0, 0.0);
  }

  // A row under the one above it, without an icon of its own.
  public static func SetFollowing(row: wref<inkCompoundWidget>, left: Bool) -> Void {
    let text = ModKeybindsScheme.FirstText(row);
    if IsDefined(text) {
      text.SetMargin(0.0, 0.0, 0.0, 0.0);
    }
    row.SetMargin(left ? 0.0 : 84.0, -8.0, left ? 84.0 : 0.0, 0.0);
  }

  // Moves the texts after the row's "/" to another row and hides the "/".
  public static func MoveSecond(row: wref<inkCompoundWidget>, to: wref<inkCompoundWidget>) -> Void {
    let after: array<wref<inkWidget>>;
    let n = row.GetNumChildren();
    let i = 0;
    let found = false;
    while i < n {
      let text = row.GetWidgetByIndex(i) as inkText;
      if IsDefined(text) {
        if Equals(text.GetText(), "/") {
          text.SetVisible(false);
          found = true;
        } else {
          if found {
            ArrayPush(after, text);
          }
        }
      }
      i += 1;
    }
    for text in after {
      text.Reparent(to, -1);
    }
  }

  public static func Apply(root: wref<inkCompoundWidget>, settings: ref<UserSettings>) -> Void {
    // Native results go into a variable first: used inline as an argument the call doesn't run.
    let schemeName = ModKeybindsMenu_PadScheme();
    let scheme = NameToString(schemeName);
    if StrLen(scheme) == 0 || Equals(scheme, "None") {
      return;
    }
    let names = ModKeybindsMenu_Names();
    let groups = ModKeybindsMenu_Groups();
    let rows: array<ref<ModKeybindsSchemeRow>>;
    let template: wref<inkImage>;
    let playStation = IsDefined(ModKeybindsScheme.Find(root, "Input/R2"));
    let specs = playStation ? ModKeybindsScheme.PlayStationRows(scheme) : ModKeybindsScheme.Rows(scheme);
    for spec in specs {
      let fields = ModKeybindsScheme.Fields(spec);
      let widget = ModKeybindsScheme.Find(root, fields[0]) as inkCompoundWidget;
      if IsDefined(widget) {
        let row = new ModKeybindsSchemeRow();
        row.widget = widget;
        row.entry = StringToName(fields[1]);
        row.button = StringToName(fields[2]);
        row.hold = Equals(fields[3], "1");
        row.split = StringToName(fields[4]);
        row.current = ModKeybindsScheme.Value(settings, names, groups, row.entry, row.button);
        ArrayPush(rows, row);
        if !IsDefined(template) {
          template = ModKeybindsScheme.FirstImage(ModKeybindsScheme.IconBlock(widget));
        }
      }
    }

    // A row naming two actions splits when they no longer share a button.
    let count = ArraySize(rows);
    let i = 0;
    while i < count {
      let row = rows[i];
      if IsNameValid(row.split) {
        let other = ModKeybindsScheme.Value(settings, names, groups, row.split, row.button);
        if NotEquals(other, row.current) {
          let parent = row.widget.GetParentWidget() as inkCompoundWidget;
          let copy = new inkHorizontalPanel();
          copy.SetName(n"modKeybindsSplit");
          copy.Reparent(parent, -1);
          ModKeybindsScheme.MoveSecond(row.widget, copy);
          let added = new ModKeybindsSchemeRow();
          added.widget = copy;
          added.entry = row.split;
          added.button = row.button;
          added.hold = row.hold;
          added.current = other;
          ArrayPush(rows, added);
        }
      }
      i += 1;
    }

    // Rows whose button changed go to that button's panel.
    let touched: array<wref<inkCompoundWidget>>;
    for row in rows {
      if NotEquals(row.current, row.button) {
        let from = row.widget.GetParentWidget() as inkCompoundWidget;
        // Unbound: left out of the drawing.
        if Equals(row.current, n"IK_None") {
          row.widget.SetVisible(false);
        }
        let tab = ModKeybindsScheme.Tab(row.widget);
        let schemeCanvas = ModKeybindsScheme.Child(tab, scheme + "Scheme") as inkCompoundWidget;
        let to = ModKeybindsScheme.Group(tab, schemeCanvas, row.current, row.hold, playStation);
        if IsDefined(to) && IsDefined(to as inkVerticalPanel) && NotEquals(to.GetName(), from.GetName()) {
          // Rows on the left of the drawing are right-aligned; take the alignment of the new panel's rows.
          if to.GetNumChildren() > 0 {
            row.widget.SetHAlign(to.GetWidgetByIndex(0).GetHAlign());
          }
          row.widget.Reparent(to, -1);
          row.moved = true;
          if !ModKeybindsScheme.Contains(touched, to) {
            ArrayPush(touched, to);
          }
        }
        if !ModKeybindsScheme.Contains(touched, from) {
          ArrayPush(touched, from);
        }
      }
    }

    // Entries the drawing has no row for (Sprint (Hold), Crouch (Hold), Scan (Toggle): no button by
    // default) get one at their button, on the on-foot tab. A hold when another row has the button:
    // they share it as tap and hold.
    let shared = ModKeybindsMenu_HoldShareNames();
    let inputTab = ModKeybindsScheme.Child(root, "Input") as inkCompoundWidget;
    let look = ArraySize(rows) > 0 ? ModKeybindsScheme.FirstText(rows[0].widget) as inkText : null;
    let j = 0;
    while j < ArraySize(shared) && IsDefined(inputTab) && IsDefined(look) {
      let entry = shared[j];
      // Pairs: the controller entry, then the game's keyboard option.
      j += 2;
      let current = ModKeybindsScheme.Value(settings, names, groups, entry, n"IK_None");
      if !ModKeybindsScheme.HasEntry(rows, entry) && IsNameValid(current) && NotEquals(current, n"IK_None") {
        let hold = false;
        for other in rows {
          if Equals(other.current, current) && !other.hold && Equals(ModKeybindsScheme.Tab(other.widget), inputTab) {
            hold = true;
          }
        }
        let schemeCanvas = ModKeybindsScheme.Child(inputTab, scheme + "Scheme") as inkCompoundWidget;
        let to = ModKeybindsScheme.Group(inputTab, schemeCanvas, current, hold, playStation);
        if IsDefined(to) && IsDefined(to as inkVerticalPanel) {
          let panel = new inkHorizontalPanel();
          panel.SetName(n"modKeybindsShared");
          if to.GetNumChildren() > 0 {
            panel.SetHAlign(to.GetWidgetByIndex(0).GetHAlign());
          }
          panel.Reparent(to, -1);
          let text = new inkText();
          text.SetName(n"modKeybindsText");
          text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily", look.GetFontStyle());
          text.SetFontSize(look.GetFontSize());
          text.SetTintColor(look.GetTintColor());
          text.SetVAlign(look.GetVAlign());
          text.SetText(ModKeybindsScheme.Label(settings, names, groups, entry));
          text.Reparent(panel, -1);
          let added = new ModKeybindsSchemeRow();
          added.widget = panel;
          added.entry = entry;
          added.button = n"IK_None";
          added.current = current;
          added.hold = hold;
          added.moved = true;
          ArrayPush(rows, added);
          if !ModKeybindsScheme.Contains(touched, to) {
            ArrayPush(touched, to);
          }
        }
      }
    }

    // In each panel that changed, a row gets an icon when the row above is another button.
    for group in touched {
      let left = ModKeybindsScheme.IsLeft(group);
      let previous = n"";
      let n = group.GetNumChildren();
      let k = 0;
      while k < n {
        let child = group.GetWidgetByIndex(k) as inkCompoundWidget;
        k += 1;
        if IsDefined(child) && child.IsVisible() {
          let row = ModKeybindsScheme.RowOf(rows, child);
          let key = IsDefined(row) ? StringToName(NameToString(row.current) + (row.hold ? "_hold" : "")) : n"";
          if IsDefined(row) {
            let icon = ModKeybindsScheme.HasIcon(child);
            let changed = row.moved || NotEquals(row.current, row.button);
            // A row keeps its icon (now for its button); one without gets one unless the row above is the same button.
            if (icon && changed) || (!icon && NotEquals(key, previous)) {
              ModKeybindsScheme.SetIcon(child, ModKeybindsScheme.Part(row.current, row.hold), left, template);
            } else {
              if row.moved {
                ModKeybindsScheme.SetFollowing(child, left);
              }
            }
          }
          previous = key;
        }
      }
    }
    if ArraySize(touched) > 0 {
      let tabs = [n"Input", n"Vehicles", n"VehicleCombat"];
      for name in tabs {
        let tab = ModKeybindsScheme.Child(root, NameToString(name)) as inkCompoundWidget;
        if IsDefined(tab) {
          let schemeCanvas = ModKeybindsScheme.Child(tab, scheme + "Scheme") as inkCompoundWidget;
          if playStation {
            ModKeybindsScheme.Stack(tab, schemeCanvas, ["L2", "L1", "arrows"]);
            ModKeybindsScheme.Stack(tab, schemeCanvas, ["R2", "R1", "XYAB"]);
          } else {
            ModKeybindsScheme.Stack(tab, schemeCanvas, ["LT", "LB", "LS", "arrows"]);
            ModKeybindsScheme.Stack(tab, schemeCanvas, ["RT", "RB", "XYAB"]);
          }
        }
      }
    }
  }

  public static func Contains(groups: array<wref<inkCompoundWidget>>, group: wref<inkCompoundWidget>) -> Bool {
    for g in groups {
      if Equals(g, group) {
        return true;
      }
    }
    return false;
  }

  // Height of a panel's rows as drawn: about 72 for a row with an icon, 58 for one under it (4K units).
  public static func Height(group: wref<inkCompoundWidget>) -> Float {
    let h = 0.0;
    let n = group.GetNumChildren();
    let i = 0;
    while i < n {
      let row = group.GetWidgetByIndex(i) as inkCompoundWidget;
      if IsDefined(row) && row.IsVisible() {
        h += ModKeybindsScheme.HasIcon(row) ? 72.0 : 58.0;
      }
      i += 1;
    }
    return h;
  }

  // Panels that grew push the next ones on their side down instead of overlapping them.
  public static func Stack(tab: wref<inkCompoundWidget>, scheme: wref<inkCompoundWidget>, names: array<String>) -> Void {
    let bottom = -1.0;
    for name in names {
      let group: wref<inkCompoundWidget>;
      if IsDefined(scheme) {
        group = ModKeybindsScheme.Child(scheme, name) as inkCompoundWidget;
      }
      if !IsDefined(group) {
        group = ModKeybindsScheme.Child(tab, name) as inkCompoundWidget;
      }
      if IsDefined(group) {
        let m = group.GetMargin();
        if bottom >= 0.0 && m.top < bottom + 12.0 {
          m.top = bottom + 12.0;
          group.SetMargin(m);
        }
        bottom = m.top + ModKeybindsScheme.Height(group);
      }
    }
  }

  public static func Value(settings: ref<UserSettings>, names: array<CName>, groups: array<CName>, entry: CName, button: CName) -> CName {
    if !IsNameValid(entry) {
      return button;
    }
    let i = 0;
    while i < ArraySize(names) {
      if Equals(names[i], entry) {
        let v = settings.GetVar(groups[i], entry) as ConfigVarName;
        return IsDefined(v) ? v.GetValue() : button;
      }
      i += 1;
    }
    return button;
  }

  public static func HasEntry(rows: array<ref<ModKeybindsSchemeRow>>, entry: CName) -> Bool {
    for row in rows {
      if Equals(row.entry, entry) {
        return true;
      }
    }
    return false;
  }

  // An entry's text as Key Bindings shows it.
  public static func Label(settings: ref<UserSettings>, names: array<CName>, groups: array<CName>, entry: CName) -> String {
    let i = 0;
    while i < ArraySize(names) {
      if Equals(names[i], entry) {
        let v = settings.GetVar(groups[i], entry) as ConfigVarName;
        return IsDefined(v) ? ModKeybindsEntryLabel(v, false) : "";
      }
      i += 1;
    }
    return "";
  }

  // The tab canvas (Input, Vehicles, VehicleCombat) a row is in.
  public static func Tab(widget: wref<inkWidget>) -> wref<inkCompoundWidget> {
    let w = widget;
    while IsDefined(w) {
      let name = w.GetName();
      if Equals(name, n"Input") || Equals(name, n"Vehicles") || Equals(name, n"VehicleCombat") {
        return w as inkCompoundWidget;
      }
      w = w.GetParentWidget();
    }
    return null;
  }

  public static func RowOf(rows: array<ref<ModKeybindsSchemeRow>>, widget: wref<inkCompoundWidget>) -> ref<ModKeybindsSchemeRow> {
    for row in rows {
      if Equals(row.widget, widget) {
        return row;
      }
    }
    return null;
  }
}

@wrapMethod(ControllerSettingsGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();
  ModKeybindsScheme.Apply(this.GetRootCompoundWidget(), this.m_settings);
  return result;
}

// Quick scan: a tap of the Scan (Hold) button. The game leaves it out on the Alternative scheme,
// where that button is also Quick Melee. With the two on different buttons the tap scans there too.
@wrapMethod(HUDManager)
protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
  let result = wrappedMethod(action, consumer);
  // Not during a hacking minigame (the game handles no input there), and only when the entries were
  // made for the Alternative scheme (a scheme changed in this session keeps the old entries).
  if ListenerAction.IsButtonJustPressed(action) && Equals(ListenerAction.GetName(action), n"Ping") && !this.IsHackingMinigameActive() {
    let game = this.GetGameInstance();
    let defs = GetAllBlackboardDefs().InputSchemes;
    let schemes = GameInstance.GetBlackboardSystem(game).Get(defs);
    if Equals(ConvertIntValueToInputScheme(schemes.GetUint(defs.Scheme)), InputScheme.ALTERNATIVE) && ModKeybindsPad.IsPadDevice(schemes.GetUint(defs.Device)) {
      // Native results go into a variable first: used inline as an argument the call doesn't run.
      let entriesScheme = ModKeybindsMenu_PadScheme();
      if NotEquals(entriesScheme, n"Alternative") {
        return result;
      }
      let names = ModKeybindsMenu_Names();
      let groups = ModKeybindsMenu_Groups();
      let settings = GameInstance.GetSettingsSystem(game);
      let scan = ModKeybindsScheme.Value(settings, names, groups, n"MKPad_visionHold", n"IK_Pad_LeftShoulder");
      let melee = ModKeybindsScheme.Value(settings, names, groups, n"MKPad_quickmelee", n"IK_Pad_LeftShoulder");
      if NotEquals(scan, melee) {
        this.StartPulse();
      }
    }
  }
  return result;
}
