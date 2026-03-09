#pragma once
#include <string>
#include <vector>

struct JukeConfig{
	std::string ToggleKey="R";
	std::string BotType="GGL_PPO";
	std::vector<int> PolicySize{1024};
	bool UseSharedHead=true;
	std::vector<int> SharedHeadSize{1024,1024,1024};
	std::vector<int> CriticSize{};
	std::wstring BotPath{};
	bool LayerNorm=true;

	std::string Activation="relu";
	std::string ObsBuilder="AdvancedObsPadded";
	int ObsSize=237;
	int TickSkip=4;
	int ActionDelay=1;
	std::string Device="cuda";
};
