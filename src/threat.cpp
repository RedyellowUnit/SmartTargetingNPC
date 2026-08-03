#include "threat.h"
#include <SKSE/SKSE.h>
#include <cmath>
#include "RE/T/TESForm.h"
#include "RE/E/EffectSetting.h"
#include "RE/M/MagicTarget.h"
#include "settings.h"

namespace Threat {
    float ThreatManager::GetDistanceWeight(float a_distance) {
        auto settings = Settings::GetSingleton();
        if (a_distance < settings->nearRange) return 1.0f;
        if (a_distance < settings->midRange) return 0.6f;
        return 0.3f;
    }

    void ThreatManager::AddHate(RE::FormID a_targetID, RE::FormID a_attackerID, float a_amount) {
        if (a_amount <= 0.0f) return;

        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        _hateTable[a_targetID][a_attackerID] += a_amount;
    }

    float ThreatManager::GetHate(RE::FormID a_targetID, RE::FormID a_attackerID) {
        std::shared_lock<std::shared_mutex> lock(_hateMutex);
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

        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        auto itTarget = _hateTable.find(a_targetID);
        if (itTarget == _hateTable.end()) return;

        float decayFactor = std::pow(0.5f, a_deltaTime / Settings::GetSingleton()->hateHalfLife);
        for (auto& [attackerID, hate] : itTarget->second) {
            hate *= decayFactor;
        }
    }

    void ThreatManager::ClearHate(RE::FormID a_targetID) {
        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        _hateTable.erase(a_targetID);
    }

    void ThreatManager::ClearHateToward(RE::FormID a_deadID) {
        std::unique_lock<std::shared_mutex> lock(_hateMutex);
        for (auto& entry : _hateTable) {
            entry.second.erase(a_deadID);
        }
    }

    void ThreatManager::Reset() {
        {
            std::unique_lock<std::shared_mutex> lock(_hateMutex);
            _hateTable.clear();
        }
        {
            std::unique_lock<std::shared_mutex> lock(_focusMutex);
            _focusMap.clear();
        }
    }

    void ThreatManager::ProcessTargetDeath(RE::FormID a_deadID) {
        ClearFocusOnTarget(a_deadID);
        ClearHateToward(a_deadID);
        ClearHate(a_deadID);
        ClearFocus(a_deadID);
    }

    RE::Actor* ThreatManager::EvaluateBestTarget(RE::Actor* a_observer, RE::Actor* a_currentTarget, const std::vector<TargetCandidate>& a_candidates) {
        if (a_candidates.empty()) {
            ClearFocus(a_observer->GetFormID());
            return nullptr;
        }

        static RE::EffectSetting* courageEff = nullptr;
        static RE::EffectSetting* allyEff = nullptr;
        static RE::BGSKeyword* dragonKeyword = nullptr;
        static bool initialized = false;

        if (!initialized) {
            courageEff = RE::TESForm::LookupByID<RE::EffectSetting>(0x0001EA79);
            allyEff = RE::TESForm::LookupByID<RE::EffectSetting>(0x0001EA76);
            dragonKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00035D59);
            initialized = true;
        }

        RE::FormID observerID = a_observer->GetFormID();

        // Dead/deleted current targets must not keep focus or block retargeting via leftover hate.
        if (a_currentTarget && (a_currentTarget->IsDead() || a_currentTarget->IsDeleted())) {
            a_currentTarget = nullptr;
        }

        // Identify the closest candidate to ensure proximity-based priority for new or low-hate actors.
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
            auto itHateTableEnd = _hateTable.end();

            if (a_currentTarget && itObserver != _hateTable.end()) {
                auto itTarget = itObserver->second.find(a_currentTarget->GetFormID());
                if (itTarget != itObserver->second.end()) {
                    currentHate = itTarget->second;
                }
            }

            for (const auto& candidate : a_candidates) {
                if (!candidate.actor || candidate.actor->IsDeleted() || candidate.actor->IsDead()) continue;

                float hate = 0.0f;
                if (itObserver != itHateTableEnd) {
                    auto itCand = itObserver->second.find(candidate.actor->GetFormID());
                    if (itCand != itObserver->second.end()) {
                        hate = itCand->second;
                    }
                }

                // Apply base proximity threat for the nearest candidate to prevent targets with zero accumulated hate from being ignored.
                if (closestCandidate && candidate.actor.get() == closestCandidate) {
                    float proximityHate = Settings::GetSingleton()->baseThreat * GetDistanceWeight(candidate.distance);
                    hate = std::max(hate, proximityHate);
                }

                // Courage / Ally Priority Logic for Target
                bool isTaunting = false;
                auto magicTargetCand = candidate.actor->As<RE::MagicTarget>();
                if (magicTargetCand) {
                    if (courageEff && magicTargetCand->HasMagicEffect(courageEff)) isTaunting = true;
                    else if (allyEff && magicTargetCand->HasMagicEffect(allyEff)) isTaunting = true;
                }

                if (isTaunting) {
                    hate += Settings::GetSingleton()->tauntBonus; // Bonus for taunt effects
                    /*SKSE::log::info(FMT_STRING("[Hate] Courage={:X} Observer={:X}"),
                        candidate.actor->GetFormID(), a_observer->GetFormID());*/
                }

                // Dragon Priority Logic
                if (dragonKeyword && candidate.actor->HasKeyword(dragonKeyword)) {
                    hate += Settings::GetSingleton()->dragonBonus;
                    /*SKSE::log::info(FMT_STRING("[Hate] Dragon={:X} Observer={:X}"),
                        candidate.actor->GetFormID(), a_observer->GetFormID());*/
                }

                // Executioner Bonus Logic for current target
                if (a_currentTarget && candidate.actor.get() == a_currentTarget) {
                    auto avOwner = candidate.actor->AsActorValueOwner();
                    if (avOwner) {
                        float currentHealth = avOwner->GetActorValue(RE::ActorValue::kHealth);
                        float maxHealth = avOwner->GetBaseActorValue(RE::ActorValue::kHealth);
                        if (maxHealth > 0.0f) {
                            float healthRatio = currentHealth / maxHealth;
                            if (healthRatio < Settings::GetSingleton()->executionerThreshold) {
                                hate += Settings::GetSingleton()->executionerBonus;
                                /*SKSE::log::info(FMT_STRING("[Hate] HealthLow={:X} Observer={:X}"),
                                    candidate.actor->GetFormID(), a_observer->GetFormID());*/
                            }
                        }
                    }
                }

                if (!bestTarget || hate > bestHate) {
                    bestTarget = candidate.actor.get();
                    bestHate = hate;
                }
            }
        }

        // Logic to determine if a target switch is necessary based on hate accumulation thresholds.
        // No valid current target (including after rejecting a corpse): always accept the best living candidate.
        // Also switch when the current target's effective hate was discounted to zero (undetected summoner).
        if (!bestTarget) {
            ClearFocus(observerID);
            return nullptr;
        }
        else if (bestTarget != a_currentTarget) {
            float hateDiff = bestHate - currentHate;
            if (!a_currentTarget ||
                (currentHate <= 0.0f && bestHate > 0.0f) ||
                hateDiff > 0.1f ||
                (currentHate > 0 && hateDiff / currentHate > Settings::GetSingleton()->switchThreshold)) {
                SetFocus(observerID, bestTarget->GetFormID());
                
                /*SKSE::log::info(FMT_STRING("[TargetSwitch] {} --> {}) (Hate={:.1f}, Dist={:.1f})"),
                    a_observer->GetDisplayFullName(), 
                    bestTarget->GetDisplayFullName(),
                    bestHate, 
                    a_observer->GetPosition().GetDistance(bestTarget->GetPosition()));*/

                return bestTarget;
            }
        }

        SetFocus(observerID, a_currentTarget->GetFormID());
        /*SKSE::log::info(FMT_STRING("[CurrentTarget] {} --> {} (Hate={:.1f})"), 
            a_observer->GetDisplayFullName(),
            a_currentTarget->GetDisplayFullName(),
            currentHate);*/
        return a_currentTarget;

        
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

    void ThreatManager::ClearFocusOnTarget(RE::FormID a_deadID) {
        std::unique_lock<std::shared_mutex> lock(_focusMutex);
        for (auto it = _focusMap.begin(); it != _focusMap.end();) {
            if (it->second == a_deadID) {
                it = _focusMap.erase(it);
            } else {
                ++it;
            }
        }
    }

    void ThreatManager::ProcessDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance) {
        if (a_damage <= 0.0f) return;
        float distanceWeight = GetDistanceWeight(a_distance);
        float weightedDamage = a_damage * distanceWeight;
        AddHate(a_targetID, a_attackerID, weightedDamage);
    }

    void ThreatManager::ProcessBash(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance) {
        float distanceWeight = GetDistanceWeight(a_distance);
        float weightedBash = Settings::GetSingleton()->bashHateValue * distanceWeight;
        AddHate(a_targetID, a_attackerID, weightedBash);
    }

    void ThreatManager::ProcessSummonAggro(RE::FormID a_targetID, RE::FormID a_summonerID, float a_distance) {
        float distanceWeight = GetDistanceWeight(a_distance);
        float weightedSummon = Settings::GetSingleton()->summonHateValue * distanceWeight;
        AddHate(a_targetID, a_summonerID, weightedSummon);
        //SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=SummonAggro)"), a_targetID, a_summonerID, weightedSummon);
    }

    void ThreatManager::ProcessDeathAggro(RE::Actor* a_victim, RE::Actor* a_killer) {
        if (!a_victim || !a_killer) return;

        auto combatGroup = a_killer->GetCombatGroup();
        if (!combatGroup) return;

        float bonus = Settings::GetSingleton()->killTransferBonus;
        RE::FormID killerID = a_killer->GetFormID();

        RE::BSReadLockGuard lock(combatGroup->lock);
        for (auto& combatTarget : combatGroup->targets) {
            auto observer = combatTarget.targetHandle.get();
            if (!observer || observer->IsDead() || observer.get() == a_killer) continue;

            // Check if the observer is an ally of the victim (not hostile to victim)
            if (!observer->IsHostileToActor(a_victim)) {
                float dist = observer->GetPosition().GetDistance(a_victim->GetPosition());
                if (dist < Settings::GetSingleton()->midRange) {
                    AddHate(observer->GetFormID(), killerID, bonus);
                    /*SKSE::log::info(FMT_STRING("[Hate] Observer={:X} Killer={:X} +{:.1f} (type=DeathAggro, Victim={:X})"), 
                        observer->GetFormID(), killerID, bonus, a_victim->GetFormID());*/
                }
            }
        }
    }

}
