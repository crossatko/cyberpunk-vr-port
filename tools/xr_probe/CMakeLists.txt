# XR_APILAYER_CPVR_probe -- a standalone OpenXR API layer.
#
# Deliberately NOT linked against openxr_loader. A layer is loaded BY the loader and must resolve
# everything downstream through the function pointers the loader hands it at negotiation; linking
# the loader into a layer produces two loaders in one process. Headers only.
#
# This target is independent of cyberpunkvrport_stereo and is never deployed into the plugin
# folder. Build it by name:
#     cmake --build build --config Release --target xr_probe_layer

add_library(xr_probe_layer SHARED
    layer.cpp
)

target_compile_features(xr_probe_layer PRIVATE cxx_std_20)

target_compile_definitions(xr_probe_layer PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    XR_USE_PLATFORM_WIN32
    _CRT_SECURE_NO_WARNINGS=1
)

# ${openxr_SOURCE_DIR} comes from the FetchContent declaration in the root CMakeLists.
target_include_directories(xr_probe_layer PRIVATE
    ${openxr_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(xr_probe_layer PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(xr_probe_layer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "XR_APILAYER_CPVR_probe"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/xr_probe"
)

# The manifest must sit next to the DLL: its library_path is the relative ".\\<dll>".
add_custom_command(TARGET xr_probe_layer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/XR_APILAYER_CPVR_probe.json"
            "$<TARGET_FILE_DIR:xr_probe_layer>/XR_APILAYER_CPVR_probe.json"
    COMMENT "Copying the API-layer manifest next to the layer DLL"
)

# ── xrprobe.exe -----------------------------------------------------------------------------
# A minimal OpenXR application: dumps what the runtime reports and exercises every call the layer
# intercepts. Unlike the layer, this one DOES link the loader -- it is an application.
add_executable(xr_probe_cli probe_cli.cpp)
target_compile_features(xr_probe_cli PRIVATE cxx_std_20)
target_compile_definitions(xr_probe_cli PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    XR_USE_PLATFORM_WIN32
    _CRT_SECURE_NO_WARNINGS=1
)
target_link_libraries(xr_probe_cli PRIVATE openxr_loader)
if(MSVC)
    target_compile_options(xr_probe_cli PRIVATE /W4 /permissive- /EHsc)
endif()
set_target_properties(xr_probe_cli PROPERTIES
    OUTPUT_NAME "xrprobe"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/xr_probe"
)
