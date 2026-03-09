#include "pch.hpp"
#include "DLL/IPC.hpp"
#include "Bot/Bot.hpp"
#include <thread>

JukePipeClient g_JukeLog;

static void InitUI() {}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		g_JukeLog.Connect();
		std::thread([hModule]{
			InitUI();
			JukeBot::OnAttach(hModule);
		}).detach();
		break;
	case DLL_PROCESS_DETACH:
		JukeBot::OnDetach();
		g_JukeLog.Disconnect();
		break;
	default:
		break;
	}
	return TRUE;
}
