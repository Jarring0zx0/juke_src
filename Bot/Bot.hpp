#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include "JukeConfig.hpp"
#include "../RLSDK/SDK_HEADERS/TAGame_classes.hpp"

extern std::string g_ToggleKey;
extern std::atomic<bool> g_BotEnabled;
extern FVehicleInputs g_LatestInput;
extern std::mutex g_InputMx;
extern JukeConfig g_JukeConfig;

namespace JukeBot {
std::wstring ExeDir();
std::wstring BootstrapPath();
bool LoadBootstrapConfig(JukeConfig& cfg);
void SetupDllSearch();
void ApplyConfig(const JukeConfig& cfg);
void Inference_Init();
void Inference_OnTick(float dt);
void OnAttach(HMODULE mod);
void OnDetach();
}
