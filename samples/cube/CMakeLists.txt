psn00bsdk_add_executable(psxe_cube GPREL main.c)
target_link_libraries(psxe_cube PRIVATE psxe)
target_compile_options(psxe_cube PRIVATE -Wall -Wextra -Wshadow -Wconversion)

psn00bsdk_add_cd_image(
    psxe_cube_iso
    psxe_cube
    iso.xml
    DEPENDS psxe_cube system.cnf
)
