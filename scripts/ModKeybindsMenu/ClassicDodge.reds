module ModKeybindsMenu

// The Classic control scheme dodges on a double tap, which the game counts itself. It misses the taps
// on a button that also has a hold: such a press is held back and arrives together with its release.
// So a dodge moved to another button often never fires. On Classic the plugin adds an action of ours,
// ModKeybindsDodge, on the dodge's button; it gets the plain presses. Two taps within the game's
// double tap times count as the dodge input here, wherever the game asks for it (WantsToDodge).
// Only with the dodge moved off its default button: there the game's own count works.

@addField(PlayerPuppet)
private let m_modKeybindsDodgePress: Float; // time of the press that is down, 0 when none

@addField(PlayerPuppet)
private let m_modKeybindsDodgeTap: Float; // time a short tap ended, 0 when none

@addField(PlayerPuppet)
private let m_modKeybindsDodgePressFrame: Uint64; // frame of the last press counted

@addField(PlayerPuppet)
public let m_modKeybindsDodgeFrame: Uint64; // frame of the last double tap

@addField(PlayerPuppet)
public let m_modKeybindsDodgeUsed: Uint64; // frame a dodge check took the double tap

@wrapMethod(PlayerPuppet)
protected cb func OnGameAttached() -> Bool {
  wrappedMethod();
  if this.IsControlledByLocalPeer() || IsHost() {
    this.RegisterInputListener(this, n"ModKeybindsDodge");
  }
}

@wrapMethod(PlayerPuppet)
protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
  if NotEquals(ListenerAction.GetName(action), n"ModKeybindsDodge") {
    return wrappedMethod(action, consumer);
  }
  let game = this.GetGame();
  let frame = GameInstance.GetFrameNumber(game);
  let time = EngineTime.ToFloat(GameInstance.GetEngineTime(game));
  // The game's double tap: each press down for up to 0.2 s, up to 0.2 s between them. A held back
  // press arrives late (about 0.12 s), so the time between gets that much more.
  if ListenerAction.IsButtonJustReleased(action) {
    if this.m_modKeybindsDodgePress > 0.0 && time - this.m_modKeybindsDodgePress <= 0.2 {
      this.m_modKeybindsDodgeTap = time;
    }
    this.m_modKeybindsDodgePress = 0.0;
    return true;
  }
  // One press per frame: the same press can arrive more than once.
  if ListenerAction.IsButtonJustPressed(action) && frame != this.m_modKeybindsDodgePressFrame {
    this.m_modKeybindsDodgePressFrame = frame;
    let second = this.m_modKeybindsDodgeTap > 0.0 && time - this.m_modKeybindsDodgeTap <= 0.32;
    this.m_modKeybindsDodgeTap = 0.0;
    this.m_modKeybindsDodgePress = time;
    if second && this.ModKeybindsCountsDodge() {
      this.m_modKeybindsDodgeFrame = frame;
    }
  }
  return true;
}

// Classic scheme, a controller in use, and the dodge on another button than its default.
@addMethod(PlayerPuppet)
private func ModKeybindsCountsDodge() -> Bool {
  let game = this.GetGame();
  let defs = GetAllBlackboardDefs().InputSchemes;
  let schemes = GameInstance.GetBlackboardSystem(game).Get(defs);
  if NotEquals(ConvertIntValueToInputScheme(schemes.GetUint(defs.Scheme)), InputScheme.LEGACY) || !ModKeybindsPad.IsPadDevice(schemes.GetUint(defs.Device)) {
    return false;
  }
  let entry = GameInstance.GetSettingsSystem(game).GetVar(n"/key_bindings/SettingsModPadSettingsLocomotion", n"MKPad_dodgeDash") as ConfigVarName;
  return IsDefined(entry) && NotEquals(entry.GetValue(), n"IK_Pad_B_CIRCLE");
}

// The game's dodge states check for input only after a press of one of their actions.
@wrapMethod(DodgeDecisions)
protected func OnAttach(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  scriptInterface.executionOwner.RegisterInputListener(this, n"ModKeybindsDodge");
}

@wrapMethod(DodgeAirDecisions)
protected func OnAttach(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  scriptInterface.executionOwner.RegisterInputListener(this, n"ModKeybindsDodge");
}

@wrapMethod(LocomotionTransition)
protected final const func WantsToDodge(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if wrappedMethod(stateContext, scriptInterface) {
    return true;
  }
  let player = scriptInterface.executionOwner as PlayerPuppet;
  // Like the game's "just tapped": true in the frame of the double tap, or in the next one when no
  // check took it in the first (one that ran before the press arrived).
  if !IsDefined(player) || player.m_modKeybindsDodgeFrame == 0ul {
    return false;
  }
  let frame = GameInstance.GetFrameNumber(scriptInterface.GetGame());
  let tapped = player.m_modKeybindsDodgeFrame;
  if frame != tapped && (frame != tapped + 1ul || player.m_modKeybindsDodgeUsed == tapped) {
    return false;
  }
  // The checks the game makes before it looks at its own dodge input.
  if !scriptInterface.HasStatFlag(gamedataStatType.HasDodge)
    || StatusEffectSystem.ObjectHasStatusEffect(player, t"BaseStatusEffect.HealFood3")
    || StatusEffectSystem.ObjectHasStatusEffect(player, t"BaseStatusEffect.DodgeCooldown")
    || StatusEffectSystem.ObjectHasStatusEffect(player, t"BaseStatusEffect.DodgeAirCooldown") {
    return false;
  }
  if !this.IsTouchingGround(scriptInterface) {
    let disableAirDash = stateContext.GetPermanentBoolParameter(n"disableAirDash");
    let perk = PlayerDevelopmentSystem.GetInstance(player).IsNewPerkBought(player, gamedataNewPerkType.Reflexes_Central_Milestone_3) == 3;
    let stamina = GameInstance.GetStatPoolsSystem(player.GetGame()).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Stamina, true) > 0.00;
    if !perk || !stamina || (disableAirDash.valid && disableAirDash.value) {
      return false;
    }
  }
  if this.IsCurrentFallSpeedTooFastToEnter(stateContext, scriptInterface) {
    return false;
  }
  if scriptInterface.IsMoveInputConsiderable() {
    stateContext.SetConditionFloatParameter(n"DodgeDirection", scriptInterface.GetInputHeading(), true);
    player.m_modKeybindsDodgeUsed = frame;
    return true;
  }
  if this.GetStaticBoolParameterDefault("dodgeWithNoMovementInput", false) {
    stateContext.SetConditionFloatParameter(n"DodgeDirection", -180.00, true);
    player.m_modKeybindsDodgeUsed = frame;
    return true;
  }
  return false;
}
