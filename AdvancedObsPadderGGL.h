#pragma once
#include <RLGymCPP/ObsBuilders/ObsBuilder.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/Gamestates/Player.h>
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <cmath>
namespace RLGC {
class AdvancedObsPadderGGL : public ObsBuilder {
public:
	int teamSize;
	float POS_STD, ANG_STD;
	bool expanding;
	AdvancedObsPadderGGL(int teamSize = 3, bool expanding = false);
	virtual FList BuildObs(const Player& player, const GameState& state) override;
	virtual void BuildObs2(const Player& player, const GameState& state, FList& out);
private:
	void AddDummy(FList& obs);
	PhysState AddPlayerToObs(FList& obs, const Player& player, const PhysState& ball, bool inv);
};
}
