#include "AggroManager.h"
#include "threat.h"
#include <SKSE/SKSE.h>

namespace Aggro {

    void AggroManager::ProcessDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance) {
        if (a_damage <= 0.0f) return;

        float distanceWeight = Threat::ThreatManager::GetDistanceWeight(a_distance);
        float weightedDamage = a_damage * distanceWeight;
        
        Threat::ThreatManager::GetSingleton()->AddHate(a_targetID, a_attackerID, weightedDamage);

        SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=Damage) Total={:.1f}"),
            a_targetID, a_attackerID, weightedDamage, Threat::ThreatManager::GetSingleton()->GetHate(a_targetID, a_attackerID));
    }

    void AggroManager::ProcessBash(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance) {
        float distanceWeight = Threat::ThreatManager::GetDistanceWeight(a_distance);
        float weightedBash = kBashHateValue * distanceWeight;
        
        Threat::ThreatManager::GetSingleton()->AddHate(a_targetID, a_attackerID, weightedBash);
    }

    void AggroManager::ProcessSummonAggro(RE::FormID a_targetID, RE::FormID a_summonerID, float a_distance) {
        float distanceWeight = Threat::ThreatManager::GetDistanceWeight(a_distance);
        float weightedSummon = kSummonHateValue * distanceWeight;

        Threat::ThreatManager::GetSingleton()->AddHate(a_targetID, a_summonerID, weightedSummon);
        SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=SummonAggro)"), a_targetID, a_summonerID, weightedSummon);
    }
}
