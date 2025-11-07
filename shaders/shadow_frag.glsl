#version 450 core

in vec3 vWorldPos;

uniform int uIsPointLight;
uniform vec3 uPointLightPosition;
uniform float uPointLightNear;
uniform float uPointLightFar;

void main()
{
	if (uIsPointLight != 0) {
		float nearPlane = max(uPointLightNear, 0.0001);
		float farPlane = max(uPointLightFar, nearPlane + 0.0001);
		float distanceToLight = length(vWorldPos - uPointLightPosition);
		float normalizedDepth = (distanceToLight - nearPlane) / (farPlane - nearPlane);
		gl_FragDepth = clamp(normalizedDepth, 0.0, 1.0);
	}
}
