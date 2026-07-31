cmake_minimum_required(VERSION 3.21)

project(
    psx-engine
    VERSION 0.1.0
    DESCRIPTION "Fixed-capacity game engine for the original PlayStation"
    LANGUAGES C ASM
)

option(PSXE_BUILD_SAMPLES "Build bundled sample games" ON)

set(PSXE_SOURCES
    src/core/engine.c
    src/core/scene.c
    src/render/render.c
    src/platform/audio.c
    src/platform/input.c
    src/platform/memory_card.c
)

psn00bsdk_add_library(psxe STATIC ${PSXE_SOURCES})
set_target_properties(psxe PROPERTIES PSN00BSDK_TARGET_TYPE EXECUTABLE_GPREL)
target_include_directories(psxe PUBLIC ${PROJECT_SOURCE_DIR}/src/include)
target_compile_features(psxe PUBLIC c_std_99)
target_compile_options(
    psxe PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter -fno-strict-aliasing
)
# The SDK applies these to compilation but not to its executable link command.
# Propagate them so GCC selects the R3000A soft-float libgcc multilib as well.
target_link_options(psxe INTERFACE -msoft-float -march=r3000 -mabi=32)
# Repeat SDK archives after psxe in downstream static link order. The executable
# helper adds them before project libraries, which is insufficient for symbols
# first referenced by psxe objects.
target_link_libraries(psxe PUBLIC ${PSN00BSDK_LIBRARIES})

if(PSXE_BUILD_SAMPLES)
    add_subdirectory(samples/cube)
endif()
