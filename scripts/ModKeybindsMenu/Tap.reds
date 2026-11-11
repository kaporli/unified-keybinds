module ModKeybindsMenu

// Sprint (Toggle) on a button shared with a (Hold) action: tap to sprint, hold for the other. The game
// sends the button's press to Sprint (Toggle) before the hold completes, so holding would sprint and
// then slide. On that device the plugin keeps the button's events from Sprint (Toggle); a tap is the
// (Hold) action let go before its hold completed, and toggles sprint here as the game's own tap does.
//
// Also, on any layout: the game holds back a press on a button that has a hold for about 0.12 s, so a
// quick tap brings its press and release in the same frame. Sprint (Toggle) only checks its start
// while its button reads pressed and misses those taps (seen on the default L3: an 80 ms tap did
// nothing, a 200 ms one sprinted); they count as a tap here.

@addField(SprintDecisions)
private let m_modKeybindsShared: array<CName>; // (Hold) actions sharing a button with Sprint (Toggle)

@addField(SprintDecisions)
private let m_modKeybindsSharedKeyboard: array<Bool>; // ...on the keyboard

@addField(SprintDecisions)
private let m_modKeybindsSharedPad: array<Bool>; // ...on the controller

@addField(SprintDecisions)
private let m_modKeybindsHeld: Bool; // the shared button's hold completed

@addField(SprintDecisions)
private let m_modKeybindsPressFrame: Uint64; // frame of Sprint (Toggle)'s last press

@addField(SprintDecisions)
private let m_modKeybindsTapFrame: Uint64; // frame of the last tap, 0 once used

@wrapMethod(SprintDecisions)
protected func OnAttach(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  // "action|keyboard|controller|keyboardShared|controllerShared", see ModKeybindsMenu_HoldRules.
  let rules = ModKeybindsMenu_HoldRules();
  for line in rules {
    let parts = StrSplit(line, "|");
    if ArraySize(parts) == 5 {
      let keyboard = Equals(parts[1], "1") && StrContains("," + parts[3] + ",", ",ToggleSprint,");
      let pad = Equals(parts[2], "1") && StrContains("," + parts[4] + ",", ",ToggleSprint,");
      if keyboard || pad {
        let action = StringToName(parts[0]);
        ArrayPush(this.m_modKeybindsShared, action);
        ArrayPush(this.m_modKeybindsSharedKeyboard, keyboard);
        ArrayPush(this.m_modKeybindsSharedPad, pad);
        if NotEquals(action, n"Sprint") {
          scriptInterface.executionOwner.RegisterInputListener(this, action);
        }
      }
    }
  }
}

@wrapMethod(SprintDecisions)
protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
  let name = ListenerAction.GetName(action);
  let shared = ArrayFindFirst(this.m_modKeybindsShared, name);
  let result = true;
  // The game's handling is for its own actions; a (Hold) action listened to here only counts its taps.
  if shared < 0 || Equals(name, n"Sprint") {
    result = wrappedMethod(action, consumer);
  }
  let game = GetGameInstance();
  let frame = GameInstance.GetFrameNumber(game);
  if shared >= 0 {
    let defs = GetAllBlackboardDefs().InputSchemes;
    let pad = ModKeybindsPad.IsPadDevice(GameInstance.GetBlackboardSystem(game).Get(defs).GetUint(defs.Device));
    if pad ? this.m_modKeybindsSharedPad[shared] : this.m_modKeybindsSharedKeyboard[shared] {
      if Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_HOLD_COMPLETE) {
        this.m_modKeybindsHeld = true;
      } else {
        if ListenerAction.IsButtonJustReleased(action) {
          if !this.m_modKeybindsHeld {
            this.m_modKeybindsTapFrame = frame;
            this.EnableOnEnterCondition(true);
          }
          this.m_modKeybindsHeld = false;
        }
      }
    }
  }
  if Equals(name, n"ToggleSprint") {
    if ListenerAction.IsButtonJustPressed(action) {
      this.m_modKeybindsPressFrame = frame;
    } else {
      if ListenerAction.IsButtonJustReleased(action) && frame == this.m_modKeybindsPressFrame {
        this.m_modKeybindsTapFrame = frame;
        this.EnableOnEnterCondition(true);
      }
    }
  }
  return result;
}

@wrapMethod(SprintDecisions)
protected const func EnterCondition(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if this.m_modKeybindsTapFrame > 0ul {
    // A tap counts for the check right after it, not for one made much later (on a ladder, in the air).
    let fresh = GameInstance.GetFrameNumber(scriptInterface.GetGame()) - this.m_modKeybindsTapFrame <= 2ul;
    this.m_modKeybindsTapFrame = 0ul;
    if fresh {
      // What the game's own tap does (UpdateInputToggles); the checks below clear it when V can't sprint.
      stateContext.SetConditionBoolParameter(n"SprintToggled", true, true);
    }
  }
  return wrappedMethod(stateContext, scriptInterface);
}
