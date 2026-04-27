#include "hook.h"
#include "threat.h"
#include "RE/A/Actor.h"
#include "RE/C/CombatGroup.h"
#include "RE/C/CombatController.h"
#include "RE/T/TESHitEvent.h"

namespace Hook {
    struct LastHitData {
        RE::FormID attackerID;
        float lastHealth;
        std::chrono::steady_clock::time_point timestamp;
    };

    class HitTracker {
    public:
        static HitTracker* GetSingleton() {
            static HitTracker singleton;
            return &singleton;
        }

        void RegisterHit(RE::FormID a_victim, RE::FormID a_attacker, float a_currentHealth) {
            std::lock_guard<std::mutex> lock(_mutex);
            _lastHits[a_victim] = { a_attacker, a_currentHealth, std::chrono::steady_clock::now() };
        }

        bool GetAndClearDamage(RE::FormID a_victim, float a_currentHealth, RE::FormID& out_attacker, float& out_damage) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _lastHits.find(a_victim);
            if (it != _lastHits.end()) {
                auto& data = it->second;
                
                // If health dropped since the last hit/check
                if (a_currentHealth < data.lastHealth) {
                    out_damage = data.lastHealth - a_currentHealth;
                    out_attacker = data.attackerID;
                    data.lastHealth = a_currentHealth;
                    return true;
                }
                
                // Timeout hit data after 5 seconds to avoid misattribution
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - data.timestamp).count() > 5) {
                    _lastHits.erase(it);
                }
            }
            return false;
        }

    private:
        std::unordered_map<RE::FormID, LastHitData> _lastHits;
        std::mutex _mutex;
    };

    class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
    public:
        static HitEventSink* GetSingleton() {
            static HitEventSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override {
            if (a_event && a_event->target && a_event->cause) {
                auto victim = a_event->target->As<RE::Actor>();
                auto attacker = a_event->cause->As<RE::Actor>();
                if (victim && attacker) {
                    float health = victim->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
                    HitTracker::GetSingleton()->RegisterHit(victim->GetFormID(), attacker->GetFormID(), health);
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class ActorHook {
    public:
        static void Install() {
            // Hook UpdateCombat (VTable index 0xE4) - Confirmed working
            REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[0] };
            _UpdateCombat = characterVtable.write_vfunc(0xE4, UpdateCombat);

            SKSE::log::info("Hooked UpdateCombat (VTable[0])");
        }

    private:
        static void UpdateCombat(RE::Actor* a_this) {
            _UpdateCombat(a_this);

            if (!a_this || a_this->IsDead()) {
                return;
            }

            // Detect damage via health delta since last hit
            float currentHealth = a_this->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            RE::FormID attackerID;
            float damage;
            if (HitTracker::GetSingleton()->GetAndClearDamage(a_this->GetFormID(), currentHealth, attackerID, damage)) {
                if (damage > 0.0f) {
                    /*SKSE::log::info(FMT_STRING("[Damage] Victim={:X}, Attacker={:X}, Amount={:.1f}"),
                        a_this->GetFormID(), attackerID, damage);*/
                    
                    Threat::ThreatManager::GetSingleton()->AddDamage(a_this->GetFormID(), attackerID, damage);
                }
            }

            // Target Switching Logic (only for NPCs)
            if (a_this->IsPlayerRef() || !a_this->IsInCombat()) {
                return;
            }

            auto combatGroup = a_this->GetCombatGroup();
            if (!combatGroup) {
                return;
            }

            auto& runtimeData = a_this->GetActorRuntimeData();
            auto currentTarget = runtimeData.currentCombatTarget.get();
            
            float currentDPS = 0.0f;
            float currentDistance = 1000000.0f;
            
            if (currentTarget) {
                currentDPS = Threat::ThreatManager::GetSingleton()->GetDPS(a_this->GetFormID(), currentTarget->GetFormID());
                currentDistance = a_this->GetPosition().GetDistance(currentTarget->GetPosition());
            }

            RE::NiPointer<RE::Actor> bestTarget = nullptr;
            float bestDPS = 0.0f;
            float minDistance = 1000000.0f;

            RE::BSReadLockGuard lock(combatGroup->lock);
            for (auto& combatTarget : combatGroup->targets) {
                auto targetHandle = combatTarget.targetHandle;
                auto target = targetHandle.get();
                if (!target || target->IsDead() || combatTarget.detectLevel <= 0) {
                    continue;
                }

                float dps = Threat::ThreatManager::GetSingleton()->GetDPS(a_this->GetFormID(), target->GetFormID());
                float dist = a_this->GetPosition().GetDistance(target->GetPosition());

                // Selection Logic:
                // 1. Higher DPS wins.
                // 2. If DPS is close (difference < 1.0 or < 10%), closer distance wins.
                
                bool isBetter = false;
                if (!bestTarget) {
                    isBetter = true;
                } else {
                    float dpsDiff = dps - bestDPS;
                    if (dpsDiff > 1.0f || (bestDPS > 0 && dpsDiff / bestDPS > 0.1f)) {
                        // Significantly higher DPS
                        isBetter = true;
                    } else if (std::abs(dpsDiff) < 1.0f || (bestDPS > 0 && std::abs(dpsDiff / bestDPS) < 0.1f)) {
                        // DPS is similar, compare distance
                        if (dist < minDistance) {
                            isBetter = true;
                        }
                    }
                }

                if (isBetter) {
                    bestTarget = target;
                    bestDPS = dps;
                    minDistance = dist;
                }
            }

            if (bestTarget && bestTarget.get() != currentTarget.get()) {
                // Threshold to switch:
                // Only switch if the best target is significantly better than current.
                bool shouldSwitch = false;
                
                float dpsDiff = bestDPS - currentDPS;
                if (dpsDiff > 1.0f || (currentDPS > 0 && dpsDiff / currentDPS > 0.15f)) {
                    shouldSwitch = true;
                } else if (std::abs(dpsDiff) < 1.0f || (currentDPS > 0 && std::abs(dpsDiff / currentDPS) < 0.15f)) {
                    // Similar DPS, check if distance is much better
                    if (minDistance < currentDistance * 0.85f) {
                        shouldSwitch = true;
                    }
                }

                if (shouldSwitch) {
                    runtimeData.currentCombatTarget = bestTarget->GetHandle();
                    
                    if (runtimeData.combatController) {
                        runtimeData.combatController->targetHandle = bestTarget->GetHandle();
                        runtimeData.combatController->previousTargetHandle = currentTarget ? currentTarget->GetHandle() : RE::ActorHandle();
                    }

                    /*SKSE::log::info(FMT_STRING("[TargetSwitch] {:X} switched target to {:X} (DPS: {:.1f}, Dist: {:.0f})"),
                        a_this->GetFormID(), bestTarget->GetFormID(), bestDPS, minDistance);*/
                }
            }
        }

        static inline REL::Relocation<decltype(UpdateCombat)> _UpdateCombat;
    };

    void Install() {
        ActorHook::Install();
    }

    void RegisterEvents() {
        auto source = RE::ScriptEventSourceHolder::GetSingleton();
        if (source) {
            source->AddEventSink<RE::TESHitEvent>(HitEventSink::GetSingleton());
            SKSE::log::info("Registered TESHitEvent Sink");
        }
    }
}
