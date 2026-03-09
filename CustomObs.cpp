#include "CustomObs.h"
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <vector>

namespace RLGC {

CustomObs::CustomObs(int teamSize)
    : teamSize(teamSize)
    , POS_STD(2300.0f)
    , ANG_STD(3.14159265358979323846f)
{}

FList CustomObs::BuildObs(const Player& player, const GameState& state) {
    FList obs;
    BuildObs2(player, state, obs);
    return obs;
}

void CustomObs::BuildObs2(const Player& player, const GameState& state, FList& obs) {
    obs.clear();

    const float POS_COEF = 1.0f / 2300.0f;
    const float ANG_COEF = 1.0f / 3.14159265358979323846f;
    bool inv = (player.team == Team::ORANGE);
    PhysState ball = InvertPhys(state.ball, inv);

    obs += ball.pos * POS_COEF;
    obs += ball.vel * POS_COEF;
    obs += ball.angVel * ANG_COEF;

    for (int i = 0; i < Action::ELEM_AMOUNT; i++) {
        obs += player.prevAction[i];
    }

    auto& pads = state.GetBoostPads(inv);
    for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
        obs += (pads[i] ? 1.0f : 0.0f);
    }

    PhysState meCar = AddPlayerToObs(obs, player, ball, inv);

    for (int i = 0; i < teamSize - 1; i++) {
        AddDummy(obs);
    }

    const Player* closestOpp = FindClosestOpponentToBall(player, state, ball);

    int enemyCount = 0;
    if (closestOpp != nullptr) {
        PhysState oc = AddPlayerToObs(obs, *closestOpp, ball, inv);
        obs += (oc.pos - meCar.pos) * POS_COEF;
        obs += (oc.vel - meCar.vel) * POS_COEF;
        enemyCount++;
    }

    while (enemyCount < teamSize) {
        AddDummy(obs);
        enemyCount++;
    }
}

const Player* CustomObs::FindClosestOpponentToBall(const Player& me, const GameState& state, const PhysState& ball) {
    const Player* closest = nullptr;
    float minDist = std::numeric_limits<float>::max();
    bool inv = (me.team == Team::ORANGE);

    for (const auto& other : state.players) {

        if (other.carId == me.carId || other.team == me.team) {
            continue;
        }

        PhysState otherCar = InvertPhys(other, inv);

        float dx = otherCar.pos.x - ball.pos.x;
        float dy = otherCar.pos.y - ball.pos.y;
        float dz = otherCar.pos.z - ball.pos.z;
        float dist = dx * dx + dy * dy + dz * dz;

        if (dist < minDist) {
            minDist = dist;
            closest = &other;
        }
    }

    return closest;
}

void CustomObs::AddDummy(FList& obs) {

    for (int i = 0; i < 7; i++) {
        obs += {0, 0, 0};
    }
    obs += {0, 0, 0, 0, 0};

    obs += {0, 0, 0};
    obs += {0, 0, 0};
}

PhysState CustomObs::AddPlayerToObs(FList& obs, const Player& player, const PhysState& ball, bool inv) {
    const float POS_COEF = 1.0f / 2300.0f;
    const float ANG_COEF = 1.0f / 3.14159265358979323846f;
    PhysState car = InvertPhys(player, inv);

    obs += (ball.pos - car.pos) * POS_COEF;
    obs += (ball.vel - car.vel) * POS_COEF;
    obs += car.pos * POS_COEF;
    obs += car.rotMat.forward;
    obs += car.rotMat.up;
    obs += car.vel * POS_COEF;
    obs += car.angVel * ANG_COEF;

    obs += player.boost / 100.f;
    obs += (float)player.isOnGround;
    float hasFlipOrJump = player.isOnGround ? 1.0f : (player.hasDoubleJumped ? 0.0f : 1.0f);
    obs += hasFlipOrJump;
    obs += (float)player.isDemoed;
    obs += 0.0f;

    return car;
}

}
