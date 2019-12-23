//--------------------------------------------------------------------------------------
// Copyright (c) XU, Tianchen. All rights reserved.
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct PSIn
{
	float4	Pos	: SV_POSITION;
	float3	Nrm	: NORMAL;
};

//--------------------------------------------------------------------------------------
// Vertex shader
//--------------------------------------------------------------------------------------
float4 main(PSIn input) : SV_TARGET
{
	const float3 L = normalize(float3(1.0, 1.0, -1.0));
	const float3 N = normalize(input.Nrm);

	const float lightAmt = saturate(dot(N, L));
	const float ambient = N.y * 0.5 + 0.5;

	return float4(lightAmt.xxx + ambient * 0.2, 1.0);
}
