// Souls of Harkinian: each save file keeps its own look.
//
// The look is the part of the Cosmetics Editor that belongs to the hero (Link, swords, gloves, shields, equipment,
// Navi, sword trails, spin attack, body size). On save it is copied from the current settings into the save file
// (section "soulsLook"); on load it is copied back and the cosmetics are applied. A save file without the section
// (older files, or files made with this option off) keeps whatever look is current. Global cosmetics such as HUD
// colors are not touched. The character creator writes these same settings.
//
// Off unless gEnhancements.SoulsPerFileLook is 1 (the Souls of Harkinian preset turns it on).
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/config/Config.h>
#include <nlohmann/json.hpp>
#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
// From CosmeticsEditor.cpp (its header pulls in ImGui; only this function is needed here)
void ApplyOrResetCustomGfxPatches(bool manualChange);

#define CVAR_SOULS_PER_FILE_LOOK CVAR_ENHANCEMENT("SoulsPerFileLook")

static const char* sLookGroups[] = { "Link",     "Swords", "Gloves", "MirrorShield", "Equipment",
                                     "Navi",     "Trails", "SpinAttack" };

static nlohmann::json SoulsLook_Current() {
    nlohmann::json look = nlohmann::json::object();
    auto all = Ship::Context::GetRawInstance()->GetConfig()->GetNestedJson();
    if (!all.contains("CVars") || !all["CVars"].contains(CVAR_PREFIX_COSMETIC)) {
        return look;
    }
    auto& cosmetics = all["CVars"][CVAR_PREFIX_COSMETIC];
    for (const char* group : sLookGroups) {
        if (cosmetics.contains(group)) {
            look[group] = cosmetics[group];
        }
    }
    return look;
}

static void SoulsLook_Save(const SaveContext& saveContext, int sectionID, bool fullSave) {
    if (!CVarGetInteger(CVAR_SOULS_PER_FILE_LOOK, 0)) {
        return;
    }
    SaveManager::Instance->SaveData("look", SoulsLook_Current().dump());
}

static void SoulsLook_Load() {
    if (!CVarGetInteger(CVAR_SOULS_PER_FILE_LOOK, 0)) {
        return;
    }
    std::string text;
    SaveManager::Instance->LoadData("look", text, std::string());
    if (text.empty()) {
        return;
    }
    nlohmann::json look = nlohmann::json::parse(text, nullptr, false);
    if (look.is_discarded() || !look.is_object()) {
        return;
    }
    auto config = Ship::Context::GetRawInstance()->GetConfig();
    for (const char* group : sLookGroups) {
        std::string key = std::string("CVars.") + CVAR_PREFIX_COSMETIC + "." + group;
        if (look.contains(group)) {
            config->SetBlock(key, look[group]);
        } else {
            CVarClearBlock((std::string(CVAR_PREFIX_COSMETIC) + "." + group).c_str()); // this file uses the defaults
        }
    }
    Ship::Context::GetRawInstance()->GetConsoleVariables()->Load();
    ApplyOrResetCustomGfxPatches(false);
}

static void RegisterSoulsLook() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    SaveManager::Instance->AddSaveFunction("soulsLook", 1, SoulsLook_Save, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("soulsLook", 1, SoulsLook_Load);
}

static RegisterShipInitFunc initSoulsLook(RegisterSoulsLook);
