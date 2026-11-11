module ModKeybindsMenu

// Scan (Hold) and Scan (Toggle) on one button: tap for a quick scan, hold to switch scanning on or
// off. The game keeps the toggle through the button's release only if Scan (Hold) was already down
// when the toggle came. On the Alternative control scheme Scan (Hold) has no press, so both arrive
// in the same frame, in any order, and the release switched scanning off again. Here the toggle
// counts as made during the hold whenever both arrive in one frame.

@addField(PlayerVisionModeController)
private let m_modKeybindsToggleFrame: Uint64; // frame scanning was last toggled on

@wrapMethod(PlayerVisionModeController)
protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
  let result = wrappedMethod(action, consumer);
  if ListenerAction.GetValue(action) > 0.00 && this.m_inputActiveFlags.m_buttonToggle {
    let name = ListenerAction.GetName(action);
    let frame = GameInstance.GetFrameNumber(this.m_owner.GetGame());
    if Equals(name, this.m_inputActionsNames.m_buttonToggle) {
      this.m_modKeybindsToggleFrame = frame;
    } else {
      if Equals(name, this.m_inputActionsNames.m_buttonHold) && frame == this.m_modKeybindsToggleFrame {
        this.m_otherVars.m_toggledDuringHold = true;
      }
    }
  }
  return result;
}
