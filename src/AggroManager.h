#pragma once

#include "RE/A/Actor.h"

namespace Aggro {

    class AggroManager {
    public:
        static AggroManager* GetSingleton() {
            static AggroManager singleton;
            return &singleton;
        }

        void ProcessDamage(RE::FormID a_targetID, RE::FormID a_attackerID, float a_damage, float a_distance);
        void ProcessBash(RE::FormID a_targetID, RE::FormID a_attackerID, float a_distance);

        static constexpr float kBashHateValue = 50.0f;

    private:
        AggroManager() = default;
    };
}
