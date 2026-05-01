#pragma once

#include <unordered_map>
#include <deque>
#include <mutex>
#include "RE/A/Actor.h"
#include "RE/N/NiSmartPointer.h"

namespace Threat {
    struct DamageRecord {
        float timestamp;
        float amount;
    };

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

        void AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance);
        void AddBashHate(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance);
        float GetHate(RE::FormID a_targetID, RE::FormID a_attackerID);
        
        void ApplyDecay(RE::FormID a_targetID, float a_deltaTime);
        void ClearHate(RE::FormID a_targetID);

        // Target Selection and Focus
        RE::Actor* EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates);
        RE::FormID GetFocus(RE::FormID a_observerID);
        void SetFocus(RE::FormID a_observerID, RE::FormID a_targetID);
        void ClearFocus(RE::FormID a_observerID);

        void Cleanup();

        static constexpr float kBaseThreat = 5.0f;
        static constexpr float kNearRange = 800.0f;
        static constexpr float kMidRange = 2000.0f;
        static constexpr float kSwitchThreshold = 0.15f;
        static constexpr float kBashHateValue = 50.0f;
        static constexpr float kHateHalfLife = 30.0f;

    private:
        ThreatManager() = default;

        // TargetID (Victim) -> AttackerID -> Cumulative Hate
        std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, float>> _hateTable;
        
        // ObserverID -> Focused TargetID
        std::unordered_map<RE::FormID, RE::FormID> _focusMap;
        
        std::mutex _mutex;
    };
}
