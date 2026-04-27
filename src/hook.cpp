#include "hook.h"
#include "RE/A/Actor.h"
#include "RE/C/CombatGroup.h"
#include "RE/C/CombatController.h"

namespace Hook {
    class ActorHook {
    public:
        static void Install() {
            // Hook Character VTABLE (covers most NPCs)
            REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[0] };
            _UpdateCombat = characterVtable.write_vfunc(0xE4, UpdateCombat);
            
            SKSE::log::info("Hooked Actor::UpdateCombat for Character");
        }

    private:
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
            
            float currentDistance = 1000000.0f;
            if (currentTarget) {
                currentDistance = a_this->GetPosition().GetDistance(currentTarget->GetPosition());
            }

            RE::NiPointer<RE::Actor> closestTarget = nullptr;
            float minDistance = 1000000.0f;

            {
                RE::BSReadLockGuard lock(combatGroup->lock);
                for (auto& combatTarget : combatGroup->targets) {
                    auto targetHandle = combatTarget.targetHandle;
                    auto target = targetHandle.get();
                    if (!target || target->IsDead()) {
                        continue;
                    }

                    // Only consider targets that the NPC has actually detected
                    if (combatTarget.detectLevel <= 0) {
                        continue;
                    }

                    float dist = a_this->GetPosition().GetDistance(target->GetPosition());
                    if (dist < minDistance) {
                        minDistance = dist;
                        closestTarget = target;
                    }
                }
            }

            if (closestTarget && closestTarget.get() != currentTarget.get()) {
                // Threshold: Only switch if the new target is at least 15% closer than the current one
                // or if we have no current target.
                bool shouldSwitch = false;
                if (!currentTarget) {
                    shouldSwitch = true;
                } else {
                    float threshold = currentDistance * 0.85f; // 15% closer
                    if (minDistance < threshold) {
                        shouldSwitch = true;
                    }
                }

                if (shouldSwitch) {
                    runtimeData.currentCombatTarget = closestTarget->GetHandle();
                    
                    if (runtimeData.combatController) {
                        runtimeData.combatController->targetHandle = closestTarget->GetHandle();
                        runtimeData.combatController->previousTargetHandle = currentTarget ? currentTarget->GetHandle() : RE::ActorHandle();
                    }
                    
                    // Optional: Notify the AI to re-evaluate its current path/action
                    // a_this->EvaluatePackage(false, false);
                }
            }
        }

        static inline REL::Relocation<decltype(UpdateCombat)> _UpdateCombat;
    };

    void Install() {
        ActorHook::Install();
    }
}
