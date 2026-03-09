#include "Core.hpp"
#include "../Includes.hpp"
#include <process.h>

extern "C" void jukeOut(const char* s);

CoreComponent::CoreComponent() : Component("Core", "Initializes globals, components, and modules.") { OnCreate(); }

CoreComponent::~CoreComponent() { OnDestroy(); }

void CoreComponent::OnCreate()
{
	MainThread = nullptr;
}

void CoreComponent::OnDestroy() {
	DestroyThread();
}

void CoreComponent::InitializeThread()
{
	unsigned tid = 0;
	MainThread = (HANDLE)_beginthreadex(nullptr, 0, &CoreComponent::InitializeGlobals, nullptr, 0, &tid);
}

void CoreComponent::DestroyThread()
{
	if (MainThread) {
		CloseHandle(MainThread);
		MainThread = nullptr;
	}
}

uintptr_t CoreComponent::GetGObjects() {

	return NULL;
}

uintptr_t CoreComponent::GetGNames() {

	return NULL;
}

unsigned __stdcall CoreComponent::InitializeGlobals(void*)
{
	Console.Initialize(std::filesystem::current_path(), "RLMod.log");

	uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));

	uintptr_t gobj_addr = 0;
	uintptr_t gname_addr = 0;

	if(!gobj_addr){
		const unsigned char p[] = {0x48, 0x8B, 0xC8, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC8};
		const char* m = "xxxxxx????xxxx";
		uintptr_t a = Memory::FindPattern(GetModuleHandle(NULL), p, m);
		if(a) gobj_addr = a + 10 + *(int32_t*)(a + 6);
	}

	if(!gobj_addr){
		const unsigned char p[] = {0x48, 0x8B, 0x05, 0x33, 0xF2, 0x03, 0x02};
		const char* m = "xxxxxxx";

		uintptr_t a = Memory::FindPattern(GetModuleHandle(NULL), p, m);
		if(a) gobj_addr = a + 7 + *(int32_t*)(a + 3);
	}

	if(!gname_addr){
		const unsigned char p[] = {0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC1};
		const char* m = "xxx????xxxx";
		uintptr_t a = Memory::FindPattern(GetModuleHandle(NULL), p, m);
		if(a) gname_addr = a + 7 + *(int32_t*)(a + 3);
	}

	if(!gname_addr){
		const unsigned char p[] = {0x49, 0x63, 0x06, 0x48, 0x8D, 0x55, 0xE8, 0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC1};
		const char* m = "xxxxxxxxxx????xxxx";
		uintptr_t a = Memory::FindPattern(GetModuleHandle(NULL), p, m);
		if(a) gname_addr = a + 14 + *(int32_t*)(a + 10);
	}

	if (gobj_addr && gname_addr) {
		GObjects = reinterpret_cast<TArray<UObject*>*>(gobj_addr);
		GNames = reinterpret_cast<TArray<FNameEntry*>*>(gname_addr);

		char buf[256];
		sprintf_s(buf, "Pattern scan success: GObj=%llX GName=%llX", (unsigned long long)gobj_addr, (unsigned long long)gname_addr);
		jukeOut(buf);
	} else {
		char buf[256];
		sprintf_s(buf, "Pattern scan failed (GObj=%llX GName=%llX); using hardcoded", (unsigned long long)gobj_addr, (unsigned long long)gname_addr);
		jukeOut(buf);

		GObjects = reinterpret_cast<TArray<UObject*>*>(BaseAddress + GObjects_Offset);
		GNames = reinterpret_cast<TArray<FNameEntry*>*>(BaseAddress + GNames_Offset);
	}

	Manager.Initialize();
	GUI.Initialize();

	if (AreGlobalsValid())
	{

		Instances.Initialize();

		Events.Initialize();
		void** UnrealVTable = reinterpret_cast<void**>(UObject::StaticClass()->VfTableObject.Dummy);
		EventsComponent::AttachDetour(reinterpret_cast<ProcessEventType>(UnrealVTable[67]));

		Main.Initialize();
	}
	else
	{
		Console.Error("[ JukeDLL ] Are offsets correct?");
	}
	return 0;
}

bool CoreComponent::AreGlobalsValid()
{
	return (AreGObjectsValid() && AreGNamesValid());
}

bool CoreComponent::AreGObjectsValid()
{
	if (GObjects
		&& UObject::GObjObjects()->size() > 0
		&& UObject::GObjObjects()->capacity() > UObject::GObjObjects()->size())
	{
		return true;
	}

	return false;
}

bool CoreComponent::AreGNamesValid()
{
	if (GNames
		&& FName::Names()->size() > 0
		&& FName::Names()->capacity() > FName::Names()->size())
	{
		return true;
	}

	return false;
}

class CoreComponent Core {};
