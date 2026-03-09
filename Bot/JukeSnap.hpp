#pragma once
#include <atomic>
#include <cstdint>

struct JukeCarSnap {
	float Px,Py,Pz;
	float Vx,Vy,Vz;
	float AVx,AVy,AVz;
	int32_t Pitch,Yaw,Roll;
	float Boost;
	uint8_t Team;
	uint8_t OnGround;
	uint8_t Jumped;
	uint8_t DoubleJumped;
	uint8_t Demoed;
	uint8_t Pad[3];
};

struct JukeBallSnap {
	float Px,Py,Pz;
	float Vx,Vy,Vz;
	float AVx,AVy,AVz;
	int32_t Pitch,Yaw,Roll;
};

struct alignas(64) JukeSnap {
	std::atomic<uint32_t> Seq{0};
	int32_t Count{0};
	int32_t Local{0};
	JukeCarSnap Cars[64]{};
	JukeBallSnap Ball{};
};

extern JukeSnap g_JukeSnap;
