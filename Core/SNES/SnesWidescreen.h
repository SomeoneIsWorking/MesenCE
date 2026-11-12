#pragma once

#include <cstdint>

struct SnesWidescreenConfig
{
	bool Enabled = false;

	//Total output width in pixels; 256 keeps the native frame
	uint16_t Width = 384;

	//Layers drawn into the widened margins (BG1 = bit 0, BG2 = bit 1)
	uint8_t WorldLayerMask = 0x03;
};
