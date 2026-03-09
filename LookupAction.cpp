#include "LookupAction.h"
#include <vector>

namespace RLGC {

LookupAction::LookupAction() {
    MakeLookupTable();
}

void LookupAction::MakeLookupTable() {// bag ho bag ho

    std::vector<float> bins = {-1.0f, 0.0f, 1.0f};

    for (float throttle : bins) {
        for (float steer : bins) {
            for (int boost = 0; boost <= 1; ++boost) {
                for (int handbrake = 0; handbrake <= 1; ++handbrake) {
                    if (boost == 1 && throttle != 1.0f) continue;

                    Action a{};

                    const float t_val = (throttle != 0.0f) ? throttle : (float)boost;
                    a.throttle = t_val;
                    a.steer = steer;
                    a.pitch = 0.0f;
                    a.yaw = steer;
                    a.roll = 0.0f;
                    a.jump = 0.0f;
                    a.boost = (float)boost;
                    a.handbrake = (float)handbrake;
                    lookupTable.push_back(a);
                }
            }
        }
    }

    for (float pitch : bins) {
        for (float yaw : bins) {
            for (float roll : bins) {
                for (int jump = 0; jump <= 1; ++jump) {
                    for (int boost = 0; boost <= 1; ++boost) {
                        if (jump == 1 && yaw != 0.0f) continue;
                        if (pitch == 0.0f && roll == 0.0f && jump == 0) continue;

                        bool hb = (jump == 1) && (pitch != 0.0f || yaw != 0.0f || roll != 0.0f);

                        Action a{};
                        a.throttle = (float)boost;
                        a.steer = yaw;
                        a.pitch = pitch;
                        a.yaw = yaw;
                        a.roll = roll;
                        a.jump = (float)jump;
                        a.boost = (float)boost;
                        a.handbrake = hb ? 1.0f : 0.0f;
                        lookupTable.push_back(a);
                    }
                }
            }
        }
    }
}

Action LookupAction::ParseAction(int actionIdx, const Player& player, const GameState& state) {
    if (actionIdx < 0 || actionIdx >= (int)lookupTable.size()) return Action{};
    return lookupTable[actionIdx];
}

int LookupAction::GetActionAmount() {
    return (int)lookupTable.size();
}

}
