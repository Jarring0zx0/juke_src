#pragma once
#include "../Gamestates/GameState.h"
#include "../BasicTypes/Action.h"
#include "../BasicTypes/Lists.h"

namespace RLGC {
	class ObsBuilder {
	public:
		virtual void Reset(const GameState& initialState) {}

		virtual FList BuildObs(const Player& player, const GameState& state) = 0;
	virtual void BuildObs2(const Player& player, const GameState& state, FList& out) {
		out = BuildObs(player, state);
	}
	};
}
