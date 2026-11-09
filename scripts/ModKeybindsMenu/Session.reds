module ModKeybindsMenu

// While a session starts the game replaces its key bindings, and keys applied then read bindings
// it had already freed: a new game crashed. The plugin holds changed keys from the start of a
// session until it is ready, and from its end until it is over (back to the main menu or the next
// save loading).
public class ModKeybindsSession extends ScriptableService {
  private cb func OnLoad() {
    let callbacks = GameInstance.GetCallbackSystem();
    callbacks.RegisterCallback(n"Session/BeforeStart", this, n"OnSessionChange");
    callbacks.RegisterCallback(n"Session/Ready", this, n"OnSessionChange");
    callbacks.RegisterCallback(n"Session/BeforeEnd", this, n"OnSessionChange");
    callbacks.RegisterCallback(n"Session/End", this, n"OnSessionChange");
  }

  private cb func OnSessionChange(event: ref<GameSessionEvent>) {
    let name = event.GetEventName();
    ModKeybindsMenu_SetStable(Equals(name, n"Session/Ready") || Equals(name, n"Session/End"));
  }
}
