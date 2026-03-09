
#pragma once

#include "GameDefines.hpp"

#include "SDK_HEADERS\Core_structs.hpp"
#include "SDK_HEADERS\Core_classes.hpp"
#include "SDK_HEADERS\Core_parameters.hpp"
#include "SDK_HEADERS\Engine_structs.hpp"
#include "SDK_HEADERS\Engine_classes.hpp"
#include "SDK_HEADERS\Engine_parameters.hpp"
#include "SDK_HEADERS\IpDrv_structs.hpp"
#include "SDK_HEADERS\IpDrv_classes.hpp"
#include "SDK_HEADERS\IpDrv_parameters.hpp"
#include "SDK_HEADERS\XAudio2_structs.hpp"
#include "SDK_HEADERS\XAudio2_classes.hpp"
#include "SDK_HEADERS\XAudio2_parameters.hpp"
#include "SDK_HEADERS\GFxUI_structs.hpp"
#include "SDK_HEADERS\GFxUI_classes.hpp"
#include "SDK_HEADERS\GFxUI_parameters.hpp"
#include "SDK_HEADERS\AkAudio_structs.hpp"
#include "SDK_HEADERS\AkAudio_classes.hpp"
#include "SDK_HEADERS\AkAudio_parameters.hpp"
#include "SDK_HEADERS\WinDrv_structs.hpp"
#include "SDK_HEADERS\WinDrv_classes.hpp"
#include "SDK_HEADERS\WinDrv_parameters.hpp"
#include "SDK_HEADERS\OnlineSubsystemEOS_structs.hpp"
#include "SDK_HEADERS\OnlineSubsystemEOS_classes.hpp"
#include "SDK_HEADERS\OnlineSubsystemEOS_parameters.hpp"
#include "SDK_HEADERS\ProjectX_structs.hpp"
#include "SDK_HEADERS\ProjectX_classes.hpp"
#include "SDK_HEADERS\ProjectX_parameters.hpp"
#include "SDK_HEADERS\TAGame_structs.hpp"
#include "SDK_HEADERS\TAGame_classes.hpp"
#include "SDK_HEADERS\TAGame_parameters.hpp"

// Class Core.Group_ORS - moved here to avoid forward declaration issues
// 0x00D0 (0x0068 - 0x0138)
class UGroup_ORS : public UScriptGroup_ORS
{
public:
	uint8_t                                           UnknownData00[0xD0];

public:
	static UClass* StaticClass()
	{
		static UClass* uClassPointer = nullptr;
		if (!uClassPointer)
		{
			uClassPointer = UObject::FindClass("Class Core.Group_ORS");
		}
		return uClassPointer;
	};
};
