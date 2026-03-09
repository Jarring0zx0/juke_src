#include "GUI.hpp"
#include "../Includes.hpp"

#include <d3d11.h>
#include <dxgi.h>

#include "../ImGui/Kiero/kiero.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"
#include "../ImGui/imgui_stdlib.h"
#include "../Components/Manager.hpp"
#include "../Modules/Mods/JukeUI.hpp"

typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

GUIComponent::GUIComponent() : Component("UserInterface", "Displays an interface") { OnCreate(); }

GUIComponent::~GUIComponent() { OnDestroy(); }

bool init = false;

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{

	if (!pSwapChain)
		return oPresent(pSwapChain, SyncInterval, Flags);

	if (!init || !pDevice || !pContext || !mainRenderTargetView)
	{

		pDevice = nullptr;
		pContext = nullptr;
		mainRenderTargetView = nullptr;

		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)) && pDevice)
		{
			pDevice->GetImmediateContext(&pContext);

			DXGI_SWAP_CHAIN_DESC sd{};
			pSwapChain->GetDesc(&sd);
			window = sd.OutputWindow;

			ID3D11Texture2D* pBackBuffer = nullptr;
			if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer)) && pBackBuffer)
			{
				pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
				pBackBuffer->Release();
			}

			if (window && !oWndProc)
				oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)GUIComponent::WndProc);

			if (pContext && mainRenderTargetView)
			{
				GUIComponent::InitImGui();
				init = true;
			}
			else
			{

				return oPresent(pSwapChain, SyncInterval, Flags);
			}
		}
		else
		{
			return oPresent(pSwapChain, SyncInterval, Flags);
		}
	}

	try
	{
		GUIComponent::Render();
		pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	catch (...)
	{
		return oPresent(pSwapChain, SyncInterval, Flags);
	}

	return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread()
{
	bool init_hook = false;
	do
	{
		if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
		{
			kiero::bind(8, (void**)&oPresent, hkPresent);
			init_hook = true;
		}
	} while (!init_hook);
	return TRUE;
}

void GUIComponent::OnCreate() {}

void GUIComponent::OnDestroy() { }

void GUIComponent::Unload()
{

	if (init)
	{
		kiero::shutdown();
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
	}

	CloseHandle(InterfaceThread);
}

HANDLE GUIComponent::InterfaceThread = NULL;

bool GUIComponent::IsOpen = true;

void GUIComponent::Initialize() {
	if (InterfaceThread) return;
	IsOpen = true;
	InterfaceThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), nullptr, 0, nullptr);
}

void GUIComponent::InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);

}

static bool IsInputMsg(UINT m){
	switch(m){
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
	case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
	case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
	case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
	case WM_KEYDOWN: case WM_KEYUP:
	case WM_SYSKEYDOWN: case WM_SYSKEYUP:
	case WM_CHAR: case WM_SYSCHAR:
		return true;
	default:
		return false;
	}
}

LRESULT __stdcall GUIComponent::WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_KEYDOWN && wParam == VK_F1) {
		IsOpen = !IsOpen;
		return TRUE;
	}

	if (IsOpen) {
		ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

		if (IsInputMsg(uMsg))
			return TRUE;
	}

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void GUIComponent::Render()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	GUI.DisplayX = (int)io.DisplaySize.x;
	GUI.DisplayY = (int)io.DisplaySize.y;

	io.MouseDrawCursor = false;

	if (IsOpen) {
		if (auto jukeUI = Manager.GetModule<JukeUI>("JukeUI"))
			jukeUI->OnRender();
	}

	ImGui::Render();
}

void Initialize() {}

class GUIComponent GUI {};
