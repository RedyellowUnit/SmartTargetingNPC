#include "threat.h"
#include <chrono>

namespace Threat {
    void ThreatManager::AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage) {
        if (a_damage <= 0.0f) return;

        std::lock_guard<std::mutex> lock(_mutex);
        
        auto& attackerMap = _data[a_targetID];
        auto& history = attackerMap[a_attackerID];
        
        auto now = std::chrono::steady_clock::now();
        float currentTimeSec = std::chrono::duration<float>(now.time_since_epoch()).count();

        history.push_back({ currentTimeSec, a_damage });
    }

    float ThreatManager::GetDPS(RE::FormID a_targetID, RE::FormID a_attackerID) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_data.contains(a_targetID) || !_data[a_targetID].contains(a_attackerID)) {
            return 0.0f;
        }

        auto& history = _data[a_targetID][a_attackerID];
        auto now = std::chrono::steady_clock::now();
        float currentTimeSec = std::chrono::duration<float>(now.time_since_epoch()).count();
        float windowStart = currentTimeSec - _windowSeconds;

        // Remove old records
        while (!history.empty() && history.front().timestamp < windowStart) {
            history.pop_front();
        }

        if (history.empty()) return 0.0f;

        float totalDamage = 0.0f;
        for (const auto& record : history) {
            totalDamage += record.amount;
        }

        return totalDamage / _windowSeconds;
    }

    void ThreatManager::Cleanup() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto now = std::chrono::steady_clock::now();
        float currentTimeSec = std::chrono::duration<float>(now.time_since_epoch()).count();
        float windowStart = currentTimeSec - _windowSeconds;

        for (auto itTarget = _data.begin(); itTarget != _data.end();) {
            auto& attackerMap = itTarget->second;
            for (auto itAttacker = attackerMap.begin(); itAttacker != attackerMap.end();) {
                auto& history = itAttacker->second;
                
                while (!history.empty() && history.front().timestamp < windowStart) {
                    history.pop_front();
                }

                if (history.empty()) {
                    itAttacker = attackerMap.erase(itAttacker);
                } else {
                    ++itAttacker;
                }
            }

            if (attackerMap.empty()) {
                itTarget = _data.erase(itTarget);
            } else {
                ++itTarget;
            }
        }
    }
}
