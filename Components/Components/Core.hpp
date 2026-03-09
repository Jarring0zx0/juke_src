#pragma once
#include "../Component.hpp"

class CoreComponent : public Component
{
private:
	HANDLE MainThread;

public:
	CoreComponent();
	~CoreComponent() override;

public:
	void OnCreate() override;
	void OnDestroy() override;

public:
	void InitializeThread();
	void DestroyThread();
	static unsigned __stdcall InitializeGlobals(void* param);

public:
	static uintptr_t GetGObjects();
	static uintptr_t GetGNames();

private:
	static bool AreGlobalsValid();
	static bool AreGObjectsValid();
	static bool AreGNamesValid();
};

extern class CoreComponent Core;
