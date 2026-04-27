#pragma once

#include <unordered_map>
#include <deque>
#include <mutex>

namespace Threat {
    struct DamageRecord {
        float timestamp;
        float amount;
    };

    class ThreatManager {
    public:
        static ThreatManager* GetSingleton() {
            static ThreatManager singleton;
            return &singleton;
        }

        void AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage);
        float GetDPS(RE::FormID a_targetID, RE::FormID a_attackerID);
        void Cleanup();

    private:
        ThreatManager() = default;

        // TargetID -> AttackerID -> Deque of DamageRecords
        std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, std::deque<DamageRecord>>> _data;
        std::mutex _mutex;

        const float _windowSeconds = 10.0f;
    };
}
