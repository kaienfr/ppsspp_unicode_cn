// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/GLES_GPU.h"
#include "GPU/Null/NullGpu.h"
#include "GPU/Software/SoftGpu.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"

GPUgstate gstate;
GPUStateCache gstate_c;
GPUInterface *gpu;
GPUStatistics gpuStats;

bool GPU_Init() {
	switch (PSP_CoreParameter().gpuCore) {
	case GPU_NULL:
		gpu = new NullGPU();
		break;
	case GPU_GLES:
		gpu = new GLES_GPU();
		break;
	case GPU_SOFTWARE:
#ifndef __SYMBIAN32__
		gpu = new SoftGPU();
#endif
		break;
	case GPU_DIRECTX9:
		// TODO
		break;
	}

	return gpu != NULL;
}

void GPU_Shutdown() {
	delete gpu;
	gpu = 0;
}

void InitGfxState()
{
	memset(&gstate, 0, sizeof(gstate));
	memset(&gstate_c, 0, sizeof(gstate_c));
	for (int i = 0; i < 256; i++) {
		gstate.cmdmem[i] = i << 24;
	}

	gstate.lightingEnable = 0x17000001;

	static const float identity4x3[12] =
	{1,0,0,
 	 0,1,0,
 	 0,0,1,
	 0,0,0,};
	static const float identity4x4[16] =
	{1,0,0,0,
	 0,1,0,0,
	 0,0,1,0,
	 0,0,0,1};

	memcpy(gstate.worldMatrix, identity4x3, 12 * sizeof(float));
	memcpy(gstate.viewMatrix, identity4x3, 12 * sizeof(float));
	memcpy(gstate.projMatrix, identity4x4, 16 * sizeof(float));
	memcpy(gstate.tgenMatrix, identity4x3, 12 * sizeof(float));
	for (int i = 0; i < 8; i++) {
		memcpy(gstate.boneMatrix + i * 12, identity4x3, 12 * sizeof(float));
	}
}

void ShutdownGfxState()
{
}

// When you have changed state outside the psp gfx core,
// or saved the context and has reloaded it, call this function.
void ReapplyGfxState()
{
	if (!gpu)
		return;
	gpu->ReapplyGfxState();
}
