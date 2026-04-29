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

    static float GetDistanceWeight(float a_distance) {
        if (a_distance < ThreatManager::kNearRange) return 1.0f;
        if (a_distance < ThreatManager::kMidRange) return 0.6f;
        return 0.3f;
    }

    float ThreatManager::GetThreatScore(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance, bool a_isClosest) {
        float rawDPS = GetDPS(a_targetID, a_attackerID);
        float effectiveDPS = a_isClosest ? std::max(rawDPS, kBaseThreat) : rawDPS;
        return effectiveDPS * GetDistanceWeight(a_distance);
    }

    RE::Actor* ThreatManager::EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates) {
        if (a_candidates.empty()) {
            ClearFocus(a_observer->GetFormID());
            return nullptr;
        }

        // Find the closest valid target for Base Threat
        RE::Actor* closestTarget = nullptr;
        float minDistanceOverall = 1000000.0f;
        for (const auto& candidate : a_candidates) {
            if (candidate.distance < minDistanceOverall) {
                minDistanceOverall = candidate.distance;
                closestTarget = candidate.actor.get();
            }
        }

        float currentScore = 0.0f;
        float currentDistance = 1000000.0f;
        if (a_currentTarget) {
            currentDistance = a_observer->GetPosition().GetDistance(a_currentTarget->GetPosition());
            currentScore = GetThreatScore(a_observer->GetFormID(), a_currentTarget->GetFormID(), currentDistance, (a_currentTarget == closestTarget));
        }

        RE::Actor* bestTarget = nullptr;
        float bestScore = 0.0f;

        for (const auto& candidate : a_candidates) {
            float score = GetThreatScore(a_observer->GetFormID(), candidate.actor->GetFormID(), candidate.distance, (candidate.actor.get() == closestTarget));

            if (!bestTarget || score > bestScore) {
                bestTarget = candidate.actor.get();
                bestScore = score;
            }
        }

        // Switching Logic
        if (bestTarget && bestTarget != a_currentTarget) {
            float scoreDiff = bestScore - currentScore;
            if (scoreDiff > 0.1f || (currentScore > 0 && scoreDiff / currentScore > kSwitchThreshold)) {
                SetFocus(a_observer->GetFormID(), bestTarget->GetFormID());
                return bestTarget;
            }
        }

        if (a_currentTarget) {
            SetFocus(a_observer->GetFormID(), a_currentTarget->GetFormID());
            return a_currentTarget;
        } else if (bestTarget) {
            SetFocus(a_observer->GetFormID(), bestTarget->GetFormID());
            return bestTarget;
        }

        ClearFocus(a_observer->GetFormID());
        return nullptr;
    }

    RE::FormID ThreatManager::GetFocus(RE::FormID a_observerID) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _focusMap.find(a_observerID);
        return (it != _focusMap.end()) ? it->second : 0;
    }

    void ThreatManager::SetFocus(RE::FormID a_observerID, RE::FormID a_targetID) {
        std::lock_guard<std::mutex> lock(_mutex);
        _focusMap[a_observerID] = a_targetID;
    }

    void ThreatManager::ClearFocus(RE::FormID a_observerID) {
        std::lock_guard<std::mutex> lock(_mutex);
        _focusMap.erase(a_observerID);
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
