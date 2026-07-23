#include "hook.h"
#include "threat.h"
#include "settings.h"
#include <SKSE/SKSE.h>
#include "RE/A/Actor.h"
#include "RE/C/CombatGroup.h"
#include "RE/C/CombatController.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESCombatEvent.h"
#include "RE/T/TESDeathEvent.h"
#include "RE/T/TESForm.h"
#include "RE/S/ScriptEventSourceHolder.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Hook {
    class HitTracker {
    public:
        static HitTracker* GetSingleton() {
            static HitTracker singleton;
            return &singleton;
        }

        void RegisterHit(RE::FormID a_victim, RE::FormID a_attacker) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto now = std::chrono::steady_clock::now();
            auto it = _lastHits.find(a_victim);

            // Throttle hit registration from the same attacker (100ms) 
            // to reduce processing overhead during high-frequency hit events (e.g., Flame concentration spells).
            if (it != _lastHits.end() && it->second.attackerID == a_attacker) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.timestamp).count() < 100) {
                    return;
                }
            }
            _lastHits[a_victim] = { a_attacker, now };
        }

        RE::FormID GetLastAttacker(RE::FormID a_victim) {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _lastHits.find(a_victim);
            if (it != _lastHits.end()) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count() <= 5) {
                    return it->second.attackerID;
                }
            }
            return 0;
        }

        void Reset() {
            std::lock_guard<std::mutex> lock(_mutex);
            _lastHits.clear();
        }

    private:
        struct HitRecord {
            RE::FormID attackerID;
            std::chrono::steady_clock::time_point timestamp;
        };
        std::unordered_map<RE::FormID, HitRecord> _lastHits;
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
                    HitTracker::GetSingleton()->RegisterHit(victim->GetFormID(), attacker->GetFormID());

                    // New: Bash Detection
                    if (a_event->flags.any(RE::TESHitEvent::Flag::kBashAttack)) {
                        float dist = victim->GetPosition().GetDistance(attacker->GetPosition());
                        Threat::ThreatManager::GetSingleton()->ProcessBash(victim->GetFormID(), attacker->GetFormID(), dist);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class CombatEventSink : public RE::BSTEventSink<RE::TESCombatEvent> {
    public:
        static CombatEventSink* GetSingleton() {
            static CombatEventSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*) override {
            if (a_event && a_event->newState == RE::ACTOR_COMBAT_STATE::kCombat) {
                auto actor1Ptr = a_event->actor.get();
                auto actor2Ptr = a_event->targetActor.get();
                auto actor1 = actor1Ptr ? actor1Ptr->As<RE::Actor>() : nullptr;
                auto actor2 = actor2Ptr ? actor2Ptr->As<RE::Actor>() : nullptr;

                if (!actor1 || !actor2) return RE::BSEventNotifyControl::kContinue;

                // Check both ways.
                // If actor1 is the summon, actor2 is the NPC gaining hate towards the summoner.
                if (actor1->IsCommandedActor()) {
                    auto summoner = actor1->GetCommandingActor().get();
                    if (summoner && !summoner->IsDead()) {
                        float dist = actor2->GetPosition().GetDistance(summoner->GetPosition());
                        Threat::ThreatManager::GetSingleton()->ProcessSummonAggro(actor2->GetFormID(), summoner->GetFormID(), dist);
                    }
                }
                
                // If actor2 is the summon, actor1 is the NPC gaining hate towards the summoner.
                if (actor2->IsCommandedActor()) {
                    auto summoner = actor2->GetCommandingActor().get();
                    if (summoner && !summoner->IsDead()) {
                        float dist = actor1->GetPosition().GetDistance(summoner->GetPosition());
                        Threat::ThreatManager::GetSingleton()->ProcessSummonAggro(actor1->GetFormID(), summoner->GetFormID(), dist);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class DeathEventSink : public RE::BSTEventSink<RE::TESDeathEvent> {
    public:
        static DeathEventSink* GetSingleton() {
            static DeathEventSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override {
            if (a_event && a_event->actorDying) {
                auto victim = a_event->actorDying->As<RE::Actor>();
                if (victim) {
                    // Try to find the actual physical killer via HitTracker (last attacker within 5s)
                    RE::FormID killerID = HitTracker::GetSingleton()->GetLastAttacker(victim->GetFormID());
                    RE::Actor* killer = nullptr;
                    
                    if (killerID != 0) {
                        killer = RE::TESForm::LookupByID<RE::Actor>(killerID);
                    }
                    
                    // Fallback to engine's reported killer if tracker didn't find anyone
                    if (!killer && a_event->actorKiller) {
                        killer = a_event->actorKiller->As<RE::Actor>();
                    }

                    if (killer) {
                        Threat::ThreatManager::GetSingleton()->ProcessDeathAggro(victim, killer);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    // DetectionHook: forces NPC to "see" its hate-based focus target within range.
    // -1000 suppression on non-focus targets is intentionally retained.
    // Fixed: Always returns the original engine function's result to prevent stack/register corruption (CTD).
    struct DetectionHook {

        static std::uint8_t* thunk(RE::Actor* a_source, RE::Actor* a_target,
            std::int32_t& a_detectionValue, std::uint8_t& a_unk04, std::uint8_t& a_unk05,
            std::uint32_t& a_unk06, RE::NiPoint3& a_pos, float& a_unk08, float& a_unk09, float& a_unk10)
        {
            if (a_source && a_target) {
                RE::FormID focus = Threat::ThreatManager::GetSingleton()->GetFocus(a_source->GetFormID());
                if (focus != 0) {
                    if (a_target->GetFormID() == focus) {
                        // Priority Target: Force-detect the actor to keep the NPC focused on high-threat targets.
                        auto result = func(a_source, a_target, a_detectionValue, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
                        float dist = a_source->GetPosition().GetDistance(a_target->GetPosition());
                        
                        if (dist < Settings::GetSingleton()->forceDetectRange && a_detectionValue > 0) {
                            a_detectionValue = 1000;
                        }
                        return result;
                    } else {
                        // Non-Focus Target: Suppress detection if the current primary focus is still at a distance.
                        // This prevents the NPC from being easily distracted by closer, lower-threat enemies.
                        // Stale/invalid focus (e.g. after death+reload) must not suppress detection.
                        auto focusActor = RE::TESForm::LookupByID<RE::Actor>(focus);
                        if (!focusActor || focusActor->IsDead() || focusActor->IsDeleted()) {
                            Threat::ThreatManager::GetSingleton()->ClearFocus(a_source->GetFormID());
                            return func(a_source, a_target, a_detectionValue, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
                        }

                        float focusDist = a_source->GetPosition().GetDistance(focusActor->GetPosition());
                        
                        // CRITICAL: We must ALWAYS execute and return the result of the original engine function (func).
                        // Bypassing 'func' or returning nullptr causes register corruption (RAX holding garbage)
                        // leading to physics/Havok CTDs during high-frequency hit events.
                        auto result = func(a_source, a_target, a_detectionValue, a_unk04, a_unk05, a_unk06, a_pos, a_unk08, a_unk09, a_unk10);
                        if (focusDist > Settings::GetSingleton()->nearRange) {
                            a_detectionValue = -1000;
                        }
                        return result;
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
            // Hook Actor VTable
            // Index 0xE4: UpdateCombat
            REL::Relocation<std::uintptr_t> characterVtable{ RE::VTABLE_Character[0] };
            _UpdateCombat = characterVtable.write_vfunc(0xE4, UpdateCombat);

            SKSE::log::info("Hooked UpdateCombat (VTable[0xE4])");
        }

        static void Reset() {
            std::lock_guard<std::mutex> lock(_updateMutex);
            _lastUpdateMap.clear();
            _actorHealthMap.clear();
        }

    private:
        static void UpdateCombat(RE::Actor* a_this) {
            _UpdateCombat(a_this);

            if (!a_this || a_this->IsDead()) {
                if (a_this) {
                    auto* threat = Threat::ThreatManager::GetSingleton();
                    threat->ClearHate(a_this->GetFormID());
                    threat->ClearFocus(a_this->GetFormID());
                }
                return;
            }

            RE::FormID victimID = a_this->GetFormID();

            // Detect damage via health delta
            float currentHealth = a_this->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            float lastHealth = currentHealth;
            {
                std::lock_guard<std::mutex> lock(_updateMutex);
                auto itHealth = _actorHealthMap.find(victimID);
                if (itHealth != _actorHealthMap.end()) {
                    lastHealth = itHealth->second;
                }
                _actorHealthMap[victimID] = currentHealth;
            }

            float damage = lastHealth - currentHealth;
            if (damage > 0.0f) {
                RE::FormID attackerID = HitTracker::GetSingleton()->GetLastAttacker(victimID);
                if (attackerID != 0) {
                    auto attacker = RE::TESForm::LookupByID<RE::Actor>(attackerID);
                    float dist = attacker ? a_this->GetPosition().GetDistance(attacker->GetPosition()) : 0.0f;
                    Threat::ThreatManager::GetSingleton()->ProcessDamage(victimID, attackerID, damage, dist);
                }
            }

            // Handle Decay and AI processing
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
        static inline std::unordered_map<RE::FormID, float> _actorHealthMap;
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
            source->AddEventSink<RE::TESCombatEvent>(CombatEventSink::GetSingleton());
            source->AddEventSink<RE::TESDeathEvent>(DeathEventSink::GetSingleton());
            SKSE::log::info("Registered TESHitEvent Sink");
            SKSE::log::info("Registered TESCombatEvent Sink");
            SKSE::log::info("Registered TESDeathEvent Sink");
        }
    }

    void ResetRuntimeState() {
        Threat::ThreatManager::GetSingleton()->Reset();
        HitTracker::GetSingleton()->Reset();
        ActorHook::Reset();
        SKSE::log::info("Reset threat/focus runtime state");
    }
}
