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

        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        float weightedDamage = a_damage * GetDistanceWeight(a_distance);
        _hateTable[a_targetID][a_attackerID] += weightedDamage;

        /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=Damage) Total={:.1f}"),
            a_targetID, a_attackerID, weightedDamage, _hateTable[a_targetID][a_attackerID]);*/
    }

    void ThreatManager::AddBashHate(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance) {
        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        float weightedBash = kBashHateValue * GetDistanceWeight(a_distance);
        _hateTable[a_targetID][a_attackerID] += weightedBash;

        /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=Bash) Total={:.1f}"),
            a_targetID, a_attackerID, weightedBash, _hateTable[a_targetID][a_attackerID]);*/
    }

    void ThreatManager::ApplyDecay(RE::FormID a_targetID, float a_deltaTime) {
        if (a_deltaTime <= 0.0f) return;

        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        auto itTarget = _hateTable.find(a_targetID);
        if (itTarget == _hateTable.end()) return;

        float decayFactor = std::pow(0.5f, a_deltaTime / kHateHalfLife);
        for (auto& [attackerID, hate] : itTarget->second) {
            hate *= decayFactor;
        }
    }

    void ThreatManager::ClearHate(RE::FormID a_targetID) {
        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        _hateTable.erase(a_targetID);
    }

    RE::Actor* ThreatManager::EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates) {
        if (a_candidates.empty()) {
            ClearFocus(a_observer->GetFormID());
            return nullptr;
        }

        RE::FormID observerID = a_observer->GetFormID();

        // Always find the closest candidate and give them Base Threat as a minimum effective hate.
        // This ensures a newly-detected actor (e.g. player joining mid-fight) with no accumulated hate
        // can still become the target if they are the closest, matching proximity-based targeting intent.
        RE::Actor* closestCandidate = nullptr;
        float minDist = 1000000.0f;
        for (const auto& cand : a_candidates) {
            if (cand.distance < minDist) {
                minDist = cand.distance;
                closestCandidate = cand.actor.get();
            }
        }

        float currentHate = 0.0f;
        RE::Actor* bestTarget = nullptr;
        float bestHate = 0.0f;

        {
            std::shared_lock<std::shared_mutex> lock(_hateMutex);
            auto itObserver = _hateTable.find(observerID);

            if (a_currentTarget && itObserver != _hateTable.end()) {
                auto itTarget = itObserver->second.find(a_currentTarget->GetFormID());
                if (itTarget != itObserver->second.end()) {
                    currentHate = itTarget->second;
                }
            }

            for (const auto& candidate : a_candidates) {
                float hate = 0.0f;
                if (itObserver != _hateTable.end()) {
                    auto itCand = itObserver->second.find(candidate.actor->GetFormID());
                    if (itCand != itObserver->second.end()) {
                        hate = itCand->second;
                    }
                }

                // Give Base Threat to the closest candidate as a minimum.
                // For existing targets with high accumulated hate, this has no effect.
                // For new actors with 0 hate, this makes them competitive when closest.
                if (closestCandidate && candidate.actor.get() == closestCandidate) {
                    float proximityHate = kBaseThreat * GetDistanceWeight(candidate.distance);
                    hate = std::max(hate, proximityHate);
                }

                if (!bestTarget || hate > bestHate) {
                    bestTarget = candidate.actor.get();
                    bestHate = hate;
                }
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
        std::shared_lock<std::shared_mutex> lock(_focusMutex);
        auto it = _focusMap.find(a_observerID);
        return (it != _focusMap.end()) ? it->second : 0;
    }

    void ThreatManager::SetFocus(RE::FormID a_observerID, RE::FormID a_targetID) {
        std::unique_lock<std::shared_mutex> lock(_focusMutex);
        _focusMap[a_observerID] = a_targetID;
    }

    void ThreatManager::ClearFocus(RE::FormID a_observerID) {
        std::unique_lock<std::shared_mutex> lock(_focusMutex);
        _focusMap.erase(a_observerID);
    }

}
