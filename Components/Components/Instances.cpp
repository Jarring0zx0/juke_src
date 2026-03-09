#include "Instances.hpp"
#include "../Includes.hpp"

InstancesComponent::InstancesComponent() : Component("Instances", "Manages class instances and objects.") { OnCreate(); }

InstancesComponent::~InstancesComponent() { OnDestroy(); }

void InstancesComponent::OnCreate()
{

    I_UCanvas = nullptr;
    I_AHUD = nullptr;
    I_UGameViewportClient = nullptr;
    I_APlayerController = nullptr;

    CachedCameraLocation = FVector(0.0f, 0.0f, 0.0f);
    CachedCameraRotation = FRotator(0.0f, 0.0f, 0.0f);
    CachedCameraFOV = 90.0f;
    bHasCachedCamera = false;
}

void InstancesComponent::OnDestroy()
{
	for (UObject* instance : CreatedInstances)
	{
		if (instance)
		{
			MarkForDestory(instance);
		}
	}
}

class UClass* InstancesComponent::FindStaticClass(const std::string& className)
{
	if (StaticClasses.empty())
	{
		MapObjects();
	}

	if (StaticClasses.find(className) != StaticClasses.end())
	{
		return StaticClasses[className];
	}

	return nullptr;
}

class UFunction* InstancesComponent::FindStaticFunction(const std::string& className)
{
	if (StaticFunctions.empty())
	{
		MapObjects();
	}

	if (StaticFunctions.find(className) != StaticFunctions.end())
	{
		return StaticFunctions[className];
	}

	return nullptr;
}

void InstancesComponent::MarkInvincible(class UObject* object)
{
	if (object)
	{
		object->ObjectFlags &= ~EObjectFlags::RF_Transient;
		object->ObjectFlags &= ~EObjectFlags::RF_TagGarbageTemp;
		object->ObjectFlags |= EObjectFlags::RF_Public;
		object->ObjectFlags |= EObjectFlags::RF_Standalone;
		object->ObjectFlags |= EObjectFlags::RF_MarkAsRootSet;
	}
}

void InstancesComponent::MarkForDestory(class UObject* object)
{
	if (object)
	{
		object->ObjectFlags = 0;
		object->ObjectFlags |= EObjectFlags::RF_Public;
		object->ObjectFlags |= EObjectFlags::RF_Transient;
		object->ObjectFlags |= EObjectFlags::RF_TagGarbageTemp;
	}
}

class UEngine* InstancesComponent::IUEngine()
{
	return UEngine::GetEngine();
}

class UAudioDevice* InstancesComponent::IUAudioDevice()
{
	return UEngine::GetAudioDevice();
}

class AWorldInfo* InstancesComponent::IAWorldInfo()
{

	return UEngine::GetCurrentWorldInfo();
}

class UCanvas* InstancesComponent::IUCanvas()
{
	return I_UCanvas;
}

class AHUD* InstancesComponent::IAHUD()
{
	return I_AHUD;
}

class UGameViewportClient* InstancesComponent::IUGameViewportClient()
{
	return I_UGameViewportClient;
}

class ULocalPlayer* InstancesComponent::IULocalPlayer()
{
	__try
	{
		UEngine* engine = IUEngine();
		if (engine && engine->GamePlayers[0])
		{
			return engine->GamePlayers[0];
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
	return nullptr;
}

class APlayerController* InstancesComponent::IAPlayerController()
{
	return I_APlayerController;
}

void InstancesComponent::SetCanvas(class UCanvas* canvas)
{
	if (I_UCanvas != canvas)
	{
		I_UCanvas = canvas;
	}
}

void InstancesComponent::SetHUD(class AHUD* hud)
{
	if (I_AHUD != hud)
	{
		I_AHUD = hud;
	}
}

void InstancesComponent::SetGameViewportClient(class UGameViewportClient* viewportClient)
{
	if (I_UGameViewportClient != viewportClient)
	{
		I_UGameViewportClient = viewportClient;
	}
}

void InstancesComponent::SetPlayerController(class APlayerController* playerController)
{
    I_APlayerController = playerController;

    if (playerController)
    {

        playerController->GetPlayerViewPoint(CachedCameraLocation, CachedCameraRotation);

        CachedCameraFOV = 90.0f;

        if (playerController->PlayerCamera)
        {
            CachedCameraFOV = playerController->PlayerCamera->GetFOVAngle();
        }
        else
        {
            CachedCameraFOV = playerController->GetFOVAngle();
        }

        if (CachedCameraLocation.X == 0 && CachedCameraLocation.Y == 0 && CachedCameraLocation.Z == 0)
        {

            if (playerController->Pawn)
            {
                CachedCameraLocation = playerController->Pawn->Location;
                CachedCameraRotation = playerController->Pawn->Rotation;
            }
        }

        bHasCachedCamera = true;

        static int debugCounter = 0;
        if (debugCounter++ % 120 == 0)
        {
            Console.Write("Camera: Loc(" + std::to_string((int)CachedCameraLocation.X) + "," +
                         std::to_string((int)CachedCameraLocation.Y) + "," +
                         std::to_string((int)CachedCameraLocation.Z) + ") FOV:" + std::to_string(CachedCameraFOV));
        }
    }
    else
    {
        bHasCachedCamera = false;
    }
}

void InstancesComponent::MapObjects()
{
	for (UObject* uObject : *UObject::GObjObjects())
	{
		if (uObject)
		{
			if (uObject->GetPackageName() != FName(L"Transient"))
			{
				std::string objectFullName = uObject->GetFullName();

				if (objectFullName.find("Class ") == 0)
				{
					StaticClasses.emplace(objectFullName, reinterpret_cast<UClass*>(uObject));
				}
				else if (objectFullName.find("Function ") == 0)
				{
					StaticFunctions.emplace(objectFullName, reinterpret_cast<UFunction*>(uObject));
				}
			}
		}
	}
}

class AGameEvent_Soccar_TA* InstancesComponent::GetGameEventAsServer()
{
	return GetInstanceOf<AGameEvent_Soccar_TA>();
}

void InstancesComponent::Initialize()
{
	MapObjects();
	Console.Write(GetNameFormatted() + "instances: init success");
}

class InstancesComponent Instances{};
