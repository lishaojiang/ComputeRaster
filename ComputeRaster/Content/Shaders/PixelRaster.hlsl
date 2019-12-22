//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Common.hlsli"
#define main PSMain
#include "PixelShader.hlsl"
#undef main

//--------------------------------------------------------------------------------------
// Buffers
//--------------------------------------------------------------------------------------
Buffer<float4> g_roVertexPos : register (t0);
StructuredBuffer<TiledPrim> g_roTiledPrimitives : register (t1);
Buffer<float3> g_roVertexNrm : register (t2);

//--------------------------------------------------------------------------------------
// UAV buffers
//--------------------------------------------------------------------------------------
RWTexture2D<float4>	g_rwRenderTarget;
RWTexture2D<uint>	g_rwDepth;

//--------------------------------------------------------------------------------------
// Check if the tile is overlapped by a straight line with a certain thickness.
//--------------------------------------------------------------------------------------
bool Overlaps(float2 pixelPos, float4 v[3], out float3 w)
{
	// Triangle edge equation setup.
	const float a01 = v[0].y - v[1].y;
	const float b01 = v[1].x - v[0].x;
	const float a12 = v[1].y - v[2].y;
	const float b12 = v[2].x - v[1].x;
	const float a20 = v[2].y - v[0].y;
	const float b20 = v[0].x - v[2].x;

	// Calculate barycentric coordinates at min corner.
	const float2 minPoint = min(v[0].xy, min(v[1].xy, v[2].xy));
	w.x = determinant(v[1].xy, v[2].xy, minPoint);
	w.y = determinant(v[2].xy, v[0].xy, minPoint);
	w.z = determinant(v[0].xy, v[1].xy, minPoint);

	// If pixel is inside of all edges, set pixel.
	const float2 dist = pixelPos - minPoint;
	w.x += (a12 * dist.x) + (b12 * dist.y);
	w.y += (a20 * dist.x) + (b20 * dist.y);
	w.z += (a01 * dist.x) + (b01 * dist.y);

	return w.x >= 0.0 && w.y >= 0.0 && w.z >= 0.0;
}

[numthreads(8, 8, 1)]
void main(uint2 GTid : SV_GroupThreadID, uint Gid : SV_GroupID)//, uint GTidx : SV_GroupIndex)
{
	const TiledPrim tiledPrim = g_roTiledPrimitives[Gid];
	const uint2 tile = uint2(tiledPrim.TileIdx % g_tileDim.x, tiledPrim.TileIdx / g_tileDim.x);

	float4 primVPos[3];

	// Load the vertex positions of the triangle
	const uint baseVIdx = tiledPrim.PrimId * 3;
	[unroll]
	for (uint i = 0; i < 3; ++i) primVPos[i] = g_roVertexPos[baseVIdx + i];

	// To screen space.
	ToScreenSpace(primVPos);

	PSIn input;
	float3 w;
	const uint2 pixelPos = (tile << 3) + GTid;
	input.Pos.xy = pixelPos + 0.5;
	if (!Overlaps(input.Pos.xy, primVPos, w)) return;

	// Normalize barycentric coordinates.
	const float area = determinant(primVPos[0].xy, primVPos[1].xy, primVPos[2].xy);
	if (area <= 0.0) return;
	w /= area;

	// Depth test
	uint depthMin;
	input.Pos.z = w.x * primVPos[0].z + w.y * primVPos[1].z + w.z * primVPos[2].z;
	const uint depth = asuint(input.Pos.z);
	InterlockedMin(g_rwDepth[pixelPos], asuint(input.Pos.z), depthMin);
	if (depth > depthMin) return;

	// Interpolations
	const float rhw = w.x * primVPos[0].w + w.y * primVPos[1].w + w.z * primVPos[2].w;
	input.Pos.w = 1.0 / rhw;

	float3 primVAtt[3];
	[unroll]
	for (i = 0; i < 3; ++i) primVAtt[i] = g_roVertexNrm[baseVIdx + i];
	[unroll]
	for (i = 0; i < 3; ++i) primVAtt[i] *= primVPos[i].w;
	const float3 nrm = w.x * primVAtt[0] + w.y * primVAtt[1] + w.z * primVAtt[2];
	input.Nrm = nrm * input.Pos.w;

	//g_rwRenderTarget[pixelPos] = float4(w, 1.0);
	g_rwRenderTarget[pixelPos] = PSMain(input);
}