#version 450 core

layout(location = 0) in vec2 a_uv; // [0..1] grid

uniform mat4 u_view;
uniform mat4 u_proj;

uniform float u_levelY;
uniform float u_size;
uniform float u_time;
uniform int   u_waveCount; // up to 4

uniform vec2  u_dirs[4];
uniform float u_amps[4];
uniform float u_wavelengths[4];
uniform float u_steepness[4];
uniform float u_speeds[4];

out VS_OUT {
    vec3 worldPos;
    vec3 worldNormal;
} v_out;

const float PI = 3.14159265358979323846;

void main() {
    // Base plane in world XZ
    vec2 xz = (a_uv - 0.5) * u_size;
    vec3 pos = vec3(xz.x, u_levelY, xz.y);

    // Gerstner displacement accumulators
    vec3 disp = vec3(0.0);

    // Jacobians for normal computation (Jx = dP/dx, Jz = dP/dz)
    vec3 Jx = vec3(1.0, 0.0, 0.0);
    vec3 Jz = vec3(0.0, 0.0, 1.0);

    for (int i = 0; i < u_waveCount; ++i) {
        vec2  D = normalize(u_dirs[i]);
        float A = u_amps[i];
        float L = max(0.001, u_wavelengths[i]);
        float k = 2.0 * PI / L;  // wave number
        float w = u_speeds[i];   // phase speed factor

        float f = k * dot(D, xz) - w * u_time; // phase
        float s = sin(f);
        float c = cos(f);

        float Q = u_steepness[i];

        // Gerstner: xz offset scales with Q*A; y with A
        disp.x += Q * A * D.x * c;
        disp.y +=      A       * s;
        disp.z += Q * A * D.y * c;

        float WA = k * A; // for derivatives

        // Jx = dP/dx; Jz = dP/dz
        // d/dx of phase f is k * D.x; d/dz is k * D.y
        // Derivatives from analytical Gerstner formulation
        Jx.x += -Q * WA * D.x * D.x * s; // d/dx of x component
        Jx.y +=      WA * D.x * c;       // d/dx of y component
        Jx.z += -Q * WA * D.x * D.y * s; // d/dx of z component

        Jz.x += -Q * WA * D.x * D.y * s; // d/dz of x component
        Jz.y +=      WA * D.y * c;       // d/dz of y component
        Jz.z += -Q * WA * D.y * D.y * s; // d/dz of z component
    }

    vec3 worldPos = pos + disp;

    // Normal from Jacobians
    vec3 N = normalize(cross(Jz, Jx));

    v_out.worldPos = worldPos;
    v_out.worldNormal = N;

    gl_Position = u_proj * u_view * vec4(worldPos, 1.0);
}
