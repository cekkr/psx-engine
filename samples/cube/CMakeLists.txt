psn00bsdk_add_executable(psxe_cube GPREL main.c)
target_link_libraries(psxe_cube PRIVATE psxe)
target_compile_options(psxe_cube PRIVATE -Wall -Wextra -Wshadow -Wconversion)

# mkpsxiso puts source modification times in ISO directory records. Normalize
# the copies used for packaging, keeping source files and the linker output intact.
set(disc_stage "${CMAKE_CURRENT_BINARY_DIR}/disc")
set(disc_executable "${CMAKE_CURRENT_BINARY_DIR}/psxe_cube${PSN00BSDK_EXECUTABLE_SUFFIX}")
set(disc_files
    "${disc_stage}/SYSTEM.CNF"
    "${disc_stage}/PSXE_CUB.EXE"
    "${disc_stage}/README.TXT"
    "${disc_stage}/LICENSE.TXT"
    "${disc_stage}/ASSETS/PAINTED.TIM"
    "${disc_stage}/ASSETS/CHIME.VAG"
)
add_custom_command(
    OUTPUT "${disc_stage}/staged"
    BYPRODUCTS ${disc_files}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${disc_stage}/ASSETS"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/system.cnf" "${disc_stage}/SYSTEM.CNF"
    COMMAND ${CMAKE_COMMAND} -E copy "${disc_executable}" "${disc_stage}/PSXE_CUB.EXE"
    COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/README.md" "${disc_stage}/README.TXT"
    COMMAND ${CMAKE_COMMAND} -E copy "${PROJECT_SOURCE_DIR}/LICENSE" "${disc_stage}/LICENSE.TXT"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/painted.tim" "${disc_stage}/ASSETS/PAINTED.TIM"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/chime.vag" "${disc_stage}/ASSETS/CHIME.VAG"
    COMMAND ${CMAKE_COMMAND} -E env TZ=UTC touch -t 200001010000.00 ${disc_files} "${disc_stage}/ASSETS"
    COMMAND ${CMAKE_COMMAND} -E touch "${disc_stage}/staged"
    DEPENDS
        psxe_cube "${disc_executable}" system.cnf assets/painted.tim assets/chime.vag
        "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/LICENSE"
    VERBATIM
)

psn00bsdk_add_cd_image(
    psxe_cube_iso
    psxe_cube
    iso.xml
    DEPENDS "${disc_stage}/staged" ${disc_files} iso.xml
)
