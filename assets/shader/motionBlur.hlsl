cbuffer MotionBlurBuffer : register(b0)
{
	float4x4 previousViewProjection;
	float4x4 inverseViewProjection;
	float2 texelSize;
	float blurStrength;
	float enabled;
};

Texture2D sceneColor : register(t0);
Texture2D sceneDepth : register(t1);
SamplerState linearClamp : register(s0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PSInput vertexMain(uint id : SV_VertexID)
{
	PSInput output;
	output.uv = float2((id << 1) & 2, id & 2);
	output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return output;
}

float3 reconstructWorld(float2 uv, float depth)
{
	const float nearPlane = 0.01;
	float w = nearPlane / max(1.0 - depth, 1e-5);
	float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float4 clip = float4(ndc * w, depth * w, w);
	float4 world = mul(clip, inverseViewProjection);
	return world.xyz / max(world.w, 1e-5);
}

float4 pixelMain(PSInput input) : SV_TARGET
{
	float4 center = sceneColor.SampleLevel(linearClamp, input.uv, 0);
	if (enabled < 0.5)
		return center;

	float depth = sceneDepth.SampleLevel(linearClamp, input.uv, 0).r;
	if (depth >= 0.9999)
		return center;

	float3 world = reconstructWorld(input.uv, depth);
	float4 previousClip = mul(float4(world, 1.0), previousViewProjection);
	float invW = rcp(max(abs(previousClip.w), 1e-5));
	float2 previousUv = previousClip.xy * invW * float2(0.5, -0.5) + 0.5;
	float2 velocity = (input.uv - previousUv) * blurStrength;
	float speed = length(velocity / max(texelSize, 1e-5));
	if (speed < 0.35)
		return center;

	velocity = clamp(velocity, -texelSize * 24.0, texelSize * 24.0);
	const int taps = 5;
	float3 color = center.rgb;
	float weight = 1.0;
	[unroll]
	for (int i = 1; i <= taps; ++i)
	{
		float t = float(i) / float(taps);
		float w = 1.0 - t;
		float2 offset = velocity * t;
		color += sceneColor.SampleLevel(linearClamp, saturate(input.uv + offset), 0).rgb * w;
		color += sceneColor.SampleLevel(linearClamp, saturate(input.uv - offset), 0).rgb * w;
		weight += 2.0 * w;
	}
	return float4(color / weight, center.a);
}
