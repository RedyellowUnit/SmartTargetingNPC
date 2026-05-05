#pragma once
#include <SimpleIni.h>

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

        // Save back defaults if file missing
        ini.SaveFile(path);
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
};
