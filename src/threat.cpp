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
                }

                // Dragon Priority Logic
                if (dragonKeyword && candidate.actor->HasKeyword(dragonKeyword)) {
                    hate += Settings::GetSingleton()->dragonBonus;
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
        if (bestTarget && bestTarget != a_currentTarget) {
            float hateDiff = bestHate - currentHate;
            if (hateDiff > 0.1f || (currentHate > 0 && hateDiff / currentHate > Settings::GetSingleton()->switchThreshold)) {
                SetFocus(observerID, bestTarget->GetFormID());
                
                SKSE::log::info(FMT_STRING("[TargetSwitch] {:X} ({}) switched target to {:X} ({}) (Hate={:.1f}, Dist={:.1f})"),
                    observerID, a_observer->GetDisplayFullName(), 
                    bestTarget->GetFormID(), bestTarget->GetDisplayFullName(),
                    bestHate, a_observer->GetPosition().GetDistance(bestTarget->GetPosition()));

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
        /*SKSE::log::info(FMT_STRING("[Hate] Victim={:X} Attacker={:X} +{:.1f} (type=SummonAggro)"), a_targetID, a_summonerID, weightedSummon);*/
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
