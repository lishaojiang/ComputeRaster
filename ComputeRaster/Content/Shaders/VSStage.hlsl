//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "VSStage.hlsli"

//--------------------------------------------------------------------------------------
// Fetch shader
//--------------------------------------------------------------------------------------
void FetchShader(uint id, out VSIn result)
{
	result = g_roVertexBuffer[id];
}