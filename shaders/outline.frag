#version 450 core

in vec2 v_uv;
out vec4 FragColor;

uniform sampler2D u_sceneColor;
uniform sampler2D u_sceneDepth;

uniform bool u_outlineEnabled;
uniform float u_outlineStrength;
uniform float u_depthThreshold;
uniform float u_normalThreshold;
uniform bool u_useNormalEdges;
uniform bool u_previewEdgeMask;

uniform float u_nearPlane;
uniform float u_farPlane;
uniform vec2 u_texelSize;

// Linearize depth from [0,1] hardware depth to view-space Z
float linearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0; // Back to NDC
    return (2.0 * u_nearPlane * u_farPlane) / (u_farPlane + u_nearPlane - z * (u_farPlane - u_nearPlane));
}

// Normalize linear depth to [0,1] range for consistent thresholding
float normalizeLinearDepth(float linearDepth) {
    return (linearDepth - u_nearPlane) / (u_farPlane - u_nearPlane);
}

// Reconstruct view-space position from depth
vec3 reconstructViewPos(vec2 uv, float depth) {
    float z = depth * 2.0 - 1.0; // to NDC
    vec4 clipPos = vec4(uv * 2.0 - 1.0, z, 1.0);
    
    // Simplified inverse projection (assumes symmetric perspective)
    float aspect = 1.0 / u_texelSize.x * u_texelSize.y;
    float tanHalfFov = tan(radians(45.0) * 0.5); // Assume 45 degree FOV
    
    vec3 viewPos;
    viewPos.z = -linearizeDepth(depth);
    viewPos.x = clipPos.x * (-viewPos.z) * tanHalfFov * aspect;
    viewPos.y = clipPos.y * (-viewPos.z) * tanHalfFov;
    
    return viewPos;
}

// Reconstruct normal from depth using cross product of view-space derivatives
vec3 reconstructNormal(vec2 uv) {
    float c = texture(u_sceneDepth, uv).r;
    float r = texture(u_sceneDepth, uv + vec2(u_texelSize.x, 0.0)).r;
    float t = texture(u_sceneDepth, uv + vec2(0.0, u_texelSize.y)).r;
    
    vec3 pc = reconstructViewPos(uv, c);
    vec3 pr = reconstructViewPos(uv + vec2(u_texelSize.x, 0.0), r);
    vec3 pt = reconstructViewPos(uv + vec2(0.0, u_texelSize.y), t);
    
    vec3 dx = pr - pc;
    vec3 dy = pt - pc;
    
    return normalize(cross(dx, dy));
}

// Sobel operator for edge detection on depth
float sobelDepth(vec2 uv) {
    // Sample 3x3 neighborhood
    float tl = linearizeDepth(texture(u_sceneDepth, uv + vec2(-1, -1) * u_texelSize).r);
    float tc = linearizeDepth(texture(u_sceneDepth, uv + vec2( 0, -1) * u_texelSize).r);
    float tr = linearizeDepth(texture(u_sceneDepth, uv + vec2( 1, -1) * u_texelSize).r);
    
    float ml = linearizeDepth(texture(u_sceneDepth, uv + vec2(-1,  0) * u_texelSize).r);
    float mr = linearizeDepth(texture(u_sceneDepth, uv + vec2( 1,  0) * u_texelSize).r);
    
    float bl = linearizeDepth(texture(u_sceneDepth, uv + vec2(-1,  1) * u_texelSize).r);
    float bc = linearizeDepth(texture(u_sceneDepth, uv + vec2( 0,  1) * u_texelSize).r);
    float br = linearizeDepth(texture(u_sceneDepth, uv + vec2( 1,  1) * u_texelSize).r);
    
    // Normalize to [0,1] for consistent thresholding
    tl = normalizeLinearDepth(tl);
    tc = normalizeLinearDepth(tc);
    tr = normalizeLinearDepth(tr);
    ml = normalizeLinearDepth(ml);
    mr = normalizeLinearDepth(mr);
    bl = normalizeLinearDepth(bl);
    bc = normalizeLinearDepth(bc);
    br = normalizeLinearDepth(br);
    
    // Sobel kernels
    // Horizontal: [-1 0 1; -2 0 2; -1 0 1]
    // Vertical:   [-1 -2 -1; 0 0 0; 1 2 1]
    float gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    
    return sqrt(gx*gx + gy*gy);
}

// Sobel operator for edge detection on normals
float sobelNormal(vec2 uv) {
    // Sample center normal
    vec3 nc = reconstructNormal(uv);
    
    // Sample 3x3 neighborhood and compute differences
    vec3 tl = reconstructNormal(uv + vec2(-1, -1) * u_texelSize);
    vec3 tc = reconstructNormal(uv + vec2( 0, -1) * u_texelSize);
    vec3 tr = reconstructNormal(uv + vec2( 1, -1) * u_texelSize);
    
    vec3 ml = reconstructNormal(uv + vec2(-1,  0) * u_texelSize);
    vec3 mr = reconstructNormal(uv + vec2( 1,  0) * u_texelSize);
    
    vec3 bl = reconstructNormal(uv + vec2(-1,  1) * u_texelSize);
    vec3 bc = reconstructNormal(uv + vec2( 0,  1) * u_texelSize);
    vec3 br = reconstructNormal(uv + vec2( 1,  1) * u_texelSize);
    
    // Compute 1 - dot(center, neighbor) for each sample (0 = same direction, 1 = opposite)
    float dtl = 1.0 - dot(nc, tl);
    float dtc = 1.0 - dot(nc, tc);
    float dtr = 1.0 - dot(nc, tr);
    float dml = 1.0 - dot(nc, ml);
    float dmr = 1.0 - dot(nc, mr);
    float dbl = 1.0 - dot(nc, bl);
    float dbc = 1.0 - dot(nc, bc);
    float dbr = 1.0 - dot(nc, br);
    
    // Apply Sobel kernel
    float gx = -dtl - 2.0*dml - dbl + dtr + 2.0*dmr + dbr;
    float gy = -dtl - 2.0*dtc - dtr + dbl + 2.0*dbc + dbr;
    
    return sqrt(gx*gx + gy*gy);
}

void main() {
    vec3 sceneColor = texture(u_sceneColor, v_uv).rgb;
    
    if (!u_outlineEnabled) {
        FragColor = vec4(sceneColor, 1.0);
        return;
    }
    
    // Compute depth edge
    float depthEdge = sobelDepth(v_uv);
    
    // Compute normal edge if enabled
    float normalEdge = 0.0;
    if (u_useNormalEdges) {
        normalEdge = sobelNormal(v_uv);
    }
    
    // Threshold each edge type separately
    float depthMask = (depthEdge > u_depthThreshold) ? 1.0 : 0.0;
    float normalMask = (normalEdge > u_normalThreshold) ? 1.0 : 0.0;
    
    // Combine edges: 60% depth weight, 40% normal weight
    float edge = max(depthMask * 0.6, normalMask * 0.4);
    
    // Apply edge to scene color
    if (u_previewEdgeMask) {
        // Debug: show edge mask (red=depth, green=normal, yellow=both)
        vec3 debugColor = vec3(depthMask, normalMask, 0.0);
        FragColor = vec4(debugColor, 1.0);
    } else {
        // Darken scene color at edges
        float darkening = 1.0 - (edge * u_outlineStrength);
        vec3 outlinedColor = sceneColor * darkening;
        FragColor = vec4(outlinedColor, 1.0);
    }
}
