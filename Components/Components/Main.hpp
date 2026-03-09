#pragma once
#include "../Component.hpp"
#include <windows.h>
#include <vector>

class MainComponent : public Component
{
public:
	MainComponent();
	~MainComponent() override;

	void OnCreate() override;
	void OnDestroy() override;

	void Initialize();

public:
	static std::vector<std::function<void()>> GameFunctions;

	static void Execute(std::function<void()> FunctionToExecute)
	{
		GameFunctions.push_back(FunctionToExecute);
	}
};

extern class MainComponent Main;
