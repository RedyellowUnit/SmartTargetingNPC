#pragma once

#include <unordered_map>
#include <deque>
#include <mutex>

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

        void AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage);
        float GetDPS(RE::FormID a_targetID, RE::FormID a_attackerID);
        float GetThreatScore(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance, bool a_isClosest);
        
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

    private:
        ThreatManager() = default;

        // TargetID -> AttackerID -> Deque of DamageRecords
        std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, std::deque<DamageRecord>>> _data;
        
        // ObserverID -> Focused TargetID
        std::unordered_map<RE::FormID, RE::FormID> _focusMap;
        
        std::mutex _mutex;

        const float _windowSeconds = 10.0f;
    };
}
