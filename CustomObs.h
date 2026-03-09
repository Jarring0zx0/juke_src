#pragma once
#include "AdvancedObsPadderGGL.h"
#include <RLGymCPP/ObsBuilders/ObsBuilder.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/Gamestates/Player.h>
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <cmath>
#include <limits>

namespace RLGC {

class CustomObs : public ObsBuilder {
public:
    int teamSize;
    float POS_STD, ANG_STD;

    CustomObs(int teamSize = 3);
    virtual ~CustomObs() = default;

    virtual FList BuildObs(const Player& player, const GameState& state) override;
    virtual void BuildObs2(const Player& player, const GameState& state, FList& out);

private:
    void AddDummy(FList& obs);
    PhysState AddPlayerToObs(FList& obs, const Player& player, const PhysState& ball, bool inv);

    const Player* FindClosestOpponentToBall(const Player& me, const GameState& state, const PhysState& ball);
};

}
