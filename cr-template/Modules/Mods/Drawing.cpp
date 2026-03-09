#include "Drawing.hpp"
#include "../Components/Includes.hpp"

#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"

#include <DirectXMath.h>
#pragma comment(lib,"DirectXTK.lib")

#define UCONST_Pi 3.1415926
#define URotation180  32768 
#define URotationToRadians  UCONST_Pi / URotation180 

Drawing::Drawing(const std::string& name, const std::string& description, uint32_t states) : Module(name, description, states) {}
Drawing::~Drawing() {}



FVector RotationToVector(FRotator R)
{
	FVector Vec;
	float fYaw = R.Yaw * URotationToRadians;
	float fPitch = R.Pitch * URotationToRadians;
	float CosPitch = cos(fPitch);
	Vec.X = cos(fYaw) * CosPitch;
	Vec.Y = sin(fYaw) * CosPitch;
	Vec.Z = sin(fPitch);

	return Vec;
}

float Size(FVector& v)
{
	return sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
}

void Normalize(FVector& v)
{
	float size = Size(v);

	if (!size)
	{
		v.X = v.Y = v.Z = 1;
	}
	else
	{
		v.X /= size;
		v.Y /= size;
		v.Z /= size;
	}
}

void inline GetAxes(FRotator R, FVector& X, FVector& Y, FVector& Z)
{
	X = RotationToVector(R);
	Normalize(X);
	R.Yaw += 16384;
	FRotator R2 = R;
	R2.Pitch = 0.f;
	Y = RotationToVector(R2);
	Normalize(Y);
	Y.Z = 0.f;
	R.Yaw -= 16384;
	R.Pitch += 16384;
	Z = RotationToVector(R);
	Normalize(Z);
}

FLOAT VectorDotProduct(FVector* pV1, FVector* pV2)
{
	return ((pV1->X * pV2->X) + (pV1->Y * pV2->Y) + (pV1->Z * pV2->Z));
}
FVector* VectorSubtract(FVector* pOut, FVector* pV1, FVector* pV2)
{
	pOut->X = pV1->X - pV2->X;
	pOut->Y = pV1->Y - pV2->Y;
	pOut->Z = pV1->Z - pV2->Z;

	return pOut;
}

// Cache the camera to avoid searching every frame
static ACamera_TA* cachedCamera = nullptr;
static int cameraRefreshCounter = 0;

static bool GetPlayerCameraData(FVector& outLoc, FRotator& outRot, float& outFOV)
{
    // Refresh camera every 60 frames (once per second at 60fps)
    if (!cachedCamera || cameraRefreshCounter++ > 60)
    {
        std::vector<ACamera_TA*> cameras = Instances.GetAllInstancesOf<ACamera_TA>();
        cachedCamera = nullptr;
        
        for (ACamera_TA* camera : cameras)
        {
            if (camera && camera->Location.X != 0 && camera->Location.Y != 0)
            {
                cachedCamera = camera;
                break;
            }
        }
        cameraRefreshCounter = 0;
    }
    
    if (cachedCamera)
    {
        outLoc = cachedCamera->Location;
        outRot = cachedCamera->Rotation;
        outFOV = 90.0f;
        return true;
    }
    
    // Fallback: try to get camera from player controller
    if (APlayerController* pc = Instances.IAPlayerController())
    {
        if (pc->Pawn)
        {
            outLoc = pc->Pawn->Location;
            outRot = pc->Pawn->Rotation;
            outFOV = 90.0f;
            return true;
        }
    }
    
    return false;
}
// =====================================================================

FVector Drawing::CalculateScreenCoordinate(FVector worldPos)
{
    FVector camLoc;  FRotator camRot;  float fovDeg = 90.f;
    if (!GetPlayerCameraData(camLoc, camRot, fovDeg))
        return FVector(-1,-1,-1);

    using namespace DirectX;

    /* ---- get Unreal forward/up ------------------------------------ */
    FVector axisF, axisR, axisU;
    GetAxes(camRot, axisF, axisR, axisU);           // axisF = +X forward, axisU = +Z up

    XMVECTOR eye   = XMVectorSet(camLoc.X, camLoc.Y, camLoc.Z, 1.f);
    XMVECTOR dir   = XMVectorSet(axisF.X, axisF.Y, axisF.Z, 0.f);
    XMVECTOR up    = XMVectorSet(axisU.X, axisU.Y, axisU.Z, 0.f);

    /* ---- LEFT-handed view & projection ---------------------------- */
    XMMATRIX view = XMMatrixLookToLH(eye, dir, up);

    float aspect  = (GUI.DisplayY == 0) ? 1.f : (GUI.DisplayX / GUI.DisplayY);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
                        XMConvertToRadians(fovDeg),
                        aspect,
                        1.f,               // near
                        100000.f);         // far

    /* ---- project --------------------------------------------------- */
    XMVECTOR clip = XMVector3Project(
        XMVectorSet(worldPos.X, worldPos.Y, worldPos.Z, 1.f),
        0, 0, GUI.DisplayX, GUI.DisplayY,
        0.0f, 1.0f,
        proj, view, XMMatrixIdentity());

    float sx = XMVectorGetX(clip);
    float sy = XMVectorGetY(clip);
    float sz = XMVectorGetZ(clip);

    if (sz <= 0.f || sz > 1.f) return FVector(-1,-1,-1);   // behind camera / off-screen
    return FVector(sx, sy, 1.f);
}

