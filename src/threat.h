#pragma once

#include <unordered_map>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include "RE/A/Actor.h"
#include "RE/N/NiSmartPointer.h"

namespace Threat {

    struct TargetCandidate {
        RE::NiPointer<RE::Actor> actor;
        float distance;
    };

    class ThreatManager {
    public:
        static ThreatManager* GetSingleton() {
            static ThreatManager singleton;
            return &singleton;
        }

        static float GetDistanceWeight(float a_distance);

        void AddHate(RE::FormID a_targetID, RE::FormID a_attackerID, float a_amount);
        float GetHate(RE::FormID a_targetID, RE::FormID a_attackerID);
        
        void ApplyDecay(RE::FormID a_targetID, float a_deltaTime);
        void ClearHate(RE::FormID a_targetID);

        void ProcessDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance);
        void ProcessBash(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance);
        void ProcessSummonAggro(RE::FormID a_targetID, RE::FormID a_summonerID, float a_distance);
        void ProcessDeathAggro(RE::Actor* a_victim, RE::Actor* a_killer);

        // Target Selection and Focus
        RE::Actor* EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates);
        RE::FormID GetFocus(RE::FormID a_observerID);
        void SetFocus(RE::FormID a_observerID, RE::FormID a_targetID);
        void ClearFocus(RE::FormID a_observerID);


    private:
        ThreatManager() = default;

        // TargetID (Victim) -> AttackerID -> Cumulative Hate
        std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, float>> _hateTable;
        
        // ObserverID -> Focused TargetID
        std::unordered_map<RE::FormID, RE::FormID> _focusMap;
        
        mutable std::shared_mutex _hateMutex;
        mutable std::shared_mutex _focusMutex;
    };
}
