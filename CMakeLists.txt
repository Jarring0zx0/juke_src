cmake_minimum_required(VERSION 3.23)
project(JukeDLL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Torch headers for [`torch::jit::load()`](SDK/TorchPolicy.cpp:1)
# Fix Torch+Caffe2 CUDA arch auto-detection failures (CMake error: CUDA_ARCHITECTURES is empty).
if (WIN32 AND (NOT DEFINED CMAKE_CUDA_ARCHITECTURES OR CMAKE_CUDA_ARCHITECTURES STREQUAL ""))
  set(CMAKE_CUDA_ARCHITECTURES "native")
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch/libtorch/share/cmake/Torch/TorchConfig.cmake")
  set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch/libtorch/share/cmake/Torch")
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch/libtorch")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch/share/cmake/Torch/TorchConfig.cmake")
  set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch/share/cmake/Torch")
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/libtorch")
endif()
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

add_library(JukeDLL SHARED
  dllmain.cpp
  Bot/Bot.cpp
  AdvancedObsPadderGGL.cpp
  CustomObs.cpp
  ObsBuilders/AdvancedObs.cpp
  LookupAction.cpp
  TorchPolicy.cpp
  pch.cpp
  Components/Component.cpp
  Components/Components/Main.cpp
  Components/Components/Console.cpp
  Components/Components/Core.cpp
  Components/Components/Events.cpp
  Components/Components/GameState.cpp
  Components/Components/Instances.cpp
  Components/Components/Manager.cpp
  Components/Components/GUI.cpp
  Blacklist/Blacklist.cpp
  Modules/Mods/Drawing.cpp
  Modules/Mods/JukeUI.cpp
  Modules/Module.cpp
  Extensions/Extensions/Colors.cpp
  Extensions/Extensions/Formatting.cpp
  Extensions/Extensions/Math.cpp
  Extensions/Extensions/Memory.cpp
  Extensions/Extensions/UnrealMemory.cpp
  RLSDK/GameDefines.cpp
  RLSDK/SDK_HEADERS/AkAudio_classes.cpp
  RLSDK/SDK_HEADERS/Core_classes.cpp
  RLSDK/SDK_HEADERS/Engine_classes.cpp
  RLSDK/SDK_HEADERS/GFxUI_classes.cpp
  RLSDK/SDK_HEADERS/IpDrv_classes.cpp
  RLSDK/SDK_HEADERS/OnlineSubsystemEOS_classes.cpp
  RLSDK/SDK_HEADERS/ProjectX_classes.cpp
  RLSDK/SDK_HEADERS/TAGame_classes.cpp
  RLSDK/SDK_HEADERS/WinDrv_classes.cpp
  RLSDK/SDK_HEADERS/XAudio2_classes.cpp
  ImGui/imgui.cpp
  ImGui/imgui_demo.cpp
  ImGui/imgui_draw.cpp
  ImGui/imgui_impl_dx11.cpp
  ImGui/imgui_impl_win32.cpp
  ImGui/imgui_stdlib.cpp
  ImGui/imgui_tables.cpp
  ImGui/imgui_widgets.cpp
  ImGui/Kiero/kiero.cpp
)

target_precompile_headers(JukeDLL PRIVATE pch.hpp)

target_include_directories(JukeDLL PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Detours
  ${CMAKE_CURRENT_SOURCE_DIR}/ImGui
  ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/Kiero
  ${CMAKE_CURRENT_SOURCE_DIR}/RLSDK
  ${CMAKE_CURRENT_SOURCE_DIR}/Extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/Components
  ${CMAKE_CURRENT_SOURCE_DIR}/Modules
  ${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/src/public
  ${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP/RLGymCPP/src
)

if(MSVC)
  target_compile_options(JukeDLL PRIVATE /bigobj /W4 /EHa)
  target_compile_options(JukeDLL PRIVATE "$<$<CONFIG:Release>:/O2;/GL;/Gw;/Gy;/GS;/guard:cf;/Qspectre;/sdl;/Zc:inline;/fp:fast>")
  target_link_options(JukeDLL PRIVATE "$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO;/DYNAMICBASE;/HIGHENTROPYVA;/NXCOMPAT;/CETCOMPAT;/guard:cf>")
  target_compile_definitions(JukeDLL PRIVATE "$<$<CONFIG:Release>:NDEBUG>")
endif()

# Bring in GigaLearnCPP (Torch + RLGC) for on-DLL inference
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../GigaLearn/GigaLearnCPP ${CMAKE_CURRENT_BINARY_DIR}/GigaLearnCPP_build)

target_link_libraries(JukeDLL PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Detours/detours.lib
  ${CMAKE_CURRENT_SOURCE_DIR}/Detours/syelog.lib
  ${CMAKE_CURRENT_SOURCE_DIR}/ImGui/Kiero/minhook/minhook.x64d.lib
  ${CMAKE_CURRENT_SOURCE_DIR}/DirectXTK.lib
  d3d11
  dxgi
  dxguid
  user32
  gdi32
  ws2_32
  bcrypt
  ${TORCH_LIBRARIES}
  GigaLearnCPP
)

set_target_properties(JukeDLL PROPERTIES OUTPUT_NAME "juke_inject")
target_sources(JukeDLL PRIVATE JukeDLL.def)