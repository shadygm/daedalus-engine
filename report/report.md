# 3D Graphics Assignment – Final Report

## Group Members and Contributions

### Shady
Implemented the following features:
- Vignette  
- Color Grading
- Film Grain  
- Depth of Field  
- Chaos Pendulum (Hierarchical Transformations)
- Object Selection  
- Environment Mapping
- Point and Spot Light Shadows  
- Multi-Light Shadow Support  
- Physically-Based Rendering
- Blinn-Phong
- GLTF Parsing  
- Runtime Object Loading  
- Runtime Light Changing  
- Camera Keyframe Interpolation (Smooth Paths)
- Bloom  

---

### Miguel
Implemented the following features:
- Water Surface *(Gerstner Waves + Dual Scrolling Normal maps)*  
- Fog
- Screen space Edge Detection (Sobel Edge Detection)
- World Curvature  
- Parallax Mapping  
- Transparency *(sorted back to front with blending)*  
- Anti Aliasing *(MSAA)*  
- Particle Effects *(snow, fireworks, etc.)*  
- MiniMap  
- More Intuitive Parameter Controls *(debug UI and material options)*  
- Report
- Video Demonstration

---

## Overview

This project implements a real time 3D graphics engine using modern OpenGL, aimed at producing visually cohesive scenes where lighting, materials, and camera behavior can be adjusted interactively. The renderer is designed to be flexible with users being able to modify the environment, animate the camera, load new models at runtime, and tune post processing effects without restarting the application. 

The engine brings together physically based shading, image based lighting, dynamic shadows, atmospheric and cinematic post effects, transparency handling, and particle based elements. Scene objects can be selected and manipulated directly in the world, allowing the engine to function as both a visualizer and a lightweight scene editor. Additional features such as a dynamic water surface, world curvature deformation, a chaos pendulum simulation, screen space edge outlining, and a minimap help demonstrate a range of rendering and simulation techniques.


### References & Learning Resources

These resources significantly influenced our approach and helped unblock multiple implementations:

- **OpenGL effects overview and links:** [What OpenGL Can Do (YouTube)](https://www.youtube.com/watch?v=Uc8FEI5bg3w)  
- **Engine showcase for ideas:** [Engine Feature Showcase (YouTube)](https://www.youtube.com/watch?v=ajG59KKmkco)  
- **OpenGL tutorial series:** [OpenGL Playlist (YouTube)](https://www.youtube.com/watch?v=h2cP6sQYdf0&list=PLA0dXqQjCx0S04ntJKUftl6OaOgsiwHjA)  
- **Primary reference for many features (PBR, IBL, post):** [LearnOpenGL](https://learnopengl.com/Introduction)

The following sections detail each feature’s goal and implementation and illustrate results with in engine screenshots.


##  Vignette

### Overview
The vignette effect darkens the image toward the screen edges to guide the viewer’s attention to the center. This helps create a more cinematic visual style.

### Implementation
The vignette is implemented in *camera_effects.frag* in the function ```applyVignette()```. The shader computes the distance of each fragment from the screen center in normalized coordinates, taking aspect ratio into account. This distance is mapped between an inner and outer radius, and then shaped using ```pow(1.0 - t, power)``` to control how smooth or sharp the transition is. The result is blended with the original color based on the intensity parameter. When the feature toggle is disabled or intensity is zero, the original color is returned. The parameters are exposed in the UI so that radius, softness, and intensity can be adjusted interactively while viewing the final output.

| Before | After |
|-------|-------|
| ![no vignette](/report/screenshots/no_Vignette.png) | ![vignette](/report/screenshots/vignette.png) |

## Color Grading

### Overview
Color grading adjusts the tonal balance, contrast, and saturation of the final rendered image. This allows for artistic control over the overall style or mood.

### Implementation
Implemented in `applyColorGrading()` in *camera_effects.frag*. The effect uses a Lift Gamma Gain configuration similar to those used in film and photography workflows. Lift applies a per channel offset to shadows, gamma applies per channel exponent shaping to midtones, and gain scales the highlights. After this, contrast is applied by blending the result against mid gray, and saturation is controlled by mixing between the color and its luminance value. The parameters are controlled through the debug UI, allowing real time adjustment without reloading shaders. The final color is clamped to prevent unintended overflow.

| Example 1 | Example 2 |
|-------|-------|
| ![Color Grading1](/report/screenshots/color_Grading1.png) | ![Color Grading2](/report/screenshots/color_Grading2.png) |

## Film Grain

### Overview
Film grain introduces subtle randomized variation across the image to reduce the digitally perfect appearance of rendered frames. This can create a more natural and textured look.

### Implementation
Implemented in `applyFilmGrain()` in *camera_effects.frag*. A pseudo random number is generated per fragment using UV coordinates, screen resolution, and a time value so that the noise appears animated over time. The grain is scaled by both a user defined amount and a luminance response factor so that darker areas can receive more grain if desired. The grain is then added to the base color and clamped. Parameters such as intensity, response behavior, and animation speed are adjusted through the UI.

| Example 1 | Example 2 |
|-------|-------|
| ![film Grain1](/report/screenshots/film_Grain1.png) | ![film Grain 2](/report/screenshots/film_Grain2.png) |

## Depth of Field

### Overview
Depth of Field simulates a camera lens focusing on a particular distance, causing objects in front of or behind that plane to appear blurred.

### Implementation
Implemented in `applyDepthOfField()` in *camera_effects.frag*. The scene depth is sampled and linearized, then used to compute a circle of confusion value representing how far the pixel is from the focal distance. This determines a blur radius in pixels. If the radius is small, the pixel remains unblurred for efficiency. Otherwise, the shader samples neighboring texels using a Poisson disk pattern and averages them to produce a smooth blur. The focus distance, blur range, and maximum blur radius are editable in real time in the UI.

| Example 1 | Example 2 |
|-------|-------|
| ![DoF1](/report/screenshots/depthOfField1.png) | ![DoF2](/report/screenshots/depthOfField2.png) |

## Chaos Pendulum

### Overview

The chaos pendulum is a simulated chain of linked nodes that exhibits chaotic motion based on small variations in initial conditions. This creates a visually dynamic pendulum animation that behaves unpredictably over time.

### Implementation

The pendulum is managed in *PendulumManager.cpp* and updated each frame from *Application.cpp*. Each pendulum link is represented as a NodeState that stores mass, length, position, velocity, and previous position. The system integrates motion using either a semi implicit Euler integrator or an RK4 integrator (selectable at runtime). After integration, the system enforces distance constraints to maintain the fixed lengths between nodes. The final world transforms for each node are built and pushed to the renderer.

The pendulum simulation runs at a fixed timestep to keep the motion stable. The UI allows adjusting gravity, damping, link size, and which integrator is used. The initial setup adds a small offset to one of the nodes so the pendulum naturally falls into chaotic motion.

| Example 1 | Example 2 |
|-------|-------|
| ![chaosPendulumn1](/report/screenshots/chaos_pendulumn1.png) | ![chaosPendulumn2](/report/screenshots/chaos_pendulumn2.png) |

## Object Selection

### Overview

Object selection allows the user to click on scene objects and move them interactively. This supports runtime editing and inspection directly in the rendered view.

### Implementation

Selection is handled by *SelectionManager.cpp*. Instead of using a color ID framebuffer, the system performs CPU side ray picking. Each frame, `beginFrame()` is called and objects register themselves via `addSelectable()`, which provides their world space bounds and an identifier.

When the user clicks, a picking ray is generated from the camera. Then it tests the ray against all registered selectable bounds and returns the nearest hit. The selected object can then be dragged using `beginDrag`, `updateDrag`, and `endDrag`, which project movement against either the ground plane or a vertical plane depending on the drag mode.

| Example 1 (X-axis movement) | Example 2 (Y-axis movement) |
|-------|-------|
| ![objectSelection1](/report/screenshots/object_Selection1.png) | ![objectionSelection2](/report/screenshots/object_Selection2.png) |

## Environment Mapping

### Overview
Environment mapping is used to provide realistic reflections and indirect lighting based on a high dynamic range environment texture. By using the surrounding scene as a lighting reference, surfaces pick up subtle lighting cues and reflections that help ground them more naturally in the environment.

### Implementation
The system loads an HDR image and converts it into a cubemap representation, handled by the `EnvironmentManager`. From this cubemap, additional derived textures are generated to support physically based shading: an irradiance cubemap for diffuse ambient lighting, a prefiltered cubemap for specular reflections at varying roughness levels, and a BRDF lookup texture to shape light response on micro surfaces. These textures are then bound to the PBR shader, allowing materials to receive both diffuse and glossy lighting contributions from the environment. The intensity of the environment can be adjusted at runtime through the UI, making it possible to quickly test different lighting moods or environments without modifying the scene itself.

| Example 1 | Example 2 |
|-------|-------|
| ![environmentMapping1](/report/screenshots/environment_Mapping1.png) | ![environmentMapping2](/report/screenshots/environment_Mapping2.png) |

## Shadow Rendering for Point and Spot Lights (Multi-Light Support)

### Overview

The renderer supports real time shadows from both spot lights and point lights, and it can handle multiple shadow casting lights at once. This allows lighting in the scene to react naturally as objects and lights move, creating a more grounded and believable image.

### Implementation

Shadow generation is coordinated by the `ShadowManager`. Spot lights produce standard depth shadow maps by rendering the scene from the light’s point of view, while point lights generate cube map shadow textures to capture depth in all directions around them. During shading, the correct shadow map is sampled depending on the light type, allowing each fragment to determine whether it is lit or occluded. The `ShadowManager` maintains a pool of shadow map resources and assigns them to active lights, ensuring that multiple shadow casting lights can be handled efficiently in a single frame. Parameters such as shadow resolution, depth range, and filtering softness can be adjusted to balance performance and visual quality.

| Example 1 | Example 2 |
|-------|-------|
| ![shadow1](/report/screenshots/shadows1.png) | ![shadow2](/report/screenshots/shadows2.png) |

## Physically Based Rendering (PBR)
### Overview

Physically Based Rendering gives materials a more realistic response to light by simulating how surfaces reflect and scatter illumination in the real world. This allows metals, plastics, stone, painted surfaces, and rough or smooth finishes to behave differently under the same lighting conditions, producing consistent results across environments.

### Implementation

The PBR shading is implemented in *pbr.frag*. It uses a standard microfacet lighting model so that highlights and reflections look natural. The shader combines lighting from scene lights with lighting taken from the environment. Direct lights are read from a GPU buffer, and shadows are applied when enabled. The environment contributes both diffuse and reflective lighting using the irradiance map, prefiltered cubemap, and BRDF lookup texture generated earlier. Materials supply values such as base color, metallic, roughness, normal maps, and emissive color, either from model textures or fallback defaults if no texture is provided. This results in surfaces that feel physically grounded and visually consistent across the scene.

| Example 1 | Example 2 |
|-------|-------|
| ![PBR1](/report/screenshots/PBR1.png) | ![PBR2](/report/screenshots/shadows2.png) |

## Model Importing & Runtime Object Loading (glTF)
### Overview

The engine supports importing glTF models and loading them directly into the scene during runtime. This allows assets created in external tools to be brought into the application without converting formats or restarting the program.

### Implementation

Models are imported using Assimp in *ModelLoader.cpp*, which reads the mesh data and material information from the glTF file. The loader extracts vertex attributes such as positions, normals, tangents, UVs, and indices, and it also reads metallic roughness PBR material properties and texture maps when available. Any missing textures fall back to default material values to keep the object visually stable.

When the user loads a model through the ImGui interface, `ModelLoader::loadModel()` is called. The geometry and materials are uploaded to the GPU through the MeshManager so the object becomes immediately visible and selectable in the scene. Textures and buffers are created on the fly, meaning no rebuild or restart is required. Once loaded, the object behaves like any other: it can receive lighting, shadows, and material adjustments, and it can be interacted with via the object selection system.

|  Object Loading |    |
|-------|-------|
| ![objectload1](/report/screenshots/objectLoad1.png) | ![objectload2](/report/screenshots/objectLoad2.png) |

## Runtime Light Changing
### Overview

Lights in the scene can be created and modified during runtime, allowing interactive control over illumination while the application is running.

### Implementation

All light properties are exposed through an ImGui panel. When any parameter changes, such as position, direction, brightness, or whether the light casts shadows, the LightManager marks its GPU data as needing an update. The updated light data is then uploaded to the GPU in the next frame, and shadow resources are refreshed if necessary. This makes lighting adjustments immediate and visible while maintaining compatibility with the existing PBR and shadow systems.

| Light Color | High Intensity   |
|-------|-------|
| ![light1](/report/screenshots/light1.png) | ![light2](/report/screenshots/light2.png) |

## Camera Keyframe Interpolation
### Overview

Camera keyframe interpolation allows the camera to move smoothly along a user defined path. This is useful for flythroughs, cutscenes, scene previews, and establishing shots. The system stores snapshots of the camera’s position, rotation, and field of view, and then interpolates between them over time to produce continuous motion.

### Implementation

Keyframes are captured through the UI in *Application.cpp*, where the current camera transform is read and stored. Each keyframe includes time, world space position, rotation (as a quaternion), and FOV. The keyframes are stored in a CameraPath, which can be edited by adding, removing, or adjusting entries through the UI panel. During playback, the `CameraPathPlayer` samples the path and returns smoothly interpolated camera states. Position interpolation uses a cubic Bezier curve built from the sequence of keyframes, producing a natural motion without sudden direction changes. Rotations are interpolated using quaternion slerp to ensure smooth orientation transitions, and FOV blends linearly. The player supports play, pause, loop, speed control, scrubbing through time, and a "follow camera" mode that directly applies the interpolated result back to the active camera each frame. A debug path renderer can optionally draw the curve and keyframes in the scene for visual clarity. 

| Example 1 (Day night cycle implemented using this feature) |
|-------|
| ![keyframe1](/report/screenshots/keyframe1.png) |

For more examples of this it was used in multiple instances of our video demo, speficially in the multi shadow section, and the keyframe + PBR showcase section.

## Bloom
### Overview

Bloom creates a soft glow around bright areas in the scene. This simulates how real camera lenses and the human eye scatter intense light, making highlights appear more natural and giving the image a more vibrant, cinematic quality.

### Implementation

Bloom is implemented within `CameraEffectsStage` and processed as part of the post processing pipeline. When enabled, the bloom system takes the rendered scene and extracts the brightest parts using a threshold filter. The result is then downsampled across several lower resolution textures to spread the light and soften it. These downsampled images are gradually upsampled and blended back together to form a smooth glow effect. The final bloom texture is then added back onto the original scene color during the post processing pass in `camera_effects.frag`, where additional options such as intensity shaping are applied. The UI exposes controls for bloom strength, blur radius, threshold softness and number of mip levels. When the bloom toggle or intensity is set to zero, the system skips processing to avoid unnecessary overhead.

| Example 1 | Example 2 |
|-------|-------|
| ![bloom1](/report/screenshots/bloom1.png) | ![bloom2](/report/screenshots/bloom2.png) |

## Water Surface (Gerstner Waves + Detail Normals)
### Overview

The water surface is simulated using Gerstner waves to create natural wave motion, combined with scrolling normal maps to add fine surface detail. This produces water that moves smoothly at large scale while still showing ripples and highlights when viewed up close.

### Implementation

The water mesh is generated as a flat grid and displaced entirely in the vertex shader. Up to four Gerstner waves are applied, each contributing horizontal and vertical displacement based on amplitude, wavelength, direction, and speed. The surface normal is computed analytically from the wave functions, ensuring smooth lighting even as the surface moves.

In the fragment shader, two independently scrolling detail normal maps are sampled and blended into the analytically derived normal. This adds smaller ripples without affecting the large wave shape. The final shading includes depth based color tinting, Fresnel reflection, and a specular highlight controlled by user parameters. The UI provides real time control over wave behavior, color falloff, surface opacity, and detail normal strength.

| Example 1 | Example 2 |
|-------|-------|
| ![water1](/report/screenshots/water1.png) | ![water2](/report/screenshots/water2.png) |

## Fog
### Overview

Fog adds atmospheric depth by gradually fading distant objects into a fog color. This helps create a sense of scale and can be used to establish mood or weather conditions.

### Implementation

Fog is applied directly in the material fragment shaders. The world space distance from the camera to each fragment is calculated, and this distance is used in an exponential falloff function to blend between the scene color and the fog color. The density and gradient of the fog control how quickly objects fade with distance, while the color determines the mood of the scene. All fog parameters are adjustable in the UI and are applied globally during the shading stage.

| Example 1 | Example 2 |
|-------|-------|
| ![fog1](/report/screenshots/fog1.png) | ![fog2](/report/screenshots/fog2.png) |


## Screen Space Edge Detection (Sobel Outline)
### Overview

Edge detection highlights object borders and surface changes by drawing outlines around visible edges. This can emphasize shapes, improve readability in cluttered scenes, or create a stylized rendering effect.

### Implementation

A Sobel filter is applied in a full screen pass that analyzes both depth and normal differences in a 3×3 neighborhood. Depth edges capture silhouette and object separation, while normal edges capture sharp changes in surface direction. These two edge signals are combined and thresholded to produce an outline mask. The final step darkens pixels where edges are detected. The UI allows adjusting outline strength and sensitivity, and the system also provides an option to preview the raw edge mask for debugging.

| Example 1 | Example 2 |
|-------|-------|
| ![edge1](/report/screenshots/edge1.png) | ![edge2](/report/screenshots/edge2.png) |


## World Curvature
### Overview

World curvature applies a stylized lens warp effect that bends the world downward or upward as distance increases. This can produce a subtle “planet-like” horizon or a fantasy style terrain presentation.

### Implementation

The effect is applied in the vertex shader after transforming positions into view space. The vertical view space coordinate is offset based on the square of the distance from the camera, controlled by a single curvature strength parameter. Because the adjustment happens before projection, the effect is consistent across geometry and silhouettes. The UI provides a toggle and a strength slider, allowing real time adjustment of how strongly the world bends.

| Before World Curviture | After World Curviture |
|-------|-------|
| ![curve1](/report/screenshots/curve2B.png) | ![curve2](/report/screenshots/curve2A.png) |

| Top-View Camera of World Curviture |
|-------|
| ![curve1](/report/screenshots/curve1.png) |

## Parallax Mapping

### Overview
Parallax mapping adds the illusion of surface depth by offsetting texture coordinates based on the viewing angle and a height value. This allows flat surfaces to appear as though they have grooves, ridges, or layered detail without using additional geometry.

### Implementation
Parallax mapping is implemented in the material fragment shaders (`pbr.frag` and `blinn_phong.frag`). A simple offset mapping approach is used: the view direction in tangent space is used to shift the texture coordinates before sampling any material textures. The height is taken either from a dedicated height map or, optionally, from the alpha or luminance of the normal map if no height texture is provided. The amount of displacement is controlled through scale and bias parameters exposed in the UI.

| Before parallax mapping | After parallax mapping |
|-------|-------|
| ![parallax1](/report/screenshots/parallax1.png) | ![parallax2](/report/screenshots/parallax2.png) |


## Transparency (Sorted Back to Front Blending)

### Overview
Transparency allows objects such as glass, water, or particles to be rendered with partial opacity. To ensure correct blending with the scene, transparent objects must be drawn in a specific order relative to the camera.

### Implementation
Objects are divided into opaque and transparent groups at render time. Opaque objects are drawn first with depth writing enabled. Transparent objects are collected into a list, sorted by distance from the camera in descending order, and then drawn with blending enabled and depth writing disabled. This ensures that farther transparent surfaces are drawn first and nearer transparent surfaces blend correctly over them. Water and particle effects are also drawn in this transparent pass so they follow the same blending rules. While sorting works well for most cases, intersecting transparent geometry can still produce artifacts, which is a known limitation of this approach.

| Example 1 | Example 2 |
|----------|----------|
| ![](/report/screenshots/transparency1.png) | ![](/report/screenshots/transparency2.png) |


## Anti Aliasing (MSAA)

### Overview
Multi Sample Anti Aliasing (MSAA) reduces jagged edges by sampling each pixel multiple times during rasterization. This results in smoother edges without requiring expensive post processing filters.

### Implementation
When MSAA is enabled, the scene is first rendered into a multisampled framebuffer. At the end of the frame, the multisampled color and depth buffers are resolved into a single-sample texture using `glBlitFramebuffer`. Post processing effects such as bloom and depth of field always operate on the resolved texture. The number of MSAA samples can be adjusted in the UI, and the system dynamically recreates the framebuffer resources when the sample count changes.

| Anti Aliasing off | Anti Aliasing On |
|----------|----------|
| ![](/report/screenshots/MSAA1.png) | ![](/report/screenshots/MSAA2.png) |

## Particle Effects (Snow, Fireworks, Aura Effects)

### Overview
The particle system is used to create dynamic and ambient effects such as snowfall, fireworks, magic auras, and explosions. These effects add motion and atmosphere to the scene.

### Implementation
Particles are simulated on the CPU each frame. Each particle stores position, velocity, lifetime, size, and color. The system updates particles based on gravity, timers, and scripted behaviors depending on the effect type. Rendering is performed using point sprites, where each particle is drawn as a camera facing quad. Snow uses alpha blending for soft flakes, while fireworks and aura effects use additive blending to produce glowing trails. Controls in the UI allow adjusting particle counts, speeds, brightness, burst patterns, and spawning behavior. The system is flexible, though large particle counts can become CPU heavy due to per frame buffer updates.

| Example Snow | Example Firework | Example Magic |
|----------|----------|----------|
| ![](/report/screenshots/particle1.png) | ![](/report/screenshots/particle2.png) | ![](/report/screenshots/particle3.png) |


## MiniMap

### Overview
The minimap provides a top down view of the scene to help visualize spatial layout and navigation. It acts as a secondary camera, rendering the world from above and displaying it as an overlay in the UI.

### Implementation
The minimap renders the scene into its own framebuffer using a top down orthographic projection. The camera is positioned above the player or scene center and looks downward. The output texture is then drawn as a circular overlay in the UI. The minimap size and camera height can be adjusted through the UI, allowing users to zoom the map in or out. Because the minimap performs a full additional render each frame, reducing its update frequency or resolution can improve performance if needed.

| Example 1 |
|----------|
| ![](/report/screenshots/minimap.png) |



## Future Improvements and Possible Extensions

While the engine includes a wide range of real time rendering and interaction features, several systems can be expanded to increase realism, performance, or artistic flexibility. The points below highlight areas with clear improvement paths and practical next steps.

### Water Simulation

The current water surface uses Gerstner waves for large motion and scrolling normal maps for small ripples. This produces a visually convincing result, but it does not yet respond dynamically to objects or environmental forces. Future improvements could include:

- Foam generation along shorelines or wave peaks  
- Screen space or planar reflections for more accurate reflection behavior  
- Refraction and underwater light scattering  
- FFT based ocean simulation (Tessendorf waves) for large bodies of water  

These additions would allow the water to feel more physically cohesive and reactive to lighting and scene interaction.

### Transparency Rendering

The transparency system sorts transparent objects back to front and blends them over the opaque scene. While this works for many cases, overlapping or interpenetrating transparent objects can lead to incorrect blending. Future work could explore:

- **Weighted blended order independent transparency** (no sorting required)  
- Depth peeling techniques for correct multi layer blending  
- Material specific blend models (glass, fog, particles, etc.)  

These improvements would make transparency more stable and visually accurate in complex scenes.

### Shadow Rendering

Although the shadow system supports multiple shadow casting lights, performance cost grows with each additional light. Improvements could focus on scalability and quality:

- Shadow atlas packing to reduce texture usage  
- Temporal or priority updates (only update shadows when needed)
- Implement SSAO to "bake" some of the light behavior into the system

These would allow shadows to remain high quality even in large or dynamic scenes.

### Particle System

Particles are currently simulated on the CPU and uploaded to the GPU every frame. This limits the number of particles that can be active simultaneously. A future upgrade could migrate simulation to the GPU using compute shaders, enabling:

- Higher particle counts  
- Complex behaviors such as turbulence and wind fields  
- Reduced CPU overhead in large scenes  

This would make effects like smoke, sparks, or volumetric weather significantly more detailed.

### Cloth and Hair Simulation

Cloth and hair animation were considered features to add but were outside the scope of the final implementation. Supporting them would require physically based simulation systems that handle collisions, wind response, and stable integration over time. Potential approaches include:

- Mass spring or position based dynamics (PBD) for stable cloth motion  
- Strand based simulation for hair, with level of detail control  
- GPU compute dispatch to maintain performance for many simulated elements  

Adding cloth and hair would greatly enhance character and environment realism, but would require dedicated simulation pipelines, collision handling, and GPU acceleration to perform well in real time.

---

Overall, the project provides a solid and adaptable rendering framework. Future work could focus on improving physical realism, making materials and transparency more accurate, and adding richer dynamic effects like particles, water interaction, and eventually cloth or hair simulation. These additions would make the engine capable of handling more complex scenes and creating experiences that feel closer to real films or games.
