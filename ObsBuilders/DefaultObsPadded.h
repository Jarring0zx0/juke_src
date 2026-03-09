#pragma once
#include "DefaultObs.h"

namespace RLGC {

	class DefaultObsPadded : public DefaultObs {
	public:

		int maxPlayers;

		DefaultObsPadded(
			int maxPlayers,
			Vec posCoef = Vec(1 / CommonValues::SIDE_WALL_X, 1 / CommonValues::BACK_WALL_Y, 1 / CommonValues::CEILING_Z),
			float velCoef = 1 / CommonValues::CAR_MAX_SPEED,
			float angVelCoef = 1 / CommonValues::CAR_MAX_ANG_VEL
		) : DefaultObs(posCoef, velCoef, angVelCoef), maxPlayers(maxPlayers) {

		}

		virtual FList BuildObs(const Player& player, const GameState& state);
	};
}
