#include "GUI.hpp"
#include "../Includes.hpp"

#include <d3d11.h>
#include <dxgi.h>

#include "../ImGui/Kiero/kiero.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"
#include "../ImGui/imgui_stdlib.h"

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
	if (!init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
		{
			pDevice->GetImmediateContext(&pContext);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			window = sd.OutputWindow;
			ID3D11Texture2D* pBackBuffer;
			pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
			pBackBuffer->Release();
			oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)GUIComponent::WndProc);
			GUIComponent::InitImGui();
			init = true;
		}

		else
			return oPresent(pSwapChain, SyncInterval, Flags);
	}

	GUIComponent::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
	InterfaceThread = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), nullptr, 0, nullptr);
}

void GUIComponent::InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
	//io.Fonts->AddFontFromFileTTF("Helvetica.ttf", 13);

}

LRESULT __stdcall GUIComponent::WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
	if (io.WantCaptureMouse && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONUP || uMsg == WM_MOUSEWHEEL || uMsg == WM_MOUSEMOVE))
	{
		return TRUE;
	}

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void coolstyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	style.WindowPadding = ImVec2(6.0f, 6.0f);
	style.WindowRounding = 8.0f;
	style.WindowBorderSize = 0.0f;
	style.WindowMinSize = ImVec2(450.0f, 450.0f);
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.ChildRounding = 0.0f;
	style.ChildBorderSize = 0.0f;
	style.PopupRounding = 8.0f;
	style.PopupBorderSize = 0.0f;
	style.FramePadding = ImVec2(12.0f, 6.0f);
	style.FrameRounding = 6.0f;
	style.FrameBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(10.0f, 3.0f);
	style.ItemInnerSpacing = ImVec2(12.0f, 4.0f);
	style.IndentSpacing = 25.0f;
	style.ColumnsMinSpacing = 6.0f;
	style.ScrollbarSize = 16.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabMinSize = 12.0f;
	style.GrabRounding = 6.0f;
	style.TabRounding = 6.0f;
	style.TabBorderSize = 0.0f;
	style.ColorButtonPosition = ImGuiDir_Right;
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

	// Dark modern theme colors
	style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.7f, 0.3f, 1.0f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(1.0f, 0.6f, 0.2f, 0.8f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.6f, 0.2f, 0.8f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_Separator] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(1.0f, 0.6f, 0.2f, 0.8f);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(1.0f, 0.6f, 0.2f, 0.8f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.8f, 0.5f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.0f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.0f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void GUIComponent::InitMainTab() {
	// Trajectory checkbox instead of button
	ImGui::Checkbox("Trajectory", &Main.TrajectoryEnabled);
	
	// Color pickers
	ImGui::Text("ColorNormal");
	ImGui::SameLine();
	ImGui::ColorEdit4("##ColorNormal", Main.ColorNormal, ImGuiColorEditFlags_NoInputs);
	
	ImGui::Text("ColorNet");
	ImGui::SameLine();
	ImGui::ColorEdit4("##ColorNet", Main.ColorNet, ImGuiColorEditFlags_NoInputs);

	// Original buttons (commented out)
	/*
	if (ImGui::Button("Template Button")) {
		Main.Execute([]() {
			Main.SpawnNotification("Template", "Template button has been pressed!", 10);
		});
	}
	if (ImGui::Button("Spawn ball")) {
		Main.Execute([]() {
			Main.SpawnBall();
		});
	}
	if (ImGui::Button("edit ball")) {
		Main.Execute([]() {
			Main.edit();
		});
	}
	*/
}

void GUIComponent::Render()
{
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	coolstyle();

	ImGuiIO IO = ImGui::GetIO();
	GUI.DisplayX = IO.DisplaySize.x;
	GUI.DisplayY = IO.DisplaySize.y;

	IO.MouseDrawCursor = IsOpen;
	
	// Make window square-shaped (450x450 instead of 840x450)
	ImGui::SetNextWindowSize(ImVec2(450, 450));

	if (IsOpen) {
		ImGui::Begin("GoodLeague", &IsOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
		{
			if (ImGui::BeginTabBar("Tab bar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoTooltip)) {
				if (ImGui::BeginTabItem("Trajectory")) {
					InitMainTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}
	
	// Draw trajectory using the data calculated in Events.cpp
	if (Main.TrajectoryEnabled && !trajectoryData.empty()) {
		try {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			
			// Only draw the FIRST trajectory (there should only be one)
			if (!trajectoryData.empty() && !trajectoryData[0].empty()) {
				const auto& ballTrajectory = trajectoryData[0];
				
				bool isGoalTrajectory = ballTrajectory[0].isGoalTrajectory;
				
				ImU32 color = isGoalTrajectory ? 
					IM_COL32((int)(Main.ColorNet[0] * 255), (int)(Main.ColorNet[1] * 255), (int)(Main.ColorNet[2] * 255), (int)(Main.ColorNet[3] * 255)) :
					IM_COL32((int)(Main.ColorNormal[0] * 255), (int)(Main.ColorNormal[1] * 255), (int)(Main.ColorNormal[2] * 255), (int)(Main.ColorNormal[3] * 255));
				
				for (size_t i = 1; i < ballTrajectory.size(); i++) {
					FVector prevWorldPos = ballTrajectory[i-1].worldPos;
					FVector currWorldPos = ballTrajectory[i].worldPos;
					
					// Skip invalid coordinates
					if (abs(prevWorldPos.X) > 8000 || abs(prevWorldPos.Y) > 8000 || abs(prevWorldPos.Z) > 3000 ||
						abs(currWorldPos.X) > 8000 || abs(currWorldPos.Y) > 8000 || abs(currWorldPos.Z) > 3000) {
						continue;
					}
					
					// Skip if coordinates are identical
					if (prevWorldPos.X == currWorldPos.X && prevWorldPos.Y == currWorldPos.Y && prevWorldPos.Z == currWorldPos.Z) {
						continue;
					}
					
					FVector prevScreenPos = Drawing::CalculateScreenCoordinate(prevWorldPos);
					FVector currScreenPos = Drawing::CalculateScreenCoordinate(currWorldPos);
					
					// Only draw if coordinates are valid and on screen
					if (prevScreenPos.Z > 0 && currScreenPos.Z > 0 && 
						prevScreenPos.X > 0 && prevScreenPos.X < GUI.DisplayX &&
						prevScreenPos.Y > 0 && prevScreenPos.Y < GUI.DisplayY &&
						currScreenPos.X > 0 && currScreenPos.X < GUI.DisplayX &&
						currScreenPos.Y > 0 && currScreenPos.Y < GUI.DisplayY) {
						
						float thickness = 4.0f;
						
						drawList->AddLine(
							ImVec2(prevScreenPos.X, prevScreenPos.Y),
							ImVec2(currScreenPos.X, currScreenPos.Y),
							color,
							thickness
						);
					}
				}
			}
		}
		catch (...) {
			// Prevent crashes from drawing errors
			Console.Write("GUI: Error drawing trajectory");
		}
	}
	
	ImGui::Render();
}

void Initialize() {}

class GUIComponent GUI {};