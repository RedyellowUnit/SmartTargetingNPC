#pragma once
#include <SimpleIni.h>
#include <cctype>
#include <string>
#include <unordered_set>
#include "util.h"
#include "RE/A/Actor.h"

class Settings {
public:
    static Settings* GetSingleton() {
        static Settings singleton;
        return &singleton;
    }

    void Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(path);

        // [Hate]
        baseThreat = (float)ini.GetDoubleValue("Hate", "fBaseThreat", 5.0);
        switchThreshold = (float)ini.GetDoubleValue("Hate", "fSwitchThreshold", 0.15);
        hateHalfLife = (float)ini.GetDoubleValue("Hate", "fHateHalfLife", 30.0);
        tauntBonus = (float)ini.GetDoubleValue("Hate", "fTauntBonus", 1000.0);
        executionerThreshold = (float)ini.GetDoubleValue("Hate", "fExecutionerThreshold", 0.3);
        executionerBonus = (float)ini.GetDoubleValue("Hate", "fExecutionerBonus", 500.0);
        bashHateValue = (float)ini.GetDoubleValue("Hate", "fBashHateValue", 50.0);
        summonHateValue = (float)ini.GetDoubleValue("Hate", "fSummonHateValue", 300.0);
        killTransferBonus = (float)ini.GetDoubleValue("Hate", "fKillTransferBonus", 500.0);
        dragonBonus = (float)ini.GetDoubleValue("Hate", "fDragonBonus", 1000000.0);

        // [Range]
        nearRange = (float)ini.GetDoubleValue("Range", "fNearRange", 800.0);
        midRange = (float)ini.GetDoubleValue("Range", "fMidRange", 2000.0);
        forceDetectRange = (float)ini.GetDoubleValue("Range", "fForceDetectRange", 3000.0);

        // [Exclusions] — actor base / race FormIDs excluded from hate management
        // Miraak (Dragonborn): hostile to dragons in the final fight; fDragonBonus breaks that encounter.
        // Accepts FormID~Plugin.esm or EditorID (comma-separated).
        constexpr const char* kDefaultExcludedObservers =
            "DLC2MiraakMQ01,DLC2MiraakMQ02,DLC2Miraak,DLC2MiraakMQ04,"
            "DLC2MiraakMQ06,DLC2MiraakSoulSteal,DLC2MiraakRace";

        const char* rawExclusions = ini.GetValue("Exclusions", "sExcludedObservers", nullptr);
        if (!rawExclusions) {
            ini.SetValue("Exclusions", "sExcludedObservers", kDefaultExcludedObservers);
            rawExclusions = kDefaultExcludedObservers;
        }

        excludedForms.clear();
        for (const auto& token : Util::String::Split(rawExclusions, ","sv)) {
            auto trimmed = Trim(token);
            if (trimmed.empty()) continue;

            RE::FormID formID = 0;
            if (trimmed.find('~') == std::string::npos) {
                if (auto* form = RE::TESForm::LookupByEditorID(trimmed)) {
                    formID = form->GetFormID();
                }
            } else {
                formID = FormUtil::Parse::GetFormIDFromConfigString(trimmed);
            }

            if (formID == static_cast<RE::FormID>(-1) || formID == 0) {
                SKSE::log::warn("Failed to resolve excluded form: {}", trimmed);
                continue;
            }
            excludedForms.insert(formID);
            SKSE::log::info("Excluded form {:X} ({})", formID, trimmed);
        }

        // Save back defaults if file missing / keys were added
        ini.SaveFile(path);
    }

    bool IsObserverExcluded(RE::Actor* a_actor) const {
        if (!a_actor || excludedForms.empty()) {
            return false;
        }

        if (auto* base = a_actor->GetActorBase()) {
            if (excludedForms.contains(base->GetFormID())) {
                return true;
            }
        }

        if (auto* race = a_actor->GetRace()) {
            if (excludedForms.contains(race->GetFormID())) {
                return true;
            }
        }

        return excludedForms.contains(a_actor->GetFormID());
    }

    // Hate
    float baseThreat = 5.0f;
    float switchThreshold = 0.15f;
    float hateHalfLife = 30.0f;
    float tauntBonus = 1000.0f;
    float executionerThreshold = 0.3f;
    float executionerBonus = 500.0f;
    float bashHateValue = 50.0f;
    float summonHateValue = 300.0f;
    float killTransferBonus = 500.0f;
    float dragonBonus = 1000000.0f;

    // Range
    float nearRange = 800.0f;
    float midRange = 2000.0f;
    float forceDetectRange = 3000.0f;

private:
    Settings() = default;
    const char* path = "Data\\SKSE\\Plugins\\SmartTargetingNPC.ini";

    std::unordered_set<RE::FormID> excludedForms;

    static std::string Trim(std::string_view a_str) {
        while (!a_str.empty() && std::isspace(static_cast<unsigned char>(a_str.front()))) {
            a_str.remove_prefix(1);
        }
        while (!a_str.empty() && std::isspace(static_cast<unsigned char>(a_str.back()))) {
            a_str.remove_suffix(1);
        }
        return std::string(a_str);
    }
};
