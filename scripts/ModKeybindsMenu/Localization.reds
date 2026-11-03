module ModKeybindsMenu

import Codeware.Localization.*

// The Key Bindings menu shows display names through the game's translation system.
// The labels are built from file and action names, so every language gets the same text.

public class LabelPackage extends ModLocalizationPackage {
  protected func DefineTexts() {
    let keys = ModKeybindsMenu_LabelKeys();
    let texts = ModKeybindsMenu_LabelTexts();
    let i = 0;
    while i < ArraySize(keys) && i < ArraySize(texts) {
      this.Text(keys[i], texts[i]);
      i += 1;
    }
  }
}

public class LabelProvider extends ModLocalizationProvider {
  public func GetPackage(language: CName) -> ref<ModLocalizationPackage> {
    return new LabelPackage();
  }

  public func GetFallback() -> CName {
    return n"en-us";
  }
}
