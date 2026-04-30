#include "threat.h"
#include <SKSE/SKSE.h>
#include <chrono>
#include <cmath>

namespace Threat {
    static float GetDistanceWeight(float a_distance) {
        if (a_distance < ThreatManager::kNearRange) return 1.0f;
        if (a_distance < ThreatManager::kMidRange) return 0.6f;
        return 0.3f;
    }

    void ThreatManager::AddDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance) {
        if (a_damage <= 0.0f) return;

        std::lock_guard<std::mutex> lock(_mutex);
        float weightedDamage = a_damage * GetDistanceWeight(a_distance);
        _hateTable[a_targetID][a_attackerID] += weightedDamage;

        /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=Damage) Total={:.1f}"),
            a_targetID, a_attackerID, weightedDamage, _hateTable[a_targetID][a_attackerID]);*/
    }

    void ThreatManager::AddBashHate(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance) {
        std::lock_guard<std::mutex> lock(_mutex);
        float weightedBash = kBashHateValue * GetDistanceWeight(a_distance);
        _hateTable[a_targetID][a_attackerID] += weightedBash;

        /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=Bash) Total={:.1f}"),
            a_targetID, a_attackerID, weightedBash, _hateTable[a_targetID][a_attackerID]);*/
    }

    float ThreatManager::GetHate(RE::FormID a_targetID, RE::FormID a_attackerID) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto itTarget = _hateTable.find(a_targetID);
        if (itTarget != _hateTable.end()) {
            auto itAttacker = itTarget->second.find(a_attackerID);
            if (itAttacker != itTarget->second.end()) {
                return itAttacker->second;
            }
        }
        return 0.0f;
    }

    void ThreatManager::ApplyDecay(RE::FormID a_targetID, float a_deltaTime) {
        if (a_deltaTime <= 0.0f) return;

        std::lock_guard<std::mutex> lock(_mutex);
        auto itTarget = _hateTable.find(a_targetID);
        if (itTarget == _hateTable.end()) return;

        float decayFactor = std::pow(0.5f, a_deltaTime / kHateHalfLife);
        for (auto& [attackerID, hate] : itTarget->second) {
            hate *= decayFactor;
        }
    }

    void ThreatManager::ClearHate(RE::FormID a_targetID) {
        std::lock_guard<std::mutex> lock(_mutex);
        _hateTable.erase(a_targetID);
    }

    RE::Actor* ThreatManager::EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates) {
        if (a_candidates.empty()) {
            ClearFocus(a_observer->GetFormID());
            return nullptr;
        }

        RE::FormID observerID = a_observer->GetFormID();

        // Initial Hate: If table is empty, give bonus to closest
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_hateTable[observerID].empty()) {
                float minDist = 1000000.0f;
                RE::Actor* closest = nullptr;
                for (const auto& cand : a_candidates) {
                    if (cand.distance < minDist) {
                        minDist = cand.distance;
                        closest = cand.actor.get();
                    }
                }
                if (closest) {
                    float initialHate = kBaseThreat * GetDistanceWeight(minDist);
                    _hateTable[observerID][closest->GetFormID()] = initialHate;
                    /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Initial Hate to Attacker={:X} (Total={:.1f})"),
                        observerID, closest->GetFormID(), initialHate);*/
                }
            }
        }

        float currentHate = a_currentTarget ? GetHate(observerID, a_currentTarget->GetFormID()) : 0.0f;

        RE::Actor* bestTarget = nullptr;
        float bestHate = 0.0f;

        for (const auto& candidate : a_candidates) {
            float hate = GetHate(observerID, candidate.actor->GetFormID());

            if (!bestTarget || hate > bestHate) {
                bestTarget = candidate.actor.get();
                bestHate = hate;
            }
        }

        // Switching Logic
        if (bestTarget && bestTarget != a_currentTarget) {
            float hateDiff = bestHate - currentHate;
            if (hateDiff > 0.1f || (currentHate > 0 && hateDiff / currentHate > kSwitchThreshold)) {
                SetFocus(observerID, bestTarget->GetFormID());
                
                /*SKSE::log::info(FMT_STRING("[TargetSwitch] {:X} ({}) switched target to {:X} ({}) (Hate={:.1f}, Dist={:.1f})"),
                    observerID, a_observer->GetDisplayFullName(), 
                    bestTarget->GetFormID(), bestTarget->GetDisplayFullName(),
                    bestHate, a_observer->GetPosition().GetDistance(bestTarget->GetPosition()));*/

                return bestTarget;
            }
        }

        if (a_currentTarget) {
            SetFocus(observerID, a_currentTarget->GetFormID());
            return a_currentTarget;
        } else if (bestTarget) {
            SetFocus(observerID, bestTarget->GetFormID());
            return bestTarget;
        }

        ClearFocus(observerID);
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
        
        for (auto itTarget = _hateTable.begin(); itTarget != _hateTable.end();) {
            auto& attackerMap = itTarget->second;
            for (auto itAttacker = attackerMap.begin(); itAttacker != attackerMap.end();) {
                if (itAttacker->second < 0.1f) {
                    itAttacker = attackerMap.erase(itAttacker);
                } else {
                    ++itAttacker;
                }
            }

            if (attackerMap.empty()) {
                itTarget = _hateTable.erase(itTarget);
            } else {
                ++itTarget;
            }
        }
    }
}
