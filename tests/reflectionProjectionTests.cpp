#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace dx = DirectX;

namespace {
	bool nearlyEqual(float left, float right, float tolerance = 0.0001f) {
		return std::abs(left - right) <= tolerance;
	}

	dx::XMFLOAT2 projectedNdc(
		dx::FXMVECTOR point,
		dx::FXMMATRIX view,
		dx::CXMMATRIX projection) {
		const dx::XMVECTOR clip = dx::XMVector4Transform(
			point, dx::XMMatrixMultiply(view, projection));
		dx::XMFLOAT4 value;
		dx::XMStoreFloat4(&value, clip);
		return { value.x / value.w, value.y / value.w };
	}
}

int main() {
	const std::array<float, 4> planeHeights = { -3.25f, 0.0f, 7.5f, 63.875f };
	const std::array<dx::XMFLOAT3, 4> directions = {
		dx::XMFLOAT3{ 0.0f, -0.12f, 1.0f },
		dx::XMFLOAT3{ 0.55f, -0.30f, 1.0f },
		dx::XMFLOAT3{ -0.72f, -0.48f, 0.65f },
		dx::XMFLOAT3{ 0.18f, 0.22f, 1.0f }
	};
	const dx::XMMATRIX projection = dx::XMMatrixPerspectiveFovLH(
		dx::XMConvertToRadians(75.0f), 16.0f / 9.0f, 0.05f, 512.0f);

	for (float planeHeight : planeHeights) {
		for (const dx::XMFLOAT3& directionValue : directions) {
			const dx::XMFLOAT3 eye{ 2.25f, planeHeight + 3.0f, -6.5f };
			const dx::XMVECTOR direction = dx::XMVector3Normalize(
				dx::XMLoadFloat3(&directionValue));
			const dx::XMMATRIX view = dx::XMMatrixLookToLH(
				dx::XMLoadFloat3(&eye), direction, dx::XMVectorSet(0, 1, 0, 0));
			const dx::XMMATRIX reflection = dx::XMMatrixReflect(
				dx::XMVectorSet(0, 1, 0, -planeHeight));
			const dx::XMMATRIX reflectedView = reflection * view;

			// A point on the reflecting plane is invariant under the reflection.
			// Therefore its projective lookup must match the main-camera pixel.
			const dx::XMVECTOR surfacePoint = dx::XMVectorSet(
				-1.75f, planeHeight, 4.25f, 1.0f);
			const dx::XMFLOAT2 mainNdc = projectedNdc(surfacePoint, view, projection);
			const dx::XMFLOAT2 reflectionNdc = projectedNdc(
				surfacePoint, reflectedView, projection);
			if (!nearlyEqual(mainNdc.x, reflectionNdc.x) ||
				!nearlyEqual(mainNdc.y, reflectionNdc.y)) {
				std::cerr << "water-plane projection moved after reflection\n";
				return 1;
			}
		}

		// The virtual image is below the plane. The sightline from the player to
		// that image must cross the water between the player and the real object.
		const dx::XMFLOAT3 player{ 0.0f, planeHeight + 2.0f, 0.0f };
		const dx::XMFLOAT3 object{ 0.0f, planeHeight + 3.0f, 12.0f };
		const float virtualObjectY = 2.0f * planeHeight - object.y;
		const float intersectionT =
			(planeHeight - player.y) / (virtualObjectY - player.y);
		const float reflectionZ = player.z + (object.z - player.z) * intersectionT;
		if (!(intersectionT > 0.0f && intersectionT < 1.0f &&
			reflectionZ > player.z && reflectionZ < object.z)) {
			std::cerr << "water reflection landed beyond the reflected object\n";
			return 1;
		}
	}

	// Exercise the complete planar lookup geometry, not merely a point already
	// on the plane. The reflected render of an object must land at the main-camera
	// pixel where the eye-to-virtual-object ray crosses the water.
	{
		constexpr float planeHeight = 2.0f;
		const dx::XMFLOAT3 eye{ 1.25f, 5.5f, -4.0f };
		const dx::XMFLOAT3 object{ -0.75f, 5.0f, 14.0f };
		const dx::XMVECTOR look = dx::XMVector3Normalize(
			dx::XMVectorSet(0.03f, -0.18f, 1.0f, 0.0f));
		const dx::XMMATRIX view = dx::XMMatrixLookToLH(
			dx::XMLoadFloat3(&eye), look, dx::XMVectorSet(0, 1, 0, 0));
		const dx::XMMATRIX reflection = dx::XMMatrixReflect(
			dx::XMVectorSet(0, 1, 0, -planeHeight));
		const dx::XMMATRIX reflectedView = reflection * view;
		const dx::XMFLOAT3 virtualObject{
			object.x, 2.0f * planeHeight - object.y, object.z
		};
		const float intersectionT = (planeHeight - eye.y) /
			(virtualObject.y - eye.y);
		const dx::XMVECTOR waterIntersection = dx::XMVectorSet(
			eye.x + (virtualObject.x - eye.x) * intersectionT,
			planeHeight,
			eye.z + (virtualObject.z - eye.z) * intersectionT,
			1.0f);
		const dx::XMFLOAT2 renderedReflection = projectedNdc(
			dx::XMVectorSet(object.x, object.y, object.z, 1.0f),
			reflectedView, projection);
		const dx::XMFLOAT2 physicalIntersection = projectedNdc(
			waterIntersection, view, projection);
		// Model an animated water fragment on the very same camera ray. Recovering
		// the plane intersection from that ray must retain the physical lookup.
		const float animatedRayScale = 0.965f;
		const dx::XMVECTOR eyeVector = dx::XMLoadFloat3(&eye);
		const dx::XMVECTOR displacedSurface = dx::XMVectorLerp(
			eyeVector, waterIntersection, animatedRayScale);
		dx::XMFLOAT3 displacedSurfaceValue;
		dx::XMStoreFloat3(&displacedSurfaceValue, displacedSurface);
		const float recoveredT = (planeHeight - eye.y) /
			(displacedSurfaceValue.y - eye.y);
		const dx::XMVECTOR recoveredPlanePoint = dx::XMVectorAdd(
			eyeVector,
			dx::XMVectorScale(dx::XMVectorSubtract(displacedSurface, eyeVector), recoveredT));
		const dx::XMFLOAT2 planePointLookup = projectedNdc(
			recoveredPlanePoint, reflectedView, projection);

		// Reproduce the screenshot failure: dropping the animated fragment
		// vertically reaches the plane on a different camera ray.
		const dx::XMVECTOR verticallyFlattened = dx::XMVectorSetY(
			displacedSurface, planeHeight);
		const dx::XMFLOAT2 verticallyFlattenedLookup = projectedNdc(
			verticallyFlattened, reflectedView, projection);
		if (!nearlyEqual(renderedReflection.x, physicalIntersection.x) ||
			!nearlyEqual(renderedReflection.y, physicalIntersection.y) ||
			!nearlyEqual(planePointLookup.x, physicalIntersection.x) ||
			!nearlyEqual(planePointLookup.y, physicalIntersection.y) ||
			(nearlyEqual(verticallyFlattenedLookup.x, physicalIntersection.x) &&
				nearlyEqual(verticallyFlattenedLookup.y, physicalIntersection.y)) ||
			!(intersectionT > 0.0f && intersectionT < 1.0f)) {
			std::cerr << "ray-plane lookup misses the camera-to-object intersection\n";
			return 1;
		}
	}

	std::ifstream shaderFile("assets/shader/staticPixel.hlsl");
	std::ostringstream shaderContents;
	shaderContents << shaderFile.rdbuf();
	const std::string shader = shaderContents.str();
	if (shader.empty() || shader.find("1.0 - screenUv.x") != std::string::npos) {
		std::cerr << "invalid mirrored-screen fallback is present\n";
		return 1;
	}
	if (shader.find("refinement < 4u") == std::string::npos ||
		shader.find("previousDelta < 0.0 && depthDelta >= 0.0") == std::string::npos) {
		std::cerr << "grazing-angle SSR crossing refinement is missing\n";
		return 1;
	}
	if (shader.find("float4(planePoint, 1.0), planarReflectionViewProjection") ==
		std::string::npos ||
		shader.find("-planarNdc.y * 0.5 + 0.5") == std::string::npos) {
		std::cerr << "planar reflection does not project the geometric plane point\n";
		return 1;
	}
	if (shader.find("(planarReflectionPlaneHeight - cameraPosition.y) / planeRayDenominator") ==
		std::string::npos ||
		shader.find("cameraPosition + cameraToSurface * planeIntersection") == std::string::npos ||
		shader.find("float3(input.worldPosition.x, planarReflectionPlaneHeight") !=
		std::string::npos) {
		std::cerr << "animated water is not intersected with the reflection plane along the camera ray\n";
		return 1;
	}
	if (shader.find("planarUv = screenUv") != std::string::npos ||
		shader.find("planarUv +=") != std::string::npos) {
		std::cerr << "planar reflection is displaced away from its physical intersection\n";
		return 1;
	}
	if (shader.find("if ((!isWater || !waterPlanarSurface) && traceVoxelReflection(") ==
		std::string::npos ||
		shader.find("reflectionSurfacePosition, geometricReflectionNormal, tracedReflectionDirection") ==
		std::string::npos ||
		shader.find("viewTraceSurface, viewTraceReflected, sceneSize") == std::string::npos) {
		std::cerr << "water reflection fallbacks do not originate on the geometric plane\n";
		return 1;
	}
	if (shader.find("if (!usedPlanarReflection && traceScreenReflection(") ==
		std::string::npos ||
		shader.find("if (!waterPlanarSurface)") == std::string::npos) {
		std::cerr << "water SSR can overwrite a valid planar reflection\n";
		return 1;
	}
	if (shader.find("reflectionColor = planarColor;") == std::string::npos) {
		std::cerr << "valid planar reflection retains a misplaced fallback ghost\n";
		return 1;
	}
	if (shader.find("planarReflectionTexture.SampleGrad(") == std::string::npos ||
		shader.find("const float2 waveWarp") == std::string::npos ||
		shader.find("const float2 warpedUv = clamp(safePlanarUv + waveWarp") ==
		std::string::npos ||
		shader.find("refractionSampler, warpedUv, ddx(warpedUv), ddy(warpedUv)") ==
		std::string::npos ||
		shader.find("viewNormal.xy * lerp(0.002") != std::string::npos ||
		shader.find("const float3 planarSoft = planarReflectionTexture.SampleLevel(") ==
		std::string::npos ||
		shader.find("const float2 warpedHitUv") == std::string::npos ||
		shader.find("spreadPixels") != std::string::npos ||
		shader.find("emissiveWarmMask") != std::string::npos ||
		shader.find("ssrRadius") != std::string::npos) {
		std::cerr << "wave-warped planar sampling is missing or max-filter ghosts remain\n";
		return 1;
	}
	if (shader.find("isWater ? 1.0 : roughness") == std::string::npos ||
		shader.find("isWater ? 0.0 : metallic") == std::string::npos) {
		std::cerr << "water local lights are not forced to a rough non-metallic response\n";
		return 1;
	}
	if (shader.find("float3 emittedLighting = isWater") == std::string::npos ||
		shader.find("if (isWater) continue;") == std::string::npos) {
		std::cerr << "water still receives a non-geometric glowstone light blob\n";
		return 1;
	}
	if (shader.find("sampleAnimatedTextureAlpha(\n\t\t\t\tinput.material") ==
		std::string::npos ||
		shader.find("const float glassPattern = blockTextures[12]") !=
		std::string::npos) {
		std::cerr << "glass alpha is not sampled from its assigned material slot\n";
		return 1;
	}
	if (shader.find("dot(reflectedHitPosition - reflectionSurfacePosition") ==
		std::string::npos ||
		shader.find("reflectiveSide > 0.08") == std::string::npos ||
		shader.find("24u, 0.62") == std::string::npos ||
		shader.find("glassSsrConfidence") == std::string::npos ||
		shader.find("validGlassTransmission") == std::string::npos ||
		shader.find("frontalReflection") != std::string::npos) {
		std::cerr << "glass reflections still mix voxel ghosts or milky face-on overlay\n";
		return 1;
	}
	if (shader.find("materialEmissions[reflectedMaterial]") == std::string::npos) {
		std::cerr << "voxel reflections discard emissive material radiance\n";
		return 1;
	}
	if (shader.find("bool validRefractedSample") == std::string::npos ||
		shader.find("refractedSampleViewZ > viewSurface.z + 0.001") == std::string::npos ||
		shader.find("isWater ? waterTime >= 0.0 : dot(N, V) >= 0.0") ==
		std::string::npos ||
		shader.find("const float minimumPixels = lerp(0.45, 1.35, grazingAmount)") ==
		std::string::npos ||
		shader.find("transmissionAttempt < 3u && !validRefractedSample") ==
		std::string::npos ||
		shader.find("const float2 transmissionUv = validRefractedSample") == std::string::npos ||
		shader.find("cameraPosition.y >= planarReflectionPlaneHeight") == std::string::npos) {
		std::cerr << "refraction / underwater planar exit path is incomplete\n";
		return 1;
	}
	if (shader.find("float transmissionRayTravel(") == std::string::npos ||
		shader.find("max(2048.0, abs(viewSurface.z) * 8.0)") == std::string::npos ||
		shader.find("transmissionRayTravel(\n\t\t\t\tviewSurface, viewRefracted, opaqueDepth)") ==
		std::string::npos ||
		shader.find("), 96.0)") != std::string::npos ||
		shader.find("), 24.0)") != std::string::npos) {
		std::cerr << "refraction still has a fixed world-distance cutoff\n";
		return 1;
	}

	std::ifstream applicationFile("source/app/VoxelApplication.cpp");
	std::ostringstream applicationContents;
	applicationContents << applicationFile.rdbuf();
	const std::string application = applicationContents.str();
	if (application.find("fluidLevel == ac::MAX_FLUID_LEVEL") == std::string::npos ||
		application.find("ac::MAX_FLUID_LEVEL - 1u") == std::string::npos ||
		application.find("const float surfaceHeight = static_cast<float>(worldY) + visibleFill") ==
		std::string::npos) {
		std::cerr << "reflection plane does not match the exposed source-water mesh height\n";
		return 1;
	}
	if (application.find("colorDescription.MipLevels = 0") == std::string::npos ||
		application.find("D3D11_RESOURCE_MISC_GENERATE_MIPS") == std::string::npos ||
		application.find("context->GenerateMips(shaderView.Get())") == std::string::npos ||
		application.find("D3D11_FILTER_MIN_MAG_MIP_LINEAR") == std::string::npos ||
		application.find("requestedWidth / 2u") != std::string::npos) {
		std::cerr << "planar reflection mip chain, trilinear filtering, or full-resolution target is missing\n";
		return 1;
	}

	std::cout << "reflection placement, glass material, emissive, and SSR checks passed\n";
	return 0;
}
