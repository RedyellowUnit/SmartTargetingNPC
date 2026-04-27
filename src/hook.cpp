#include "hook.h"
#include "threat.h"
#include "RE/A/Actor.h"
#include "RE/C/CombatGroup.h"
#include "RE/C/CombatController.h"

namespace Hook {
    class ActorHook {
    public:
        static void Install() {
            REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[0] };
            
            // Hook UpdateCombat (0xE4)
            _UpdateCombat = characterVtable.write_vfunc(0xE4, UpdateCombat);
            
            // Hook HandleHealthDamage (0x104)
            _HandleHealthDamage = characterVtable.write_vfunc(0x104, HandleHealthDamage);
            
            SKSE::log::info("Hooked Actor::UpdateCombat and Actor::HandleHealthDamage");
        }

    private:
        static void HandleHealthDamage(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage) {
            _HandleHealthDamage(a_this, a_attacker, a_damage);

            if (a_this && a_attacker && a_damage > 0.0f) {
                Threat::ThreatManager::GetSingleton()->AddDamage(a_this->GetFormID(), a_attacker->GetFormID(), a_damage);
            }
        }

        static void UpdateCombat(RE::Actor* a_this) {
            _UpdateCombat(a_this);

            if (!a_this || a_this->IsPlayerRef() || !a_this->IsInCombat()) {
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

            {
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
                }
            }
            
            // Periodic cleanup of threat data (could be done here or in a separate timer)
            // For now, just call it occasionally or every update (it has its own internal timing)
            // Threat::ThreatManager::GetSingleton()->Cleanup();
        }

        static inline REL::Relocation<decltype(UpdateCombat)> _UpdateCombat;
        static inline REL::Relocation<decltype(HandleHealthDamage)> _HandleHealthDamage;
    };

    void Install() {
        ActorHook::Install();
    }
}
