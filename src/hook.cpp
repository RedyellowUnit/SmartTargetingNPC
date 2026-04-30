#include "hook.h"
#include "threat.h"
#include <SKSE/SKSE.h>
#include "RE/A/Actor.h"
#include "RE/C/CombatGroup.h"
#include "RE/C/CombatController.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESForm.h"
#include <mutex>
#include <unordered_map>
#include <vector>

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

                    // New: Bash Detection
                    if (a_event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) {
                        float dist = victim->GetPosition().GetDistance(attacker->GetPosition());
                        Threat::ThreatManager::GetSingleton()->AddBashHate(victim->GetFormID(), attacker->GetFormID(), dist);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // DetectionHook: forces NPC to "see" its hate-based focus target within range.
    // -1000 suppression on non-focus targets is intentionally retained:
    //   Without it, vanilla combat AI prefers closer targets, ignoring the distant ranged attacker.
    // The infinite-chase bug is fixed by capping force-detection to kForceDetectRange:
    //   When the player flees beyond this range, focus is not force-detected → vanilla clears target
    //   → focus is cleared → suppression stops → faction NPCs naturally resume fighting each other.
    struct DetectionHook {
        // Beyond this distance, force-detection is not applied.
        // Vanilla's natural escape/disengage logic takes over.
        static constexpr float kForceDetectRange = 3000.0f;

        static std::uint8_t* thunk(RE::Actor* a_source, RE::Actor* a_target,
            std::int32_t& a_detectionValue, std::uint8_t& a_unk04, std::uint8_t& a_unk05,
            std::uint32_t& a_unk06, RE::NiPoint3& a_pos, float& a_unk08, float& a_unk09, float& a_unk10)
        {
            if (a_source && a_target) {
                RE::FormID focus = Threat::ThreatManager::GetSingleton()->GetFocus(a_source->GetFormID());
                if (focus != 0) {
                    if (a_target->GetFormID() == focus) {
                        auto result = func(a_source, a_target, a_detectionValue, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
                        float dist = a_source->GetPosition().GetDistance(a_target->GetPosition());
                        if (dist < kForceDetectRange) {
                            a_detectionValue = 1000; // Force detected within combat range
                        }
                        // Beyond kForceDetectRange: leave vanilla result as-is → escape is possible
                        return result;
                    } else {
                        // Suppress non-focus targets so vanilla AI is forced to pursue
                        // the hate-based focus (e.g., distant mage over close melee enemy).
                        // This suppression ends automatically when focus is cleared (player escaped),
                        // allowing vanilla faction-vs-faction combat to resume.
                        a_detectionValue = -1000;
                        return nullptr;
                    }
                }
            }
            return func(a_source, a_target, a_detectionValue, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> target{ REL::VariantID(41659, 42742, 0) };
            uintptr_t hookAddr = target.address() + REL::Relocate(0x526, 0x67B, 0);
            auto& trampoline = SKSE::GetTrampoline();
            func = trampoline.write_call<5>(hookAddr, thunk);
            SKSE::log::info("Installed Detection Hook at {:X}", hookAddr);
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
                if (a_this) Threat::ThreatManager::GetSingleton()->ClearHate(a_this->GetFormID());
                return;
            }

            RE::FormID victimID = a_this->GetFormID();

            // Handle Decay
            auto now = std::chrono::steady_clock::now();
            float deltaTime = 0.0f;
            {
                std::lock_guard<std::mutex> lock(_updateMutex);
                auto it = _lastUpdateMap.find(victimID);
                if (it != _lastUpdateMap.end()) {
                    deltaTime = std::chrono::duration<float>(now - it->second).count();
                }
                _lastUpdateMap[victimID] = now;
            }

            if (deltaTime > 0.0f) {
                Threat::ThreatManager::GetSingleton()->ApplyDecay(victimID, deltaTime);
            }

            // Detect damage via health delta since last hit
            float currentHealth = a_this->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            RE::FormID attackerID;
            float damage;
            if (HitTracker::GetSingleton()->GetAndClearDamage(victimID, currentHealth, attackerID, damage)) {
                if (damage > 0.0f) {
                    auto attacker = RE::TESForm::LookupByID<RE::Actor>(attackerID);
                    float dist = attacker ? a_this->GetPosition().GetDistance(attacker->GetPosition()) : 0.0f;
                    Threat::ThreatManager::GetSingleton()->AddDamage(victimID, attackerID, damage, dist);
                }
            }

            // Target Switching Logic (only for NPCs)
            if (a_this->IsPlayerRef() || !a_this->IsInCombat()) {
                Threat::ThreatManager::GetSingleton()->ClearFocus(victimID);
                if (!a_this->IsPlayerRef() && !a_this->IsInCombat()) {
                    Threat::ThreatManager::GetSingleton()->ClearHate(victimID);
                }
                return;
            }

            auto combatGroup = a_this->GetCombatGroup();
            if (!combatGroup) {
                return;
            }

            auto& runtimeData = a_this->GetActorRuntimeData();
            auto currentTarget = runtimeData.currentCombatTarget.get();

            // If the vanilla engine cleared the target (e.g., player fled out of detection range),
            // respect that decision and do not force a new target via hate scores alone.
            // This allows vanilla faction-vs-faction combat to resume naturally.
            if (!currentTarget) {
                Threat::ThreatManager::GetSingleton()->ClearFocus(victimID);
                return;
            }
            
            std::vector<Threat::TargetCandidate> candidates;
            {
                RE::BSReadLockGuard lock(combatGroup->lock);
                for (auto& combatTarget : combatGroup->targets) {
                    auto target = combatTarget.targetHandle.get();
                    if (!target || target->IsDead()) continue;
                    float dist = a_this->GetPosition().GetDistance(target->GetPosition());
                    candidates.push_back({ target, dist });
                }
            }

            auto bestTarget = Threat::ThreatManager::GetSingleton()->EvaluateBestTarget(a_this, currentTarget.get(), candidates);

            if (bestTarget && bestTarget != currentTarget.get()) {
                runtimeData.currentCombatTarget = bestTarget->GetHandle();
                
                if (runtimeData.combatController) {
                    runtimeData.combatController->targetHandle = bestTarget->GetHandle();
                    runtimeData.combatController->previousTargetHandle = currentTarget ? currentTarget->GetHandle() : RE::ActorHandle();
                }
            }
        }

        static inline REL::Relocation<decltype(UpdateCombat)> _UpdateCombat;
        static inline std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> _lastUpdateMap;
        static inline std::mutex _updateMutex;
    };

    void Install() {
        ActorHook::Install();
        DetectionHook::Install();
    }

    void RegisterEvents() {
        auto source = RE::ScriptEventSourceHolder::GetSingleton();
        if (source) {
            source->AddEventSink<RE::TESHitEvent>(HitEventSink::GetSingleton());
            SKSE::log::info("Registered TESHitEvent Sink");
        }
    }
}
