#include "pch.hpp"
#include "Components/Includes.hpp"
#include "Bot/Bot.hpp"
#include "Bot/JukeSnap.hpp"
#define OUTWRAPPER_IMPL
#include "OutWrapper.h"
#include "../DLL/IPC.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <exception>
#include <mmsystem.h>
#include <process.h> //FIX GAY 
#pragma comment(lib, "winmm.lib")

#include <RLGymCPP/ObsBuilders/AdvancedObs.h>
#include "../ObsBuilders/AdvancedObs.h"
#include "AdvancedObsPadderGGL.h"
#include "CustomObs.h"
#include "LookupAction.h"
#include "TorchPolicy.h"
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/Gamestates/Player.h>
#include <RLGymCPP/BasicTypes/Lists.h>

#ifdef _MSC_VER
#pragma comment(lib, "Delayimp.lib")
#pragma comment(linker, "/DELAYLOAD:GigaLearnCPP.dll")
#endif

std::string g_ToggleKey="R";
std::atomic<bool> g_BotEnabled(false);
FVehicleInputs g_LatestInput{};
std::mutex g_InputMx;
JukeConfig g_JukeConfig;
std::atomic<uintptr_t> g_InputAddr{0};
std::atomic<uintptr_t> g_GameEventAddr{0};
std::atomic<uintptr_t> g_GameShareAddr{0};
JukeSnap g_JukeSnap{};
std::atomic<bool> g_WriterRun{true};
std::atomic<bool> g_LoopRun{true};
std::atomic<bool> g_ModelOk(false);
static std::atomic<uint32_t> g_MatchGeneration{0};

namespace JukeBot {

#pragma pack(push, 1)
struct InputPacket {
	float Throttle;
	float Steer;
	float Pitch;
	float Yaw;
	float Roll;
	float DodgeForward;
	float DodgeRight;
	uint32_t Flags;
};
#pragma pack(pop)

static __declspec(noinline) void SafeMemCpy(void* dst, const void* src, size_t size) {
	__try {
		if (dst) {
			memcpy(dst, src, size);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_InputAddr = 0;
	}
}

static unsigned __stdcall InputWriterThread(void*){
	while(g_WriterRun){
		if(g_BotEnabled && g_ModelOk.load() && g_InputAddr!=0){
			InputPacket pkt{};
			{
				std::lock_guard<std::mutex> lk(g_InputMx);
				pkt.Throttle = g_LatestInput.Throttle;
				pkt.Steer = g_LatestInput.Steer;
				pkt.Pitch = g_LatestInput.Pitch;
				pkt.Yaw = g_LatestInput.Yaw;
				pkt.Roll = g_LatestInput.Roll;
				pkt.DodgeForward = -g_LatestInput.Pitch;
				pkt.DodgeRight = g_LatestInput.Yaw;

				uint32_t f=0;
				if(g_LatestInput.bHandbrake) f|=1;
				if(g_LatestInput.bJump) f|=(1<<1);
				if(g_LatestInput.bActivateBoost) f|=(1<<2);
				if(g_LatestInput.bHoldingBoost) f|=(1<<3);
				pkt.Flags=f;
			}

			void* dest=(void*)g_InputAddr.load();
			if(dest) SafeMemCpy(dest,&pkt,sizeof(pkt));
			Sleep(0);
		}else{
			Sleep(10);
		}
	}
	return 0;
}

static std::atomic<bool> g_WriterStarted{false};
static std::atomic<bool> g_LoopStarted{false};
static std::atomic<int> g_ToggleVk{0};
static std::atomic<unsigned long long> g_LastToggleMs{0};

static unsigned long long now_ms(){ return (unsigned long long)GetTickCount64(); }

static int vk_from_name(std::string s){
	auto trim=[](std::string& t){
		size_t a=0; while(a<t.size() && (t[a]==' '||t[a]=='\t'||t[a]=='\r'||t[a]=='\n')) a++;
		size_t b=t.size(); while(b>a && (t[b-1]==' '||t[b-1]=='\t'||t[b-1]=='\r'||t[b-1]=='\n')) b--;
		t=t.substr(a,b-a);
	};
	trim(s);
	for(char& c: s) c=(char)toupper((unsigned char)c);
	if(s.size()==1){
		char c=s[0];
		if((c>='A'&&c<='Z')||(c>='0'&&c<='9')) return (int)(unsigned char)c;
	}
	if(s.size()>=2 && s[0]=='F'){
		int n=0;
		try{ n=std::stoi(s.substr(1)); }catch(...){ n=0; }
		if(n>=1 && n<=24) return 0x70+(n-1);
	}
	if(s=="SPACE") return 0x20;
	if(s=="TAB") return 0x09;
	if(s=="SHIFT") return 0x10;
	if(s=="CTRL"||s=="CONTROL") return 0x11;
	if(s=="ALT") return 0x12;
	if(s=="INSERT") return VK_INSERT;
	return 0;
}
//vectorkey is shit
static void ZeroInputsOnce(){
	void* dest=(void*)g_InputAddr.load();
	if(!dest) return;
	InputPacket z{};
	SafeMemCpy(dest,&z,sizeof(z));
}

void ApplyConfig(const JukeConfig& cfg){
	g_JukeConfig = cfg;
	g_ToggleKey = g_JukeConfig.ToggleKey;
	int vk = vk_from_name(g_ToggleKey);
	g_ToggleVk = vk;

	if(g_JukeConfig.TickSkip < 1) g_JukeConfig.TickSkip = 1;
	if(g_JukeConfig.TickSkip > 16) g_JukeConfig.TickSkip = 16;

	char buf[512];
	sprintf_s(buf,"cfg: toggle=%s vk=%d tickskip=%d botdir=%ls", g_ToggleKey.c_str(), vk, g_JukeConfig.TickSkip, g_JukeConfig.BotPath.c_str());
	jukeOut(buf);
}

static unsigned __stdcall BotLoopThread(void*){
	using clk = std::chrono::steady_clock;
	const auto interval = std::chrono::duration<double>(1.0/120.0);
	auto next = clk::now() + interval;
	auto last = clk::now();

	while(g_LoopRun){
		int vk = g_ToggleVk.load();
		if(vk){
			SHORT st = GetAsyncKeyState(vk);
			if(st & 1){
				unsigned long long now = now_ms();
				unsigned long long prev = g_LastToggleMs.load();
				if(now - prev > 250){
					g_LastToggleMs = now;

					const bool cur = g_BotEnabled.load();
					if(!cur){
						if(!g_ModelOk.load()){
							jukeOut("bot enable blocked: model not loaded (press 'load bot')");
						}else{
							g_BotEnabled = true;
							jukeOut("bot enabled");
						}
					}else{
						g_BotEnabled = false;
						jukeOut("bot disabled");
						{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput = FVehicleInputs{}; }
						ZeroInputsOnce();
					}
				}
			}
		}

		auto t = clk::now();
		double dt = std::chrono::duration<double>(t - last).count();
		last = t;

		if(g_BotEnabled){
			try{ Inference_OnTick((float)dt); }catch(...){
				g_BotEnabled = false;
				{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput = FVehicleInputs{}; }
				ZeroInputsOnce();
				jukeOut("bot disabled: runtime error");
			}
		}

		auto now = clk::now();
		if(now < next) std::this_thread::sleep_until(next);
		else next = now;
		next += interval;
	}
	return 0;
}

static std::wstring exedir(){
	wchar_t b[MAX_PATH]{0};
	GetModuleFileNameW(nullptr,b,MAX_PATH);
	std::wstring s(b);
	auto p=s.find_last_of(L"\\/");
	if(p!=std::wstring::npos) s.erase(p);
	return s;
}
std::wstring ExeDir(){ return exedir(); }

static void dlog(const std::wstring& s){
	std::wstring d=ExeDir()+L"\\log.txt";
	CreateDirectoryW(d.c_str(),NULL);
	std::wstring f=d+L"\\dll_log.txt";
	FILE* fp=nullptr;
	_wfopen_s(&fp,f.c_str(),L"a+, ccs=UTF-8");
	if(!fp) return;
	SYSTEMTIME st; GetLocalTime(&st);
	wchar_t t[64];
	swprintf(t,64,L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
	fputws(t,fp); fputws(s.c_str(),fp); fputws(L"\n",fp);
	fclose(fp);
}
static void dloga(const char* s){ std::wstring w; while(*s) w.push_back((wchar_t)(unsigned char)*s++); dlog(w); }

extern "C" void Juke_LogW(const wchar_t* s){ if(s) dlog(s); }
extern "C" void Juke_LogA(const char* s){ if(s) dloga(s); }

static LONG CALLBACK Juke_SEH(PEXCEPTION_POINTERS p){
	auto ec=(unsigned long long)p->ExceptionRecord->ExceptionCode;
	if(ec==0x40010006ULL||ec==0x4001000AULL) return EXCEPTION_CONTINUE_SEARCH;
	static int n=0; if((n++%60)==0) dlog(L"seh_code "+std::to_wstring(ec));
	return EXCEPTION_CONTINUE_SEARCH;
}

void SetupDllSearch(){
	HMODULE k=GetModuleHandleW(L"kernel32.dll");
	wchar_t tmp[MAX_PATH]{0};
	GetTempPathW(MAX_PATH,tmp);
	if(!k) return;
	using PFN_SetDefaultDllDirectories=BOOL (WINAPI*)(DWORD);
	using PFN_AddDllDirectory=DLL_DIRECTORY_COOKIE (WINAPI*)(PCWSTR);
	auto pSet=(PFN_SetDefaultDllDirectories)GetProcAddress(k,"SetDefaultDllDirectories");
	auto pAdd=(PFN_AddDllDirectory)GetProcAddress(k,"AddDllDirectory");
	if(pSet) pSet(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|LOAD_LIBRARY_SEARCH_USER_DIRS);
	if(pAdd) pAdd(tmp);
}

std::wstring BootstrapPath(){
	wchar_t tmp[MAX_PATH]{0};
	GetTempPathW(MAX_PATH,tmp);
	return std::wstring(tmp)+L"juke_bootstrap_"+std::to_wstring((unsigned long long)GetCurrentProcessId())+L".json";
}

static bool rf8(const std::wstring& p,std::string& out){
	std::ifstream f(std::filesystem::path(p),std::ios::binary);
	if(!f) return false;
	std::ostringstream ss; ss<<f.rdbuf();
	out=ss.str();
	return true;
}
static void trimws(std::string& s){
	s.erase(std::remove_if(s.begin(),s.end(),[](unsigned char c){return c=='\r'||c=='\n'||c=='\t';}),s.end());
}
static bool ext_int(const std::string& s,size_t& i,int& v){
	while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
	bool neg=false; if(i<s.size()&&s[i]=='-'){neg=true;++i;}
	if(i>=s.size()||!isdigit((unsigned char)s[i])) return false;
	long val=0;
	while(i<s.size()&&isdigit((unsigned char)s[i])){val=val*10+(s[i]-'0');++i;}
	v=neg?-(int)val:(int)val;
	return true;
}
static bool ext_arr_int(const std::string& s,size_t& i,std::vector<int>& out){
	while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
	if(i>=s.size()) return false;
	char o=s[i],c=0;
	if(o=='[') c=']';
	else if(o=='{') c='}';
	else return false;
	++i; out.clear();
	while(i<s.size()){
		while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
		if(i<s.size()&&s[i]==c){++i;break;}
		int v=0; if(!ext_int(s,i,v)) return false;
		out.push_back(v);
		while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
		if(i<s.size()&&s[i]==','){++i;continue;}
		if(i<s.size()&&s[i]==c){++i;break;}
	}
	return true;
}
static bool ext_str(const std::string& s,size_t& i,std::string& out){
	while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
	if(i>=s.size()||s[i]!='"') return false;
	++i; std::string t;
	while(i<s.size()&&s[i]!='"'){t.push_back(s[i]);++i;}
	if(i>=s.size()||s[i]!='"') return false;
	++i; out.swap(t); return true;
}
static bool ext_bool(const std::string& s,size_t& i,bool& b){
	while(i<s.size()&&isspace((unsigned char)s[i])) ++i;
	if(s.compare(i,4,"true")==0){b=true;i+=4;return true;}
	if(s.compare(i,5,"false")==0){b=false;i+=5;return true;}
	if(i<s.size()&&s[i]=='"'){
		size_t j=i; std::string t;
		if(!ext_str(s,j,t)) return false;
		if(t=="true"){b=true;i=j;return true;}
		if(t=="false"){b=false;i=j;return true;}
	}
	return false;
}
static bool keypos(const std::string& s,const char* key,size_t& pos){
	std::string k=std::string("\"")+key+"\"";
	size_t p=s.find(k);
	if(p==std::string::npos) return false;
	p=s.find(':',p+k.size());
	if(p==std::string::npos) return false;
	pos=p+1; return true;
}

bool LoadBootstrapConfig(JukeConfig& cfg){
	std::string raw;
	if(!rf8(BootstrapPath(),raw)) return false;
	trimws(raw);
	size_t pos=0;

	if(!keypos(raw,"ToggleKey",pos)) return false;
	{ std::string tk; size_t i=pos; if(!ext_str(raw,i,tk)) return false; cfg.ToggleKey=tk; }

	if(keypos(raw,"BotPath",pos)){
		std::string bp; size_t i=pos;
		if(ext_str(raw,i,bp)) cfg.BotPath=std::wstring(bp.begin(),bp.end());
	}

	if(!keypos(raw,"BotType",pos)) return false;
	{ std::string bt; size_t i=pos; if(!ext_str(raw,i,bt)) return false; cfg.BotType=bt; if(cfg.BotType!="GGL_PPO") return false; }

	if(!keypos(raw,"USE_SHARED_HEAD",pos)) return false;
	{ bool ush=false; size_t i=pos; if(!ext_bool(raw,i,ush)) return false; cfg.UseSharedHead=ush; }

	if(keypos(raw,"POLICY_SIZE",pos)){
		size_t i=pos; std::vector<int> a; if(!ext_arr_int(raw,i,a)) return false; cfg.PolicySize=a;
	}
	if(keypos(raw,"LAYER_NORM",pos)){
		bool ln=false; size_t i=pos; if(ext_bool(raw,i,ln)) cfg.LayerNorm=ln;
	}
	if(keypos(raw,"ACTIVATION",pos)){
		std::string act; size_t i=pos; if(ext_str(raw,i,act)) cfg.Activation=act;
	}

	if(cfg.UseSharedHead){
		if(!keypos(raw,"SHARED_HEAD_SIZE",pos)) return false;
		size_t i=pos; std::vector<int> a; if(!ext_arr_int(raw,i,a)) return false; cfg.SharedHeadSize=a;
	}else{
		if(keypos(raw,"CRITIC_SIZE",pos)){
			size_t i=pos; std::vector<int> a; if(!ext_arr_int(raw,i,a)) return false; cfg.CriticSize=a;
		}
	}

	if(!keypos(raw,"obsSize",pos)) return false;
	{ int os=0; size_t i=pos; if(!ext_int(raw,i,os)) return false; cfg.ObsSize=os; }

	if(!keypos(raw,"tickSkip",pos)) return false;
	{ int ts=0; size_t i=pos; if(!ext_int(raw,i,ts)) return false; cfg.TickSkip=ts; }

	if(!keypos(raw,"actionDelay",pos)) return false;
	{ int ad=0; size_t i=pos; if(!ext_int(raw,i,ad)) return false; cfg.ActionDelay=ad; }

	if(!keypos(raw,"ObsBuilder",pos)) return false;
	{ std::string ob; size_t i=pos; if(!ext_str(raw,i,ob)) return false; cfg.ObsBuilder=ob; }

	if(cfg.ToggleKey.empty()) return false;
	if(cfg.PolicySize.empty()) cfg.PolicySize=std::vector<int>{1024};
	if(!(cfg.ObsBuilder=="AdvancedObs"||cfg.ObsBuilder=="AdvancedObsPadded")) return false;
	if(cfg.TickSkip<=0||cfg.ActionDelay<=0) return false;
	return true;
}

}

namespace {

static std::array<int,64> g_OnGroundTicks{};
static bool g_BoostMapBuilt=false;
static std::array<AVehiclePickup_Boost_TA*, RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT> g_BoostPads{};

static ABall_TA* g_ball=nullptr;

static RocketSim::Vec ToVec(const FVector& v){ return RocketSim::Vec(v.X,v.Y,v.Z); }
static RocketSim::Vec ToVec0(){ return RocketSim::Vec(0,0,0); }
static RocketSim::RotMat ToRot(const FRotator& r){
	const float S=3.14159265358979323846f/32768.0f;
	return RocketSim::Angle(r.Yaw*S, r.Pitch*S, r.Roll*S).ToRotMat();
}

static UGameShare_TA* GetGameShare(){
	auto* gs=reinterpret_cast<UGameShare_TA*>(g_GameShareAddr.load(std::memory_order_acquire));
	if(!gs) return nullptr;
	__try{
		return gs;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return nullptr;
	}
}

static AGameEvent_TA* GetGE(ACar_TA* myCar){
	(void)myCar;
	auto* ge=reinterpret_cast<AGameEvent_TA*>(g_GameEventAddr.load(std::memory_order_acquire));
	if(!ge) return nullptr;
	__try{
		if(ge->bDeleteMe) return nullptr;
		return ge;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return nullptr;
	}
}

static ABall_TA* GetBall(AGameEvent_TA* ge){
	__try{
		if(g_ball && !g_ball->bDeleteMe) return g_ball;
		if(ge && !ge->bDeleteMe && ge->IsA(AGameEvent_Soccar_TA::StaticClass())){
			auto* sg = (AGameEvent_Soccar_TA*)ge;
			const int n = sg->GameBalls.size();
			if(n>0){
				auto* b = sg->GameBalls[0];
				if(b && !b->bDeleteMe){ g_ball=b; return g_ball; }
			}
		}
		g_ball=Instances.GetInstanceOf<ABall_TA>();
		return g_ball;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return nullptr;
	}
}

static APlayerReplicationInfo* GetCarPRI(ACar_TA* c) {
	if(!c || c->bDeleteMe) return nullptr;

	auto* pri = *(APlayerReplicationInfo**)((uintptr_t)c + 0x0808);
	if(!pri || IsBadReadPtr(pri, sizeof(APlayerReplicationInfo)) || pri->bDeleteMe) return nullptr;
	return pri;
}

static bool GetTeamIndex(ACar_TA* c,int32_t& out){
	out=0;
	__try{
		if(!c||c->bDeleteMe) return false;
		auto* pri=GetCarPRI(c);
		if(!pri) {

			return false;
		}

		static int s_dbg=0;
		if(s_dbg<20) {
			char buf[256];
			sprintf_s(buf, "GetTeamIndex: pri=%p IsA(APRI_TA)=%d", pri, pri->IsA(APRI_TA::StaticClass()));
			jukeOut(buf);
		}

		if(pri->IsA(APRI_TA::StaticClass())) {
			auto* priTA = (APRI_TA*)pri;
			int32_t lti = priTA->LastTeamIndex;

			if(s_dbg<20) {
				char buf[128];
				sprintf_s(buf, "  > LastTeamIndex: %d (offset 0x7B0)", lti);
				jukeOut(buf);
			}

		}

		auto* team=pri->Team;
		if(team) {
			out=team->TeamIndex;

			if(s_dbg<20) {
				char buf[128];
				sprintf_s(buf, "  > TeamInfo: ptr=%p TeamIndex=%d (offset 0x280)", team, out);
				jukeOut(buf);
			}

			if(out >= 0 && out <= 1) {
				s_dbg++;
				return true;
			}
		} else {
			if(s_dbg<20) jukeOut("  > TeamInfo: null");
		}

		__try {
			uint8_t tm = *(uint8_t*)((uintptr_t)pri + 0x270);
			if(s_dbg<20) {
				char buf[128];
				sprintf_s(buf, "  > Byte @ 0x270: %d", (int)tm);
				jukeOut(buf);
			}
			if(tm <= 1) {
				out = (int32_t)tm;
				s_dbg++;
				return true;
			}
		} __except(EXCEPTION_EXECUTE_HANDLER) {}

		s_dbg++;
		return false;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return false;
	}
}

static bool IsOnGroundPython(ACar_TA* c) { //Fuck
	__try {
		if(!c || c->bDeleteMe) return false;

		uint32_t flags = *(uint32_t*)((uintptr_t)c + 0x07D0);
		return ((flags >> 4) & 1) != 0;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool _IsJumped(ACar_TA* c) {
	__try {
		if(!c || c->bDeleteMe) return false;

		uint32_t flags = *(uint32_t*)((uintptr_t)c + 0x07D0);
		return ((flags >> 2) & 1) != 0;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool _IsDoubleJumped(ACar_TA* c) {
	__try {
		if(!c || c->bDeleteMe) return false;

		uint32_t flags = *(uint32_t*)((uintptr_t)c + 0x07D0);
		return ((flags >> 3) & 1) != 0;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool IsDemo(ACar_TA* c){
	__try{
		if(!c||c->bDeleteMe) return false;
		return c->IsDemolished();
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return false;
	}
}
static float GetBoost(ACar_TA* c){
	__try{
		if(!c||c->bDeleteMe) return 0.f;
		auto* bc=c->BoostComponent;
		if(!bc) return 0.f;
		float b=bc->CurrentBoostAmount;
		if(b<0.f) b=0.f; if(b>100.f) b=100.f;
		return b;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		return 0.f;
	}
}

static void BuildBoostPadMapImpl(){
	const int N=RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT;
	for(int i=0;i<N;++i) g_BoostPads[i]=nullptr;

	auto* gs=GetGameShare();
	if(!gs){ g_BoostMapBuilt=true; return; }
	auto& pills=gs->ActiveBoostPills;
	const int m=pills.size();
	if(m<=0){ g_BoostMapBuilt=true; return; }

	for(int i=0;i<N;++i){
		const auto& t=RLGC::CommonValues::BOOST_LOCATIONS[i];
		FVector tp(t.x,t.y,t.z);
		AVehiclePickup_Boost_TA* best=nullptr;
		float bestd=1e30f;
		for(int j=0;j<m;++j){
			auto* pad=pills[j];
			if(!pad||pad->bDeleteMe) continue;
			FVector l=pad->Location;
			float dx=l.X-tp.X,dy=l.Y-tp.Y,dz=l.Z-tp.Z;
			float d2=dx*dx+dy*dy+dz*dz;
			if(d2<bestd){bestd=d2;best=pad;}
		}
		g_BoostPads[i]=best;
	}
	g_BoostMapBuilt=true;
}

static void BuildBoostPadMap(){
	if(g_BoostMapBuilt) return;
	__try{
		BuildBoostPadMapImpl();
	}__except(EXCEPTION_EXECUTE_HANDLER){
		g_BoostMapBuilt=true;
	}
}

static void UpdateBoostPads(RLGC::GameState& gs){
	const int N=RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT;
	if((int)gs.boostPads.size()!=N){
		gs.boostPads.assign(N,true);
		gs.boostPadsInv.assign(N,true);
		gs.boostPadTimers.assign(N,0.f);
		gs.boostPadTimersInv.assign(N,0.f);
	}

	BuildBoostPadMap();

	for(int i=0;i<N;++i){
		bool active=true;
		__try{
			auto* pad=g_BoostPads[i];
			if(pad && !pad->bDeleteMe){

				active = !pad->IsPickedUp();
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			active=true;
		}
		gs.boostPads[i]=active;
		gs.boostPadsInv[i]=active;
		gs.boostPadTimers[i]=0.f;
		gs.boostPadTimersInv[i]=0.f;
	}

	for(int i=0;i<N;++i){
		gs.boostPadsInv[i]=gs.boostPads[N-1-i];
	}
}

static __declspec(noinline) bool TryBuildFromLive(RLGC::GameState& gs,int& localIdx,int ticksElapsed, ACar_TA* myCar){
	AGameEvent_TA* ge=nullptr;
	ABall_TA* ball=nullptr;

	constexpr uintptr_t GVC_GAME_EVENT_OFFSET = 0x0300;

	__try{
		auto* gvc = Instances.GetInstanceOf<UGameViewportClient_TA>();
		if(gvc){
			ge = *(AGameEvent_TA**)((uintptr_t)gvc + GVC_GAME_EVENT_OFFSET);
			if(ge && ge->bDeleteMe) ge=nullptr;

			static int s_gvc=0;
			if(s_gvc<5){
				char buf[256];
				sprintf_s(buf, "GameEvent from ViewportClient: gvc=%p ge=%p (offset=0x%X)", gvc, ge, (unsigned)GVC_GAME_EVENT_OFFSET);
				jukeOut(buf);
				s_gvc++;
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		ge=nullptr;
	}

	if(!ge){
		__try{
			ge=reinterpret_cast<AGameEvent_TA*>(g_GameEventAddr.load(std::memory_order_acquire));
			if(ge && ge->bDeleteMe) ge=nullptr;
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
		}
	}

	if(!ge){
		__try{
			if(myCar && !myCar->bDeleteMe && myCar->PRI){
				auto* pri=(APRI_TA*)myCar->PRI;
				if(pri){
					ge = pri->ReplicatedGameEvent ? pri->ReplicatedGameEvent : pri->GameEvent;
					if(ge && ge->bDeleteMe) ge=nullptr;
				}
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
		}
	}

	if(!ge){
		__try{
			APlayerController* pc=Instances.IAPlayerController();
			if(!pc){ if(auto* lp=Instances.IULocalPlayer()) pc=lp->Actor; }
			if(pc && pc->IsA(APlayerController_TA::StaticClass())){
				ge=((APlayerController_TA*)pc)->GetGameEvent();
				if(ge && ge->bDeleteMe) ge=nullptr;
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
		}
	}

	if(!ge){
		__try{
			if(auto* wi=Instances.IAWorldInfo()){
				if(auto* gi=wi->Game){
					if(gi->IsA(AGameInfo_TA::StaticClass())){
						auto* gita=(AGameInfo_TA*)gi;
						ge=gita->CurrentGame;
						if(ge && ge->bDeleteMe) ge=nullptr;
					}
				}
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
		}
	}

	if(!ge){

		static int s_debug=0;
		if(s_debug<1){
			s_debug++;
			char buf[256];

			auto* objs=UObject::GObjObjects();
			sprintf_s(buf, "GObjects addr=%p num=%d", objs, objs?(int)objs->size():-1);
			jukeOut(buf);

			auto* sc = AGameEvent_Soccar_TA::StaticClass();
			sprintf_s(buf, "AGameEvent_Soccar_TA::StaticClass()=%p", sc);
			jukeOut(buf);
		}

		__try{
			ge = Instances.GetInstanceOf<AGameEvent_Soccar_TA>();

			if(ge && ge->bDeleteMe) ge=nullptr;
			else if(ge){

				static int s_log=0;
				if(s_log<5){
					char buf[128];
					sprintf_s(buf, "found ge via Instances: %p", ge);
					jukeOut(buf);
					s_log++;
				}
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
			static int s_err=0;
			if(s_err<1){ jukeOut("TryBuildFromLive fallback exception"); s_err++; }
		}
	}

	if(!ge){
		static int s_noGe=0;
		if(s_noGe<5){ jukeOut("TryBuildFromLive: ge still null after all attempts"); s_noGe++; }
		return false;
	}

	static uintptr_t s_lastGE = 0;
	static uint32_t s_curGen = 0;
	static int s_geFound = 0;
	static int s_ball = 0;
	static int s_cars = 0;
	static int s_np = 0;
	static int s_loop = 0;
	static int s_carmatch = 0;
	static int s_added = 0;

	const uint32_t curGen = g_MatchGeneration.load(std::memory_order_acquire);
	if(curGen != s_curGen){
		s_curGen = curGen;
		s_geFound = 0;
		s_ball = 0;
		s_cars = 0;
		s_np = 0;
		s_loop = 0;
		s_carmatch = 0;
		s_added = 0;
		char buf[128];
		sprintf_s(buf, "TryBuildFromLive: counters reset for gen %u", curGen);
		jukeOut(buf);
	}

	if((uintptr_t)ge != s_lastGE){
		char buf[256];
		sprintf_s(buf, "Game event changed: old=%p new=%p (new match detected)", (void*)s_lastGE, ge);
		jukeOut(buf);
		s_lastGE = (uintptr_t)ge;

		g_InputAddr = 0;

		g_MatchGeneration.fetch_add(1, std::memory_order_release);
	}

	if(s_geFound<5){
		char buf[128];
		sprintf_s(buf, "TryBuildFromLive: ge=%p", ge);
		jukeOut(buf);
		s_geFound++;
	}

	__try{
		if(ge->IsA(AGameEvent_Soccar_TA::StaticClass())){

			constexpr uintptr_t BALLS_OFFSET = 0x08E0;
			uintptr_t geAddr = (uintptr_t)ge;
			auto* ballsArray = (TArray<ABall_TA*>*)(geAddr + BALLS_OFFSET);

			const int bn = ballsArray->size();
			if(s_ball<5){
				char buf[128];
				sprintf_s(buf, "GameBalls.size()=%d (offset=0x%X)", bn, (unsigned)BALLS_OFFSET);
				jukeOut(buf);
				s_ball++;
			}
			if(bn>0) ball=ballsArray->at(0);
			if(ball && ball->bDeleteMe) ball=nullptr;
			if(s_ball<10 && ball){
				char buf[128];
				sprintf_s(buf, "ball=%p", ball);
				jukeOut(buf);
				s_ball++;
			}
		}
	}__except(EXCEPTION_EXECUTE_HANDLER){
		ball=nullptr;
	}

	constexpr uintptr_t CARS_OFFSET = 0x0350;
	constexpr uintptr_t PLAYERS_OFFSET = 0x0330;

	uintptr_t geAddr = (uintptr_t)ge;
	auto* carsArray = (TArray<ACar_TA*>*)(geAddr + CARS_OFFSET);

	int nCars=0;
	__try{ nCars=carsArray->size(); }__except(EXCEPTION_EXECUTE_HANDLER){ nCars=0; }

	if(s_cars<5){
		char buf[256];
		auto* sc = AGameEvent_Soccar_TA::StaticClass();
		bool isaSoccar = ge->IsA(sc);
		sprintf_s(buf, "TryBuildFromLive: nCars=%d (offset=0x%X) IsA(Soccar)=%d", nCars, (unsigned)CARS_OFFSET, isaSoccar?1:0);
		jukeOut(buf);
		s_cars++;
	}

	if(nCars<=0){
		auto* playersArray = (TArray<AController*>*)(geAddr + PLAYERS_OFFSET);
		int np=0;
		__try{ np=playersArray->size(); }__except(EXCEPTION_EXECUTE_HANDLER){ np=0; }

		if(s_np<5){
			char buf[128];
			sprintf_s(buf, "TryBuildFromLive: nCars=0, trying Players (offset=0x%X), np=%d", (unsigned)PLAYERS_OFFSET, np);
			jukeOut(buf);
			s_np++;
		}

		if(np<=0) return false;

		int idx=0;
		for(int i=0;i<np && idx<64;++i){
			AController* ctrl=nullptr;
			__try{ ctrl=playersArray->at(i); }__except(EXCEPTION_EXECUTE_HANDLER){ ctrl=nullptr; }
			if(!ctrl) continue;
			__try{ if(ctrl->bDeleteMe) continue; }__except(EXCEPTION_EXECUTE_HANDLER){ continue; }
			APawn* pawn=nullptr;
			__try{ pawn=ctrl->Pawn; }__except(EXCEPTION_EXECUTE_HANDLER){ pawn=nullptr; }
			if(!pawn) continue;
			__try{ if(pawn->bDeleteMe) continue; }__except(EXCEPTION_EXECUTE_HANDLER){ continue; }
			if(!pawn->IsA(ACar_TA::StaticClass())) continue;
			ACar_TA* c=(ACar_TA*)pawn;

			const FVector cl=c->Location;
			const FVector cv=c->Velocity;
			const FVector cav=c->AngularVelocity;
			const FRotator cr=c->Rotation;

			RLGC::Player p{};
			p.index=idx;
			p.carId=(uint32_t)idx;

			int32_t tn=0; GetTeamIndex(c,tn);
			p.team=(tn==1)?Team::ORANGE:Team::BLUE;

			p.pos=ToVec(cl);
			p.vel=ToVec(cv);
			p.angVel=ToVec(cav);
			p.rotMat=ToRot(cr);

			const bool wc=IsOnGroundPython(c);
			if(idx>=0 && idx<(int)g_OnGroundTicks.size()){
				if(wc) g_OnGroundTicks[(size_t)idx]=0;
				else{
					int add=ticksElapsed; if(add<0) add=0;
					g_OnGroundTicks[(size_t)idx]+=add;
					if(g_OnGroundTicks[(size_t)idx]>1000000) g_OnGroundTicks[(size_t)idx]=1000000;
				}
				p.isOnGround=wc || g_OnGroundTicks[(size_t)idx]<=6;
			}else p.isOnGround=wc;

			p.hasJumped=_IsJumped(c);
			p.hasDoubleJumped=_IsDoubleJumped(c);
			p.hasFlipped=p.hasDoubleJumped;
			p.airTimeSinceJump=0.f;

			p.isDemoed=IsDemo(c);
			p.boost=GetBoost(c);

			gs.players.push_back(p);

			bool isLocal = (c == myCar);
			if(!isLocal && myCar) {
				__try {
					auto* myPRI = GetCarPRI(myCar);
					auto* cPRI = GetCarPRI(c);
					if(myPRI && cPRI && myPRI == cPRI) {
						isLocal = true;
					}
				} __except(EXCEPTION_EXECUTE_HANDLER) {}
			}

			if(isLocal) localIdx=idx;
			++idx;
		}
		if(gs.players.empty()) return false;
		if(localIdx<0) localIdx=0;
	}else{
		int idx=0;
		for(int i=0;i<nCars && idx<64;++i){
			ACar_TA* c=nullptr;
			__try{ c=carsArray->at(i); }__except(EXCEPTION_EXECUTE_HANDLER){ c=nullptr; }

			if(s_loop<10){
				char buf[128];
				sprintf_s(buf, "Cars[%d] = %p", i, c);
				jukeOut(buf);
				s_loop++;
			}

			if(!c) continue;

			bool del=false;
			__try{ del=(c->bDeleteMe!=0); }__except(EXCEPTION_EXECUTE_HANDLER){ del=true; }
			if(del){
				static int s_del=0;
				if(s_del<5){ jukeOut("car is deleted, skipping"); s_del++; }
				continue;
			}

			static int s_build=0;
			if(s_build<5){ jukeOut("building player from car..."); s_build++; }

			__try{
				static int dbg=0;
				if(dbg<5) jukeOut("step1: reading Location");
				const FVector cl=c->Location;
				if(dbg<5) jukeOut("step2: reading Velocity");
				const FVector cv=c->Velocity;
				if(dbg<5) jukeOut("step3: reading AngularVelocity");
				const FVector cav=c->AngularVelocity;
				if(dbg<5) jukeOut("step4: reading Rotation");
				const FRotator cr=c->Rotation;
				if(dbg<5) jukeOut("step5: creating Player");

				RLGC::Player p{};
				p.index=idx;
				p.carId=(uint32_t)idx;

				if(dbg<5) jukeOut("step6: GetTeamIndex");
				int32_t tn=0; GetTeamIndex(c,tn);
				p.team=(tn==1)?Team::ORANGE:Team::BLUE;

				if(dbg<5) jukeOut("step7: ToVec");
				p.pos=ToVec(cl);
				p.vel=ToVec(cv);
				p.angVel=ToVec(cav);
				p.rotMat=ToRot(cr);

				if(dbg<5) jukeOut("step8: bOnGround");
				const bool wc=IsOnGroundPython(c);
				if(idx>=0 && idx<(int)g_OnGroundTicks.size()){
					if(wc) g_OnGroundTicks[(size_t)idx]=0;
					else{
						int add=ticksElapsed; if(add<0) add=0;
						g_OnGroundTicks[(size_t)idx]+=add;
						if(g_OnGroundTicks[(size_t)idx]>1000000) g_OnGroundTicks[(size_t)idx]=1000000;
					}
					p.isOnGround=wc || g_OnGroundTicks[(size_t)idx]<=6;
				}else p.isOnGround=wc;

				if(dbg<5) jukeOut("step9: bJumped"); //676767676676767
				p.hasJumped=_IsJumped(c);
				p.hasDoubleJumped=_IsDoubleJumped(c);
				p.hasFlipped=p.hasDoubleJumped;
				p.airTimeSinceJump=0.f;

				if(dbg<5) jukeOut("step10: IsDemo/GetBoost");
				p.isDemoed=IsDemo(c);
				p.boost=GetBoost(c);

				if(dbg<5) jukeOut("step11: push_back");
				gs.players.push_back(p);

				if(s_carmatch<10){
					char buf[256];
					int32_t tn2=0; GetTeamIndex(c,tn2);
					sprintf_s(buf,"car[%d] ptr=%p myCar=%p match=%d team=%d", idx, c, myCar, (c==myCar)?1:0, tn2);
					jukeOut(buf);
					s_carmatch++;
				}

				bool isLocal = (c == myCar);
				if(!isLocal && myCar) {

					__try {
						auto* myPRI = GetCarPRI(myCar);
						auto* cPRI = GetCarPRI(c);
						if(myPRI && cPRI && myPRI == cPRI) {
							isLocal = true;
							static int s_pri=0;
							if(s_pri<5){ jukeOut("local match via PRI!"); s_pri++; }
						}
					} __except(EXCEPTION_EXECUTE_HANDLER) {}
				}

				if(isLocal) localIdx=idx;
				++idx;
				dbg++;

				if(s_added<5){ jukeOut("player added!"); s_added++; }
			}__except(EXCEPTION_EXECUTE_HANDLER){
				static int s_ex=0;
				if(s_ex<5){ jukeOut("EXCEPTION building player from car"); s_ex++; }
			}
		}
		if(gs.players.empty()) return false;

		static int s_matched=0;
		if(s_matched<5){
			char buf[128];
			sprintf_s(buf,"car match result: localIdx=%d (found=%d)", localIdx, (localIdx>=0)?1:0);
			jukeOut(buf);
			s_matched++;
		}

		if(localIdx<0) localIdx=0;
	}

	RocketSim::BallState b{};
	if(ball){
		__try{
			if(!ball->bDeleteMe){
				const FVector p0=ball->Location;
				const FVector v0=ball->Velocity;
				const FVector av0=ball->AngularVelocity;
				const FRotator r0=ball->Rotation;
				b.pos=ToVec(p0); b.vel=ToVec(v0); b.angVel=ToVec(av0); b.rotMat=ToRot(r0);
			}else{
				b.pos=ToVec0(); b.vel=ToVec0(); b.angVel=ToVec0(); b.rotMat=ToRot(FRotator{0,0,0});
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			b.pos=ToVec0(); b.vel=ToVec0(); b.angVel=ToVec0(); b.rotMat=ToRot(FRotator{0,0,0});
		}
	}else{
		b.pos=ToVec0(); b.vel=ToVec0(); b.angVel=ToVec0(); b.rotMat=ToRot(FRotator{0,0,0});
	}
	gs.ball=b;
	UpdateBoostPads(gs);
	return true;
}

struct SnapView {
	int32_t Count;
	int32_t Local;
	JukeCarSnap Cars[64];
	JukeBallSnap Ball;
};

static __declspec(noinline) bool ReadSnap(SnapView& out){
	for(int k=0;k<3;++k){
		uint32_t s1=g_JukeSnap.Seq.load(std::memory_order_acquire);
		if(s1&1u) continue;
		int32_t n=g_JukeSnap.Count;
		int32_t li=g_JukeSnap.Local;
		if(n<0) n=0;
		if(n>64) n=64;
		out.Count=n;
		out.Local=li;
		if(n>0) memcpy(out.Cars,g_JukeSnap.Cars,(size_t)n*sizeof(JukeCarSnap));
		out.Ball=g_JukeSnap.Ball;
		uint32_t s2=g_JukeSnap.Seq.load(std::memory_order_acquire);
		if(s1==s2) return true;
	}
	out.Count=0;
	out.Local=-1;
	out.Ball=JukeBallSnap{};
	return false;
}

static RocketSim::Vec ToVec3(float x,float y,float z){ return RocketSim::Vec(x,y,z); }

static void BuildGameStateImpl(RLGC::GameState& gs,int& localIdx,int ticksElapsed){
	static int s_dbg=0;
	static uintptr_t s_lastGameEvent=0;

	APlayerController* pc=Instances.IAPlayerController();

	__try{
		if(pc && pc->bDeleteMe) pc=nullptr;
	}__except(EXCEPTION_EXECUTE_HANDLER){
		pc=nullptr;
	}

	if(!pc){
		__try{
			pc = Instances.GetInstanceOf<APlayerController_TA>();
			if(pc && pc->bDeleteMe) pc=nullptr;
		}__except(EXCEPTION_EXECUTE_HANDLER){
			pc=nullptr;
		}
	}

	if(!pc){
		if(auto* lp=Instances.IULocalPlayer()) {
			__try{
				pc=lp->Actor;
				if(pc && pc->bDeleteMe) pc=nullptr;
			}__except(EXCEPTION_EXECUTE_HANDLER){
				pc=nullptr;
			}
		}
	}

	if(!pc){
		g_InputAddr=0;
		if(s_dbg<20 || (s_dbg++%240)==0) jukeOut("gs: pc null (proceeding for recovery)");
	} else {

	}

	SnapView sv{};
	ReadSnap(sv);
	int nCars=(sv.Count<0)?0:((sv.Count>64)?64:sv.Count);
	if(nCars<=0){

		static int s_live=0;
		if(s_live<10){ jukeOut("gs: snap empty, trying TryBuildFromLive"); s_live++; }

		ACar_TA* myCar = nullptr;

		AGameEvent_TA* ge = nullptr;
		__try{
			auto* gvc = Instances.GetInstanceOf<UGameViewportClient_TA>();
			if(gvc){
				constexpr uintptr_t GVC_GAME_EVENT_OFFSET = 0x0300;
				ge = *(AGameEvent_TA**)((uintptr_t)gvc + GVC_GAME_EVENT_OFFSET);
				if(ge && ge->bDeleteMe) ge=nullptr;
			}
		}__except(EXCEPTION_EXECUTE_HANDLER){
			ge=nullptr;
		}

		if(ge){
			__try{
				constexpr uintptr_t LOCAL_PLAYERS_OFFSET = 0x0360;
				auto* localPlayersArray = (TArray<APlayerController*>*)((uintptr_t)ge + LOCAL_PLAYERS_OFFSET);
				int nlp = 0;
				__try{ nlp = localPlayersArray->size(); }__except(EXCEPTION_EXECUTE_HANDLER){ nlp = 0; }

				if(nlp > 0){
					APlayerController* localPC = nullptr;
					__try{ localPC = localPlayersArray->at(0); }__except(EXCEPTION_EXECUTE_HANDLER){ localPC = nullptr; }

					if(localPC && !localPC->bDeleteMe){

						pc = localPC;

						auto* pri = (APRI_TA*)localPC->PlayerReplicationInfo;
						if(pri && !pri->bDeleteMe){
							constexpr uintptr_t PRI_CAR_OFFSET = 0x0498;
							myCar = *(ACar_TA**)((uintptr_t)pri + PRI_CAR_OFFSET);
							if(myCar && myCar->bDeleteMe) myCar = nullptr;

							static int s_gep = 0;
							if(s_gep < 5){
								char buf[256];
								sprintf_s(buf, "myCar via GameEvent.LocalPlayers: localPC=%p pri=%p car=%p", localPC, pri, myCar);
								jukeOut(buf);
								s_gep++;
							}
						}
					}
				}
			}__except(EXCEPTION_EXECUTE_HANDLER){}
		}

		if(!myCar){
			__try {
				if(pc && pc->IsA(APlayerController_TA::StaticClass())) {
					auto* pct = (APlayerController_TA*)pc;
					auto* pri = (APRI_TA*)pct->PlayerReplicationInfo;
					if(pri && !pri->bDeleteMe) {
						constexpr uintptr_t PRI_CAR_OFFSET = 0x0498;
						myCar = *(ACar_TA**)((uintptr_t)pri + PRI_CAR_OFFSET);
						if(myCar && myCar->bDeleteMe) myCar = nullptr;

						static int s_pricar = 0;
						if(s_pricar < 5) {
							char buf[256];
							sprintf_s(buf, "myCar via cached PC PRI: pri=%p car=%p", pri, myCar);
							jukeOut(buf);
							s_pricar++;
						}
					}
				}
			} __except(EXCEPTION_EXECUTE_HANDLER) {}
		}

		if(!myCar && pc && pc->Pawn && !pc->Pawn->bDeleteMe) {
			__try{
				if(pc->Pawn->IsA(ACar_TA::StaticClass())) {
					myCar = (ACar_TA*)pc->Pawn;
					static int s_pawn = 0;
					if(s_pawn < 5) { jukeOut("myCar via pc->Pawn fallback"); s_pawn++; }
				}
			}__except(EXCEPTION_EXECUTE_HANDLER){
				myCar = nullptr;
			}
		}

		if(pc){
			g_InputAddr = (uintptr_t)pc + 0x9A8;
		}

		if(TryBuildFromLive(gs,localIdx,ticksElapsed,myCar)){

			static int s_ok=0;
			if(s_ok<10){
				char buf[128];
				sprintf_s(buf, "TryBuildFromLive ok: players=%d", (int)gs.players.size());
				jukeOut(buf);
				s_ok++;
			}
			return;
		}
		static int s_fail=0;
		if(s_fail<10){ jukeOut("TryBuildFromLive failed"); s_fail++; }
		if(s_dbg<20 || (s_dbg++%240)==0) jukeOut("gs: snap empty");
		return;
	}

	if(pc) g_InputAddr = (uintptr_t)pc + 0x9A8;

	gs.players.reserve((size_t)nCars);

	for(int idx=0; idx<nCars; ++idx){
		const JukeCarSnap& c=sv.Cars[idx];

		RLGC::Player p{};
		p.index=idx;
		p.carId=(uint32_t)idx;

		p.team=(c.Team==1)?Team::ORANGE:Team::BLUE;

		p.pos=ToVec3(c.Px,c.Py,c.Pz);
		p.vel=ToVec3(c.Vx,c.Vy,c.Vz);
		p.angVel=ToVec3(c.AVx,c.AVy,c.AVz);

		FRotator r{};
		r.Pitch=c.Pitch;
		r.Yaw=c.Yaw;
		r.Roll=c.Roll;
		p.rotMat=ToRot(r);

		const bool wc=(c.OnGround!=0);
		if(idx>=0 && idx<(int)g_OnGroundTicks.size()){
			if(wc) g_OnGroundTicks[(size_t)idx]=0;
			else{
				int add=ticksElapsed; if(add<0) add=0;
				g_OnGroundTicks[(size_t)idx]+=add;
				if(g_OnGroundTicks[(size_t)idx]>1000000) g_OnGroundTicks[(size_t)idx]=1000000;
			}
			p.isOnGround=wc || g_OnGroundTicks[(size_t)idx]<=6;
		}else p.isOnGround=wc;

		p.hasJumped=(c.Jumped!=0);
		p.hasDoubleJumped=(c.DoubleJumped!=0);
		p.hasFlipped=p.hasDoubleJumped;
		p.airTimeSinceJump=0.f;

		p.isDemoed=(c.Demoed!=0);
		p.boost=c.Boost;

		gs.players.push_back(p);
	}

	localIdx=(sv.Local>=0 && sv.Local<(int)gs.players.size())?sv.Local:0;

	RocketSim::BallState b{};
	b.pos=ToVec3(sv.Ball.Px,sv.Ball.Py,sv.Ball.Pz);
	b.vel=ToVec3(sv.Ball.Vx,sv.Ball.Vy,sv.Ball.Vz);
	b.angVel=ToVec3(sv.Ball.AVx,sv.Ball.AVy,sv.Ball.AVz);

	FRotator br{};
	br.Pitch=sv.Ball.Pitch;
	br.Yaw=sv.Ball.Yaw;
	br.Roll=sv.Ball.Roll;
	b.rotMat=ToRot(br);

	gs.ball=b;
	UpdateBoostPads(gs);
}

static void BuildGameState(RLGC::GameState& gs,int& localIdx,int ticksElapsed){
	gs.players.clear();
	localIdx=-1;
	__try{
		BuildGameStateImpl(gs,localIdx,ticksElapsed);
	}__except(EXCEPTION_EXECUTE_HANDLER){
		gs.players.clear();
		localIdx=-1;
	}
}

static bool LooksLikeModelDir(const std::filesystem::path& dir){
	std::error_code ec;
	if(!std::filesystem::exists(dir,ec)) return false;
	auto has=[&](const wchar_t* f){ return std::filesystem::exists(dir/f,ec); };
	return has(L"POLICY.lt")||has(L"POLICY_OPTIM.lt")||has(L"SHARED_HEAD.lt")||has(L"SHARED_HEAD_OPTIM.lt")||has(L"CRITIC.lt")||has(L"CRITIC_OPTIM.lt");
}
static std::filesystem::path ResolveModelDir(const std::filesystem::path& root){
	std::error_code ec;
	if(!std::filesystem::exists(root,ec)) return root;
	if(LooksLikeModelDir(root)) return root;
	long long best=-1;
	std::filesystem::path bestp;
	for(const auto& e: std::filesystem::directory_iterator(root,ec)){
		if(ec) break;
		if(!e.is_directory(ec)) continue;
		auto name=e.path().filename().wstring();
		try{ long long v=std::stoll(name); if(v>best){best=v;bestp=e.path();} }catch(...){}
	}
	return (best>=0)?bestp:root;
}

static std::unique_ptr<RLGC::ObsBuilder> g_Obs;
static std::unique_ptr<RLGC::ActionParser> g_Parser;
static Juke::TorchPolicy g_Torch;
static std::mutex g_ModelMx;
static int g_PendingIdx=0,g_CurrentIdx=0;
static RLGC::Action g_Current{};
static std::filesystem::path g_ModelsFolder;

static bool g_tbp=false;

}

namespace JukeBot {

void Inference_Init(){
	g_BotEnabled=false;
	{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput = FVehicleInputs{}; }
	ZeroInputsOnce();
	g_ModelOk=false;

	g_BoostMapBuilt = false;
	g_BoostPads.fill(nullptr);
	g_ball = nullptr;
	g_GameEventAddr = 0;
	g_GameShareAddr = 0;
	g_InputAddr = 0;

	std::lock_guard<std::mutex> lk(g_ModelMx);

	try{
		torch::set_num_threads(1);
		torch::set_num_interop_threads(1);
	}catch(...){}

	if(g_JukeConfig.ObsBuilder == "AdvancedObs") {
		g_Obs = std::make_unique<RLGC::AdvancedObs1v1>();
	} else if(g_JukeConfig.ObsBuilder == "AdvancedObsPadded1v1") {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(1, false);
	} else if(g_JukeConfig.ObsBuilder == "AdvancedObsPadded2v2") {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(2, false);
	} else if(g_JukeConfig.ObsBuilder == "AdvancedObsPadded3v3") {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(3, false);
	} else if(g_JukeConfig.ObsBuilder == "AdvancedObsPadded4v4") {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(4, false);
	} else if(g_JukeConfig.ObsBuilder == "AdvancedObsPadded") {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(3, false);
	} else if(g_JukeConfig.ObsBuilder == "Custom") {
		g_Obs = std::make_unique<RLGC::CustomObs>(3);
	} else {
		g_Obs = std::make_unique<RLGC::AdvancedObsPadderGGL>(3, false);
	}
	g_Parser=std::make_unique<RLGC::LookupAction>();

	g_ModelsFolder=g_JukeConfig.BotPath.empty()?std::filesystem::path(L"Bot"):std::filesystem::path(g_JukeConfig.BotPath);
	g_ModelsFolder=ResolveModelDir(g_ModelsFolder);

	char p[768];
	sprintf_s(p,"model_dir=%ls", g_ModelsFolder.wstring().c_str());
	jukeOut(p);

	g_Current=RLGC::Action{};
	g_PendingIdx=g_CurrentIdx=0;
	g_OnGroundTicks.fill(0);

	bool useGPU = (g_JukeConfig.Device == "cuda" || g_JukeConfig.Device == "gpu");
	if (useGPU && !torch::cuda::is_available()) {
		jukeOut("cuda requested but not available; falling back to cpu");
		useGPU = false;
	}
	const bool ok = g_Torch.Load(g_ModelsFolder, g_JukeConfig.UseSharedHead, useGPU,
		g_JukeConfig.PolicySize, g_JukeConfig.SharedHeadSize, g_JukeConfig.ObsSize, g_JukeConfig.Activation);
	g_ModelOk = ok;
	jukeOut(ok?"model ok":"model fail");
	if(!ok){
		jukeOut("hint: ensure POLICY.lt and SHARED_HEAD.lt exist (case-insensitive) in botdir");
		return;
	}

	try{
		torch::NoGradGuard ng;
		auto z=torch::zeros({g_JukeConfig.ObsSize},torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU));
		int a=g_Torch.Act(z);
		char b[128];
		sprintf_s(b,"warmup action=%d", a);
		jukeOut(b);
	}catch(const std::exception& e){
		std::string msg = "warmup fail: ";
		msg += e.what();
		jukeOut(msg.c_str());
	}catch(...){
		jukeOut("warmup fail: unknown exception");
	}
}

static void StepFixed120(int ticksElapsed){
	static int s_ticks=8;
	static bool s_upd=true;
	static int s_print=0;

	if(ticksElapsed<0) ticksElapsed=0;

	if(!g_BotEnabled){
		s_ticks=g_JukeConfig.TickSkip>0?g_JukeConfig.TickSkip:8;
		s_upd=true;
		g_Current=RLGC::Action{};
		g_PendingIdx=g_CurrentIdx=0;
		g_OnGroundTicks.fill(0);
		{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput=FVehicleInputs{}; }
		return;
	}

	if(!g_ModelOk.load()){
		if((s_print++%240)==0) jukeOut("bot on but model not loaded");
		return;
	}

	std::lock_guard<std::mutex> mlk(g_ModelMx);
	if(!g_ModelOk.load() || !g_Obs || !g_Parser){
		if((s_print++%240)==0) jukeOut("bot on but model not ready");
		return;
	}

	s_ticks += ticksElapsed;

	static RLGC::GameState gs{};
	static RLGC::FList obs{};

	gs.deltaTime=(float)ticksElapsed/120.f;

	int localIdx=-1;
	BuildGameState(gs,localIdx,ticksElapsed);
	if(gs.players.empty()){
		if((s_print++%240)==0) jukeOut("no players in gamestate");
		return;
	}
	if(localIdx<0||localIdx>=(int)gs.players.size()) localIdx=0;

	RLGC::Player& me=gs.players[localIdx];
	me.prevAction=g_Current;

	static int s_team_dbg=0;
	if(s_team_dbg<10){
		char buf[200];
		sprintf_s(buf,"local: idx=%d team=%s", localIdx, (me.team==Team::ORANGE)?"ORANGE":"BLUE");
		jukeOut(buf);
		s_team_dbg++;
	}

	if(s_upd){
		s_upd=false;
		g_Obs->BuildObs2(me,gs,obs);
		if((s_print++%240)==0){
			char ob[256];
			sprintf_s(ob,"tick: players=%d local=%d obs=%d", (int)gs.players.size(), localIdx, (int)obs.size());
			jukeOut(ob);

			if(obs.size()>=9){
				char db[256];
				sprintf_s(db,"obs[0-8]: %.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f",
					obs[0],obs[1],obs[2],obs[3],obs[4],obs[5],obs[6],obs[7],obs[8]);
				jukeOut(db);

				if(obs.size() >= 77) {
					char db2[512];
					sprintf_s(db2, "player[51-76]: relBall=%.2f,%.2f,%.2f pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f up=%.2f,%.2f,%.2f boost=%.2f og=%.0f flip=%.0f",
						obs[51],obs[52],obs[53],
						obs[57],obs[58],obs[59],
						obs[60],obs[61],obs[62],
						obs[63],obs[64],obs[65],
						obs[74], obs[75], obs[76]);
					jukeOut(db2);
				}
			}
		}
		try{
			torch::NoGradGuard ng;
			torch::Tensor t=torch::from_blob(obs.data(),{(int64_t)obs.size()},torch::kFloat32);
			g_PendingIdx=g_Torch.Act(t);
			if((s_print++%240)==0){
				char ab[96];
				sprintf_s(ab,"act_idx=%d", g_PendingIdx);
				jukeOut(ab);
			}

			g_CurrentIdx = g_PendingIdx;

		}catch(...){
			g_ModelOk=false;
			jukeOut("inference exception; model disabled");
			return;
		}
	}

	const int tickSkip=g_JukeConfig.TickSkip>0?g_JukeConfig.TickSkip:4;

	if(s_ticks >= tickSkip-1){

		g_Current = g_Parser->ParseAction(g_CurrentIdx,me,gs);
	}

	if(s_ticks >= tickSkip){
		s_ticks = 0;
		s_upd = true;
	}

	FVehicleInputs in{};
	in.Throttle=g_Current.throttle;
	in.Steer=g_Current.steer;
	in.Pitch=g_Current.pitch;
	in.Yaw=g_Current.yaw;
	in.Roll=g_Current.roll;
	in.DodgeForward=-g_Current.pitch;
	in.DodgeRight=g_Current.yaw;
	in.bHandbrake=(g_Current.handbrake>=0.5f)?1u:0u;
	in.bJump=(g_Current.jump>=0.5f)?1u:0u;
	in.bActivateBoost=(g_Current.boost>=0.5f)?1u:0u;
	in.bHoldingBoost=in.bActivateBoost;
	in.bJumped=0u;
	{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput=in; }

	void* dest=(void*)g_InputAddr.load();
	if(dest){
		InputPacket pkt{};
		pkt.Throttle=in.Throttle;
		pkt.Steer=in.Steer;
		pkt.Pitch=in.Pitch;
		pkt.Yaw=in.Yaw;
		pkt.Roll=in.Roll;
		pkt.DodgeForward=in.DodgeForward;
		pkt.DodgeRight=in.DodgeRight;
		uint32_t f=0;
		if(in.bHandbrake) f|=1;
		if(in.bJump) f|=(1<<1);
		if(in.bActivateBoost) f|=(1<<2);
		if(in.bHoldingBoost) f|=(1<<3);
		pkt.Flags=f;
		SafeMemCpy(dest,&pkt,sizeof(pkt));
	}

	if((s_print++%240)==0){
		char ib[192];
		sprintf_s(ib,"out thr=%.2f st=%.2f p=%.2f y=%.2f j=%d b=%d hb=%d", in.Throttle,in.Steer,in.Pitch,in.Yaw,(int)in.bJump,(int)in.bActivateBoost,(int)in.bHandbrake);
		jukeOut(ib);
	}
}

void Inference_OnTick(float dt){
	static double s_accum = 0.0;
	if(dt > 0.f){
		s_accum += (double)dt * 120.0;
	}
	int ticksElapsed = (int)s_accum;
	if (ticksElapsed > 0) {
		s_accum -= (double)ticksElapsed;
		StepFixed120(ticksElapsed);
	}
}

static unsigned __stdcall InitThread(void*){
	AddVectoredExceptionHandler(1,Juke_SEH);
	SetupDllSearch();

	if(!g_tbp){
		try{
			if(timeBeginPeriod(1)==TIMERR_NOERROR) g_tbp=true;
		}catch(...){}
	}

	if(!g_WriterStarted.exchange(true)){
		unsigned tid=0;
		HANDLE h=(HANDLE)_beginthreadex(nullptr,0,&InputWriterThread,nullptr,0,&tid);
		if(h) CloseHandle(h);
	}
	if(!g_LoopStarted.exchange(true)){
		unsigned tid=0;
		HANDLE h=(HANDLE)_beginthreadex(nullptr,0,&BotLoopThread,nullptr,0,&tid);
		if(h) CloseHandle(h);
	}

	jukeOut("dll init");

	jukeOut("cfg skipped");

	Core.InitializeThread();
	jukeOut("core init");

	dlog(L"init_ok");
	return 0;
}

static HANDLE g_singleton_mutex=nullptr;

void OnAttach(HMODULE){
	static std::atomic<bool> s_once{false};
	if(s_once.exchange(true)){
		jukeOut("dll attach skipped (already initialized)");
		return;
	}

	if(!g_singleton_mutex){
		g_singleton_mutex = CreateMutexA(nullptr, FALSE, "Local\\JukeInjectSingleton");
		if(g_singleton_mutex && GetLastError() == ERROR_ALREADY_EXISTS){
			CloseHandle(g_singleton_mutex);
			g_singleton_mutex=nullptr;
			jukeOut("dll attach skipped (singleton already exists)");
			return;
		}
	}

	dlog(L"attach");
	unsigned tid=0;
	HANDLE h=(HANDLE)_beginthreadex(nullptr,0,&InitThread,nullptr,0,&tid);
	if(h) CloseHandle(h);
}
void OnDetach(){
	g_LoopRun=false;
	g_WriterRun=false;
	g_BotEnabled=false;
	g_ModelOk=false;
	{ std::lock_guard<std::mutex> lk(g_InputMx); g_LatestInput = FVehicleInputs{}; }
	ZeroInputsOnce();
	g_InputAddr=0;
	if(g_tbp) timeEndPeriod(1);

	if(g_singleton_mutex){
		CloseHandle(g_singleton_mutex);
		g_singleton_mutex=nullptr;
	}
}

}

void Inference_OnTick(float dt){ JukeBot::Inference_OnTick(dt); }
