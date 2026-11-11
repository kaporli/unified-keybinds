module ModKeybindsMenu

// The Classic control scheme dodges on a double tap, which the game counts itself. It misses the taps
// on a button that also has a hold: such a press is held back and arrives together with its release.
// So a dodge moved to another button often never fires. On Classic the plugin adds an action of ours,
// ModKeybindsDodge, on the dodge's button; it gets the plain presses. Two of them within the game's
// double tap time count as the dodge input here, wherever the game asks for it (WantsToDodge).

@addField(PlayerPuppet)
private let m_modKeybindsDodgePress: Float; // time of the last single press

@addField(PlayerPuppet)
private let m_modKeybindsDodgePressFrame: Uint64; // frame of the last press counted

@addField(PlayerPuppet)
public let m_modKeybindsDodgeFrame: Uint64; // frame of the last double tap

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
  // One press per frame: the same press can arrive more than once.
  if ListenerAction.IsButtonJustPressed(action) && frame != this.m_modKeybindsDodgePressFrame {
    this.m_modKeybindsDodgePressFrame = frame;
    let defs = GetAllBlackboardDefs().InputSchemes;
    let schemes = GameInstance.GetBlackboardSystem(game).Get(defs);
    let time = EngineTime.ToFloat(GameInstance.GetEngineTime(game));
    // The game's double tap: up to 0.2 s down and 0.2 s up.
    if time - this.m_modKeybindsDodgePress <= 0.4 && Equals(ConvertIntValueToInputScheme(schemes.GetUint(defs.Scheme)), InputScheme.LEGACY)
      && ModKeybindsPad.IsPadDevice(schemes.GetUint(defs.Device)) {
      this.m_modKeybindsDodgePress = 0.0;
      this.m_modKeybindsDodgeFrame = frame;
    } else {
      this.m_modKeybindsDodgePress = time;
    }
  }
  return true;
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
  // Like the game's "just tapped": true in the frame of the double tap (and the next, for a check
  // that ran before the press arrived).
  if !IsDefined(player) || player.m_modKeybindsDodgeFrame == 0ul
    || GameInstance.GetFrameNumber(scriptInterface.GetGame()) - player.m_modKeybindsDodgeFrame > 1ul {
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
    return true;
  }
  if this.GetStaticBoolParameterDefault("dodgeWithNoMovementInput", false) {
    stateContext.SetConditionFloatParameter(n"DodgeDirection", -180.00, true);
    return true;
  }
  return false;
}
