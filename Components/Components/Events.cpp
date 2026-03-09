#include "Events.hpp"
#include "../Includes.hpp"
#include "../../Bot/JukeSnap.hpp"
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>
extern "C" void Juke_LogW(const wchar_t*);
extern std::string g_ToggleKey;
extern std::atomic<bool> g_BotEnabled;
extern std::atomic<bool> g_ModelOk;
extern std::atomic<uintptr_t> g_GameEventAddr;
extern std::atomic<uintptr_t> g_GameShareAddr;
extern FVehicleInputs g_LatestInput;
extern std::mutex g_InputMx;

void Inference_OnTick(float dt);

PreEvent::PreEvent() : PE_Caller(nullptr), PE_Function(nullptr), PE_Params(nullptr), PE_Detour(true) {}

PreEvent::PreEvent(class UObject* caller, class UFunction* function, void* params) : PE_Caller(caller), PE_Function(function), PE_Params(params), PE_Detour(true) {}

PreEvent::~PreEvent() {}

class UObject* PreEvent::Caller() const
{
	return PE_Caller;
}

template <typename T> T* PreEvent::GetCaller() const
{
	if (PE_Caller)
	{
		return static_cast<T*>(PE_Caller);
	}

	return nullptr;
}

class UFunction* PreEvent::Function() const
{
	return PE_Function;
}

void* PreEvent::Params() const
{
	return PE_Params;
}

template <typename T> T* PreEvent::GetParams() const
{
	if (PE_Params)
	{
		return reinterpret_cast<T*>(PE_Params);
	}

	return nullptr;
}

bool PreEvent::ShouldDetour() const
{
	if (PE_Function && EventsComponent::IsEventBlacklisted(PE_Function->ObjectInternalInteger))
	{
		return false;
	}

	return PE_Detour;
}

void PreEvent::SetDetour(bool bDetour)
{
	PE_Detour = bDetour;
}

PreEvent PreEvent::operator=(const PreEvent& other)
{
	PE_Caller = other.PE_Caller;
	PE_Function = other.PE_Function;
	PE_Params = other.PE_Params;
	PE_Detour = other.PE_Detour;
	return *this;
}

PostEvent::PostEvent() : PE_Result(nullptr) {}

PostEvent::PostEvent(class UObject* caller, class UFunction* function, void* params, void* result) : PreEvent(caller, function, params), PE_Result(result) {}

PostEvent::~PostEvent() {}

void* PostEvent::Result() const
{
	return PE_Result;
}

template <typename T> T* PostEvent::GetResult() const
{
	if (PE_Result)
	{
		return reinterpret_cast<T*>(PE_Result);
	}

	return nullptr;
}

PostEvent PostEvent::operator=(const PostEvent& other)
{
	PE_Caller = other.PE_Caller;
	PE_Function = other.PE_Function;
	PE_Params = other.PE_Params;
	PE_Detour = other.PE_Detour;
	PE_Result = other.PE_Result;
	return *this;
}

namespace Hooks
{
	void ExecuteFunctions(PreEvent& event)
	{
		(void)event;
		for (std::function<void()> Func : MainComponent::GameFunctions) {
			if (Func) Func();
		}
		MainComponent::GameFunctions.clear();
	}

	static ACar_TA* g_LocalCarCached = nullptr;

	static __declspec(noinline) bool IsDeadGe(AGameEvent_TA* ge)
	{
		__try { return (!ge) || ge->bDeleteMe; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return true; }
	}

	static __declspec(noinline) UGameShare_TA* TryGetGameShare()
	{
		__try {
			auto* wi = Instances.IAWorldInfo();
			if (wi && wi->GameShare) return reinterpret_cast<UGameShare_TA*>(wi->GameShare);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}

	static __declspec(noinline) AGameEvent_TA* TryGetGameEventPc(APlayerController* pc)
	{
		__try {
			if (pc && pc->IsA(APlayerController_TA::StaticClass()))
				return static_cast<APlayerController_TA*>(pc)->GetGameEvent();
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}

	static __declspec(noinline) AGameEvent_TA* TryGetGameEventCar(ACar_TA* car)
	{
		__try {
			if (car && !car->bDeleteMe && car->PRI) {
				auto* pri = reinterpret_cast<APRI_TA*>(car->PRI);
				if (!pri) return nullptr;
				return pri->ReplicatedGameEvent ? pri->ReplicatedGameEvent : pri->GameEvent;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
		return nullptr;
	}

	static __forceinline float Clamp100(float x) { return x < 0.0f ? 0.0f : (x > 100.0f ? 100.0f : x); }

	static __forceinline bool FillCar(ACar_TA* c, JukeCarSnap& o)
	{
		__try {
			if (!c || c->bDeleteMe) return false;
			const FVector p = c->Location;
			const FVector v = c->Velocity;
			const FVector av = c->AngularVelocity;
			const FRotator r = c->Rotation;
			o.Px = p.X; o.Py = p.Y; o.Pz = p.Z;
			o.Vx = v.X; o.Vy = v.Y; o.Vz = v.Z;
			o.AVx = av.X; o.AVy = av.Y; o.AVz = av.Z;
			o.Pitch = (int32_t)r.Pitch; o.Yaw = (int32_t)r.Yaw; o.Roll = (int32_t)r.Roll;

			float boost = 0.f;
			ACarComponent_Boost_TA* bc = c->BoostComponent;
			if (bc) boost = bc->CurrentBoostAmount;
			o.Boost = Clamp100(boost);

			uint8_t team = 0;
			APlayerReplicationInfo* pri = (APlayerReplicationInfo*)c->PRI;
			if (pri && pri->Team) team = (uint8_t)(pri->Team->TeamIndex == 1 ? 1 : 0);
			o.Team = team;

			uint32_t flags = *(uint32_t*)((uintptr_t)c + 0x07D0);
			bool bOnGround = ((flags >> 4) & 1) != 0;
			o.OnGround = (uint8_t)(bOnGround ? 1 : 0);
			o.Jumped = (uint8_t)(c->bJumped != 0);
			o.DoubleJumped = (uint8_t)(c->bDoubleJumped != 0);
			o.Demoed = (uint8_t)(c->IsDemolished() ? 1 : 0);

			o.Pad[0] = o.Pad[1] = o.Pad[2] = 0;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static __forceinline bool FillBall(ABall_TA* b, JukeBallSnap& o)
	{
		__try {
			if (!b || b->bDeleteMe) return false;
			const FVector p = b->Location;
			const FVector v = b->Velocity;
			const FVector av = b->AngularVelocity;
			const FRotator r = b->Rotation;
			o.Px = p.X; o.Py = p.Y; o.Pz = p.Z;
			o.Vx = v.X; o.Vy = v.Y; o.Vz = v.Z;
			o.AVx = av.X; o.AVy = av.Y; o.AVz = av.Z;
			o.Pitch = (int32_t)r.Pitch; o.Yaw = (int32_t)r.Yaw; o.Roll = (int32_t)r.Roll;
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	static __declspec(noinline) void SnapWrite(AGameEvent_TA* ge, ACar_TA* localCar)
	{
		JukeSnap s{};
		s.Count = 0;
		s.Local = -1;

		__try {
			if (ge && !ge->bDeleteMe) {
				const int nc = ge->Cars.size();
				for (int i = 0; i < nc && s.Count < 64; ++i) {
					ACar_TA* c = ge->Cars[i];
					if (!FillCar(c, s.Cars[s.Count])) continue;
					if (c == localCar) s.Local = s.Count;
					++s.Count;
				}

				if (s.Count == 0) {
					const int np = ge->Players.size();
					for (int i = 0; i < np && s.Count < 64; ++i) {
						AController* ctrl = ge->Players[i];
						if (!ctrl || ctrl->bDeleteMe) continue;
						APawn* pawn = ctrl->Pawn;
						if (!pawn || pawn->bDeleteMe) continue;
						if (!pawn->IsA(ACar_TA::StaticClass())) continue;
						ACar_TA* c = (ACar_TA*)pawn;
						if (!FillCar(c, s.Cars[s.Count])) continue;
						if (c == localCar) s.Local = s.Count;
						++s.Count;
					}
				}

				if (s.Local < 0 && localCar && s.Count < 64) {
					if (FillCar(localCar, s.Cars[s.Count])) {
						s.Local = s.Count;
						++s.Count;
					}
				}

				if (ge->IsA(AGameEvent_Soccar_TA::StaticClass())) {
					auto* sg = (AGameEvent_Soccar_TA*)ge;
					const int bn = sg->GameBalls.size();
					if (bn > 0) FillBall(sg->GameBalls[0], s.Ball);
				}
			} else if (localCar) {
				if (FillCar(localCar, s.Cars[0])) { s.Count = 1; s.Local = 0; }
				FillBall(Instances.GetInstanceOf<ABall_TA>(), s.Ball);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			s.Count = 0;
			s.Local = -1;
		}

		if (s.Count == 0) {
			__try{
				if(auto* wi = Instances.IAWorldInfo()){
					for(APawn* p = wi->PawnList; p; p = p->NextPawn){
						if(!p->bDeleteMe && p->IsA(ACar_TA::StaticClass())){
							ACar_TA* c = (ACar_TA*)p;
							if(!FillCar(c, s.Cars[s.Count])) continue;
							if(c == localCar) s.Local = s.Count;
							++s.Count;
							if(s.Count >= 64) break;
						}
					}
				}
			}__except(EXCEPTION_EXECUTE_HANDLER){}
		}

		g_JukeSnap.Seq.fetch_add(1, std::memory_order_acq_rel);
		g_JukeSnap.Count = s.Count;
		g_JukeSnap.Local = s.Local;
		if (s.Count > 0) memcpy(g_JukeSnap.Cars, s.Cars, (size_t)s.Count * sizeof(JukeCarSnap));
		g_JukeSnap.Ball = s.Ball;
		g_JukeSnap.Seq.fetch_add(1, std::memory_order_release);
	}

	void PlayerControllerTick(PreEvent& event)
	{

		auto curGe = reinterpret_cast<AGameEvent_TA*>(g_GameEventAddr.load(std::memory_order_relaxed));
		if (IsDeadGe(curGe)) g_GameEventAddr.store(0, std::memory_order_release);

		if (!event.Caller()) return;

		auto* lp = Instances.IULocalPlayer();
		APlayerController* localPC = (lp && lp->Actor) ? lp->Actor : nullptr;

		auto* tickPC = event.GetCaller<APlayerController>();
		if (localPC) {
			if (tickPC == localPC && Instances.IAPlayerController() != localPC) Instances.SetPlayerController(localPC);
		} else {
			if (!Instances.IAPlayerController() && tickPC) Instances.SetPlayerController(tickPC);
		}

		APlayerController* pcUse = localPC ? localPC : tickPC;
		ACar_TA* car = (pcUse && pcUse->Pawn) ? reinterpret_cast<ACar_TA*>(pcUse->Pawn) : nullptr;
		if (car) g_LocalCarCached = car;

		if (UGameShare_TA* gs = TryGetGameShare())
			g_GameShareAddr.store(reinterpret_cast<uintptr_t>(gs), std::memory_order_release);

		AGameEvent_TA* ge = TryGetGameEventPc(pcUse);
		if (!ge) ge = TryGetGameEventCar(car);
		if (!ge) {
			__try{
				if (auto* wi = Instances.IAWorldInfo()) {
					if (auto* gi = wi->Game) {
						if (gi->IsA(AGameInfo_TA::StaticClass())) {
							auto* gita = reinterpret_cast<AGameInfo_TA*>(gi);
							ge = gita->CurrentGame;
							if (ge && ge->bDeleteMe) ge = nullptr;
						}
					}
				}
			}__except(EXCEPTION_EXECUTE_HANDLER){ ge=nullptr; }
		}
		if (!IsDeadGe(ge)) {
			g_GameEventAddr.store(reinterpret_cast<uintptr_t>(ge), std::memory_order_release);
			SnapWrite(ge, car);
		} else {
			SnapWrite(nullptr, car);
		}
	}

	static bool IsLocalCar(ACar_TA* car){
		if (!car) return false;
		if (g_LocalCarCached && car == g_LocalCarCached) return true;
		APlayerController* pc = nullptr;
		if (auto* lp = Instances.IULocalPlayer()) pc = lp->Actor;
		if (!pc) pc = Instances.IAPlayerController();
		if (!pc || !pc->Pawn) return false;
		return car == reinterpret_cast<ACar_TA*>(pc->Pawn);
	}

	void BotSetVehicleInput(PreEvent& event)
	{
		ACar_TA* car = event.GetCaller<ACar_TA>();
		if (IsLocalCar(car)) {
			AGameEvent_TA* ge = TryGetGameEventCar(car);
			SnapWrite(IsDeadGe(ge) ? nullptr : ge, car);
		}
		if (!g_BotEnabled || !g_ModelOk.load()) return;
		if (!IsLocalCar(car)) return;
		auto* p = event.GetParams<ACar_TA_eventSetVehicleInput_Params>();
		if (!p) return;
		FVehicleInputs in{};
		{ std::lock_guard<std::mutex> lk(g_InputMx); in = g_LatestInput; }
		p->NewInput = in;
	}

	void BotSetVehicleInputVehicle(PreEvent& event)
	{
		ACar_TA* car = event.GetCaller<ACar_TA>();
		if (IsLocalCar(car)) {
			AGameEvent_TA* ge = TryGetGameEventCar(car);
			SnapWrite(IsDeadGe(ge) ? nullptr : ge, car);
		}
		if (!g_BotEnabled || !g_ModelOk.load()) return;
		if (!IsLocalCar(car)) return;
		auto* p = event.GetParams<AVehicle_TA_eventSetVehicleInput_Params>();
		if (!p) return;
		FVehicleInputs in{};
		{ std::lock_guard<std::mutex> lk(g_InputMx); in = g_LatestInput; }
		p->NewInput = in;
	}

	void BotProcessMove(PreEvent& event)
	{

		if (!g_BotEnabled) return;
		try {
			APlayerController* callerPC = event.GetCaller<APlayerController>();
			if (callerPC && Instances.IAPlayerController() != callerPC)
				Instances.SetPlayerController(callerPC);
		} catch (...) {}
	}
	void BotProcessMovePost(const PostEvent& event)
	{

		if (!g_BotEnabled || !g_ModelOk.load()) return;
		auto* p = event.GetParams<APlayerController_TA_execProcessMove_TA_Params>();
		if (!p) return;
		FVehicleInputs in{};
		{ std::lock_guard<std::mutex> lk(g_InputMx); in = g_LatestInput; }
		p->NewInput = in;
		static int n = 0; if ((n++ % 120) == 0) Juke_LogW(L"pm_override");
	}

	static void MaybeToggleFromKeyName(const std::string& keyNameRaw);

	void GameViewPortKeyPress(const PostEvent& event)
	{
		if (!event.Params()) return;
		auto* p = event.GetParams<UGameViewportClient_TA_execHandleKeyPress_Params>();
		if (!p) return;

		if (p->EventType == static_cast<uint8_t>(EInputEvent::IE_Released))
			MaybeToggleFromKeyName(p->Key.ToString());
	}

	static void MaybeToggleFromKeyName(const std::string& keyNameRaw)
	{
		auto up = [](std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; };
		std::string a = up(keyNameRaw);
		if (a == "F1") GUI.IsOpen = !GUI.IsOpen;
	}

	void ViewportHandleInputKeyPost(const PostEvent& event)
	{
		if (!event.Params()) return;
		auto* p = event.GetParams<UGameViewportClient_execHandleInputKey_Params>();
		if (!p) return;

		if (p->EventType == static_cast<uint8_t>(EInputEvent::IE_Released))
		{
			MaybeToggleFromKeyName(p->Key.ToString());
		}
	}

	void InteractionOnReceivedNativeInputKeyPre(PreEvent& event)
	{
		if (!event.Params()) return;
		auto* p = event.GetParams<UInteraction_execOnReceivedNativeInputKey_Params>();
		if (!p) return;
		if (p->EventType == static_cast<uint8_t>(EInputEvent::IE_Released))
		{
			MaybeToggleFromKeyName(p->Key.ToString());
		}
	}

	void GFxDataMainMenuAdded(PreEvent& event)
	{
		GameState.MainMenuAdded();
	   }
}
namespace Hooks
{

	void PlayerInputTick(PreEvent& event)
	{
		(void)event;
	}
}
EventsComponent::EventsComponent() : Component("Events", "Manages function hooks and process event.") { OnCreate(); }
EventsComponent::~EventsComponent() { OnDestroy(); }
void EventsComponent::OnCreate()
{
	Detoured = false;
	ProcessEvent = nullptr;
}

void EventsComponent::OnDestroy()
{
	DetachDetour();
}

bool EventsComponent::IsDetoured()
{
	return (Detoured && ProcessEvent);
}

void EventsComponent::AttachDetour(const ProcessEventType& detourFunction)
{
	if (IsDetoured() || !detourFunction) return;

	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(reinterpret_cast<LPCVOID>(detourFunction), &mbi, sizeof(mbi))) return;
	const DWORD p = mbi.Protect & 0xFF;
	const bool exec = (p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
	if (!exec) return;

	ProcessEvent = detourFunction;
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach(&reinterpret_cast<PVOID&>(ProcessEvent), reinterpret_cast<PVOID>(ProcessEventDetour));
	const LONG st = DetourTransactionCommit();
	if (st != NO_ERROR) {
		Detoured = false;
		ProcessEvent = nullptr;
		return;
	}
	Detoured = true;
}

void EventsComponent::DetachDetour()
{
	if (!IsDetoured()) return;
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourDetach(&reinterpret_cast<PVOID&>(ProcessEvent), reinterpret_cast<PVOID>(ProcessEventDetour));
	DetourTransactionCommit();
	Detoured = false;
	ProcessEvent = nullptr;
}

void EventsComponent::ProcessEventDetour(class UObject* caller, class UFunction* function, void* params, void* result)
{
	if (!ProcessEvent || !function) return;

	PreEvent event(caller, function, params);
	auto preIt = PreHookedEvents.find(function->ObjectInternalInteger);
	if (preIt != PreHookedEvents.end())
	{
		for (const auto& preEvent : preIt->second)
		{
			try { preEvent(event); } catch (...) { }
		}
	}

	if (event.ShouldDetour())
	{
		ProcessEvent(caller, function, params, result);
	}

	auto postIt = PostHookedEvents.find(function->ObjectInternalInteger);
	if (postIt != PostHookedEvents.end())
	{
		for (const auto& postEvent : postIt->second)
		{
			try { postEvent(PostEvent(caller, function, params, result)); } catch (...) { }
		}
	}
}

bool EventsComponent::IsEventBlacklisted(uint32_t functionInteger)
{
	return (std::find(BlacklistedEvents.begin(), BlacklistedEvents.end(), functionInteger) != BlacklistedEvents.end());
}

void EventsComponent::BlacklistEvent(const std::string& function)
{
	UFunction* foundFunction = Instances.FindStaticFunction(function);

	if (foundFunction)
	{
		if (!IsEventBlacklisted(foundFunction->ObjectInternalInteger))
		{
			BlacklistedEvents.emplace_back(foundFunction->ObjectInternalInteger);
			std::sort(BlacklistedEvents.begin(), BlacklistedEvents.end());
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to blacklist function \"" + function + "\"!");
	}
}

void EventsComponent::WhitelistEvent(const std::string& functionName)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		std::vector<uint32_t>::iterator blackIt = std::find(BlacklistedEvents.begin(), BlacklistedEvents.end(), foundFunction->ObjectInternalInteger);

		if (blackIt != BlacklistedEvents.end())
		{
			BlacklistedEvents.erase(blackIt);
			std::sort(BlacklistedEvents.begin(), BlacklistedEvents.end());
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to whitelist function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPre(const std::string& functionName, std::function<void(PreEvent&)> preHook)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		HookEventPre(foundFunction->ObjectInternalInteger, preHook);
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPre(uint32_t functionIndex, std::function<void(PreEvent&)> preHook)
{
	auto* objs = UObject::GObjObjects();
	UObject* foundFunction = nullptr;
	if (objs && functionIndex < (uint32_t)objs->size())
		foundFunction = (*objs)[functionIndex];

	if (foundFunction && foundFunction->IsA(UFunction::StaticClass()))
	{
		if (PreHookedEvents.find(functionIndex) != PreHookedEvents.end())
		{
			PreHookedEvents[functionIndex].push_back(preHook);
		}
		else
		{
			PreHookedEvents[functionIndex] = std::vector<std::function<void(PreEvent&)>>{ preHook };
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function at index \"" + std::to_string(functionIndex) + "\"!");
	}
}

void EventsComponent::HookEventPost(const std::string& functionName, std::function<void(const PostEvent&)> postHook)
{
	UFunction* foundFunction = Instances.FindStaticFunction(functionName);

	if (foundFunction)
	{
		HookEventPost(foundFunction->ObjectInternalInteger, postHook);
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function \"" + functionName + "\"!");
	}
}

void EventsComponent::HookEventPost(uint32_t functionIndex, std::function<void(const PostEvent&)> postHook)
{
	auto* objs = UObject::GObjObjects();
	UObject* foundFunction = nullptr;
	if (objs && functionIndex < (uint32_t)objs->size())
		foundFunction = (*objs)[functionIndex];

	if (foundFunction && foundFunction->IsA(UFunction::StaticClass()))
	{
		if (PostHookedEvents.find(functionIndex) != PostHookedEvents.end())
		{
			PostHookedEvents[functionIndex].push_back(postHook);
		}
		else
		{
			PostHookedEvents[functionIndex] = std::vector<std::function<void(const PostEvent&)>>{ postHook };
		}
	}
	else
	{
		Console.Warning(GetNameFormatted() + "Warning: Failed to hook function at index \"" + std::to_string(functionIndex) + "\"!");
	}
}

void EventsComponent::Initialize()
{

	HookEventPre("Function Engine.PlayerController.PlayerTick", &Hooks::PlayerControllerTick);
	HookEventPre("Function TAGame.PlayerController_TA.PlayerTick", &Hooks::PlayerControllerTick);
}

class EventsComponent Events{};
