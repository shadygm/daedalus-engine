cmake_minimum_required(VERSION 3.14 FATAL_ERROR)
project(ComputerGraphics C CXX)

# Set this before including framework such that it knows to use the OpenGL4.5 version of GLAD
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/framework")
	# Create framework library and include CMake scripts (compiler warnings, sanitizers and static analyzers).
	add_subdirectory("framework")
elseif(PROJECT_IS_TOP_LEVEL)
	add_subdirectory("../../../framework/" "${CMAKE_CURRENT_BINARY_DIR}/framework/")
endif()

add_executable( daedalus_engine
	src/particle/ParticleSystem.cpp
	src/app/Application.cpp
	src/camera/CameraStage.cpp
	src/camera/FPSCamera.cpp
	src/camera/TopCamera.cpp
	src/camera/CameraPath.cpp
	src/camera/CameraPathPlayer.cpp
	src/mesh/mesh.cpp
	src/mesh/MeshInstance.cpp
	src/mesh/MeshManager.cpp
	src/scene/ModelLoader.cpp
	src/player/PlayerController.cpp
	src/rendering/EnvironmentManager.cpp
	src/rendering/CameraEffectsStage.cpp
	src/rendering/LightManager.cpp
	src/rendering/ShadingStage.cpp
	src/rendering/ShaderManager.cpp
	src/rendering/texture.cpp
	src/rendering/SunPathController.cpp
	src/rendering/PathRenderer.cpp
	src/terrain/ProceduralFloor.cpp
	src/app/DebugUiManager.cpp
	src/app/SelectionManager.cpp
	src/util/BezierPath.cpp
	src/util/PathAnimator.cpp
	src/pendulum/PendulumManager.cpp
	src/ui/Minimap.cpp
    src/water/Water.cpp
)

target_include_directories(daedalus_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mesh
	${CMAKE_CURRENT_SOURCE_DIR}/src/scene
    ${CMAKE_CURRENT_SOURCE_DIR}/src/player
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/src/terrain
	${CMAKE_CURRENT_SOURCE_DIR}/src/water
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
	${CMAKE_CURRENT_SOURCE_DIR}/src/pendulum
	${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)


target_compile_definitions(daedalus_engine PRIVATE RESOURCE_ROOT="${CMAKE_CURRENT_LIST_DIR}/")
target_compile_features(daedalus_engine PRIVATE cxx_std_20)
target_link_libraries(daedalus_engine PRIVATE CGFramework assimp::assimp)
enable_sanitizers(daedalus_engine)
set_project_warnings(daedalus_engine)

# Copy all files in the resources folder to the build directory after every successful build.
add_custom_command(TARGET daedalus_engine POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	"${CMAKE_CURRENT_LIST_DIR}/resources/" "$<TARGET_FILE_DIR:daedalus_engine>/resources/")

# We would like to copy the files when they changed. Even if no *.cpp files were modified (and
# thus no build is triggered). We tell CMake that the executable depends on the shader files in
# the build directory. We also tell it how to generate those files (by copying them from the
# shaders folder in this directory). The gather all glsl files in the shaders folder when CMake
# is configured. So if you were to add a shader file then you need to configure CMake again.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/")

set(daedalus_engine_shader_copies "")
set(daedalus_engine_shader_sources "")
set(_shader_output_paths "")

set(SHADER_SOURCE_DIRS
	"${CMAKE_CURRENT_LIST_DIR}/src/rendering/shaders"
	"${CMAKE_CURRENT_LIST_DIR}/shaders"
)

foreach(shader_dir IN LISTS SHADER_SOURCE_DIRS)
	if (EXISTS "${shader_dir}")
		file(GLOB dir_shaders
			"${shader_dir}/*.glsl"
			"${shader_dir}/*.vert"
			"${shader_dir}/*.frag"
			"${shader_dir}/*.comp"
		)
		list(APPEND daedalus_engine_shader_sources ${dir_shaders})
	endif()
endforeach()

foreach (shader_file IN LISTS daedalus_engine_shader_sources)
	get_filename_component(file_name ${shader_file} NAME)
	set(output_path "${CMAKE_CURRENT_BINARY_DIR}/shaders/${file_name}")
	list(FIND _shader_output_paths ${output_path} _existing_index)
	if (_existing_index GREATER -1)
		continue()
	endif()

	add_custom_command(
		OUTPUT "${output_path}"
		COMMAND ${CMAKE_COMMAND} -E copy
		"${shader_file}"
		"${output_path}"
		DEPENDS "${shader_file}"
	)
	list(APPEND daedalus_engine_shader_copies "${output_path}")
	list(APPEND _shader_output_paths "${output_path}")
endforeach()
add_custom_target(daedalus_engine_copy_shaders DEPENDS ${daedalus_engine_shader_copies})
add_dependencies(daedalus_engine daedalus_engine_copy_shaders)

