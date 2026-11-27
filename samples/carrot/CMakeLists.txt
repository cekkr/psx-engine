psn00bsdk_add_executable(psxe_carrot GPREL main.c)
target_link_libraries(psxe_carrot PRIVATE psxe)
target_compile_options(psxe_carrot PRIVATE -Wall -Wextra -Wshadow -Wconversion)

psn00bsdk_target_incbin(psxe_carrot PRIVATE carrot_poses assets/poses.bin)
psn00bsdk_target_incbin(psxe_carrot PRIVATE carrot_tim assets/color.tim)

set(disc_stage "${CMAKE_CURRENT_BINARY_DIR}/disc")
set(disc_executable "${CMAKE_CURRENT_BINARY_DIR}/psxe_carrot${PSN00BSDK_EXECUTABLE_SUFFIX}")
set(disc_files "${disc_stage}/SYSTEM.CNF" "${disc_stage}/CARROT.EXE" "${disc_stage}/CREDITS.TXT")
add_custom_command(
    OUTPUT "${disc_stage}/staged"
    BYPRODUCTS ${disc_files}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${disc_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/system.cnf" "${disc_stage}/SYSTEM.CNF"
    COMMAND ${CMAKE_COMMAND} -E copy "${disc_executable}" "${disc_stage}/CARROT.EXE"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${disc_stage}/CREDITS.TXT"
    COMMAND ${CMAKE_COMMAND} -E env TZ=UTC touch -t 200001010000.00 ${disc_files}
    COMMAND ${CMAKE_COMMAND} -E touch "${disc_stage}/staged"
    DEPENDS psxe_carrot "${disc_executable}" system.cnf README.md
    VERBATIM
)

psn00bsdk_add_cd_image(
    psxe_carrot_iso
    psxe_carrot
    iso.xml
    DEPENDS "${disc_stage}/staged" ${disc_files} iso.xml
)
