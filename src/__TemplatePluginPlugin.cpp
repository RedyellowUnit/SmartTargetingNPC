#include "__TemplatePluginPlugin.h"

namespace Sample {
void ExecuteSampleCode() {
  // 1. Logging Information
  // 'SKSE::log::info' will log to the <PluginName>.log file located in
  // Documents/My Games/Skyrim Special Edition/SKSE
  SKSE::log::info("Executing Sample Code...");

  GetPlayerLocationInfo();
}

bool GetPlayerLocationInfo(){
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || !player->IsHandleValid() || !player->Is3DLoaded()) {
    return false;
  }

  auto pcell = player->GetParentCell();

  if (pcell == nullptr) {
      return false;
  }

  SKSE::log::info(FMT_STRING("Player Cell is {}"), pcell->GetFormEditorID());
  return true;
}
} // namespace Sample



