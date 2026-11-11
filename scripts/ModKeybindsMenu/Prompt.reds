module ModKeybindsMenu

// A (Hold) action sharing its key with another action gives way to an on-screen prompt on that
// key: while the game shows an interaction (talk, loot, use, a mod's pickpocket) whose input action
// is on the shared key, holding the key does the interaction and the (Hold) action doesn't start.
// The plugin drops the hold from the action for the device in use while it is blocked. A prompt's
// key is known by its input action (Choice1, ...): its raw key keeps the device it was made on.

public class ModKeybindsPrompt extends ScriptableSystem {
  private let m_actions: array<CName>;          // hold-only game actions, e.g. Sprint
  private let m_keyboard: array<Bool>;          // waits for the hold on the keyboard
  private let m_controller: array<Bool>;        // waits for the hold on the controller
  private let m_keyboardShared: array<String>;  // actions on its shared key, ",a,b," per device
  private let m_controllerShared: array<String>;
  private let m_hubCallback: ref<CallbackHandle>;
  private let m_lootCallback: ref<CallbackHandle>;
  private let m_deviceCallback: ref<CallbackHandle>;

  private func OnAttach() -> Void {
    for line in ModKeybindsMenu_HoldRules() {
      // The plugin writes "-" for a device without shared actions: StrSplit leaves out empty parts.
      let parts = StrSplit(line, "|");
      if ArraySize(parts) == 5 {
        ArrayPush(this.m_actions, StringToName(parts[0]));
        ArrayPush(this.m_keyboard, Equals(parts[1], "1"));
        ArrayPush(this.m_controller, Equals(parts[2], "1"));
        ArrayPush(this.m_keyboardShared, "," + parts[3] + ",");
        ArrayPush(this.m_controllerShared, "," + parts[4] + ",");
      }
    }
    if ArraySize(this.m_actions) == 0 {
      return;
    }
    let defs = GetAllBlackboardDefs();
    let ui = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.UIInteractions);
    this.m_hubCallback = ui.RegisterListenerVariant(defs.UIInteractions.InteractionChoiceHub, this, n"OnPromptsChanged");
    this.m_lootCallback = ui.RegisterListenerVariant(defs.UIInteractions.LootData, this, n"OnPromptsChanged");
    let schemes = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.InputSchemes);
    this.m_deviceCallback = schemes.RegisterListenerUint(defs.InputSchemes.Device, this, n"OnDeviceChanged");
  }

  private func OnDetach() -> Void {
    let defs = GetAllBlackboardDefs();
    let ui = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.UIInteractions);
    if IsDefined(this.m_hubCallback) {
      ui.UnregisterListenerVariant(defs.UIInteractions.InteractionChoiceHub, this.m_hubCallback);
    }
    if IsDefined(this.m_lootCallback) {
      ui.UnregisterListenerVariant(defs.UIInteractions.LootData, this.m_lootCallback);
    }
    if IsDefined(this.m_deviceCallback) {
      GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.InputSchemes).UnregisterListenerUint(defs.InputSchemes.Device, this.m_deviceCallback);
    }
    ModKeybindsMenu_SetHoldBlocked([]);
  }

  protected cb func OnPromptsChanged(value: Variant) -> Bool {
    this.Update();
    return true;
  }

  protected cb func OnDeviceChanged(value: Uint32) -> Bool {
    this.Update();
    return true;
  }

  // Input actions of the prompts on screen.
  private func PromptActions() -> array<String> {
    let actions: array<String>;
    let defs = GetAllBlackboardDefs();
    let ui = GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.UIInteractions);
    let hub = FromVariant<InteractionChoiceHubData>(ui.GetVariant(defs.UIInteractions.InteractionChoiceHub));
    if hub.active {
      for choice in hub.choices {
        ArrayPush(actions, NameToString(choice.inputAction));
      }
    }
    let loot = FromVariant<LootData>(ui.GetVariant(defs.UIInteractions.LootData));
    if loot.isActive {
      for choice in loot.choices {
        ArrayPush(actions, NameToString(choice.inputAction));
      }
    }
    return actions;
  }

  private func Update() -> Void {
    let defs = GetAllBlackboardDefs();
    let pad = ModKeybindsPad.IsPadDevice(GameInstance.GetBlackboardSystem(this.GetGameInstance()).Get(defs.InputSchemes).GetUint(defs.InputSchemes.Device));
    let prompts = this.PromptActions();
    let blocked: array<CName>;
    let i = 0;
    while i < ArraySize(this.m_actions) {
      if (pad && this.m_controller[i]) || (!pad && this.m_keyboard[i]) {
        let shared = pad ? this.m_controllerShared[i] : this.m_keyboardShared[i];
        for action in prompts {
          // A prompt's hold variant (Choice1_Hold) is on the same key as the prompt.
          if StrLen(action) > 0 && (StrContains(shared, "," + action + ",") || StrContains(shared, "," + action + "_Hold,")) {
            ArrayPush(blocked, this.m_actions[i]);
            break;
          }
        }
      }
      i += 1;
    }
    ModKeybindsMenu_SetHoldBlocked(blocked);
  }
}
