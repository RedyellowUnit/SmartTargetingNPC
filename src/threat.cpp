#include "threat.h"

namespace Threat {
    void ThreatManager::AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage) {
        if (a_damage <= 0.0f) return;

        std::lock_guard<std::mutex> lock(_mutex);
        
        auto& attackerMap = _data[a_targetID];
        auto& history = attackerMap[a_attackerID];
        
        float currentTime = static_cast<float>(RE::Calendar::GetSingleton()->GetHoursPassed());
        // Convert hours to seconds for easier window management (roughly)
        // Actually, let's use a more high-resolution timer if possible, but Calendar is stable for engine time.
        // Better: use GetHoursPassed() * 3600.0f
        float currentTimeSec = currentTime * 3600.0f;

        history.push_back({ currentTimeSec, a_damage });
    }

    float ThreatManager::GetDPS(RE::FormID a_targetID, RE::FormID a_attackerID) {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_data.contains(a_targetID) || !_data[a_targetID].contains(a_attackerID)) {
            return 0.0f;
        }

        auto& history = _data[a_targetID][a_attackerID];
        float currentTimeSec = static_cast<float>(RE::Calendar::GetSingleton()->GetHoursPassed()) * 3600.0f;
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
        
        float currentTimeSec = static_cast<float>(RE::Calendar::GetSingleton()->GetHoursPassed()) * 3600.0f;
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
