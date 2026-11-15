idf_component_register(
    SRCS "platform/deca_compat.c" "dwt_uwb_driver/deca_interface.c"
         "dwt_uwb_driver/deca_rsl.c" "dwt_uwb_driver/dw3000/dw3000_device.c"
         "dwt_uwb_driver/lib/qmath/src/qmath.c"
    INCLUDE_DIRS "dwt_uwb_driver"
    PRIV_INCLUDE_DIRS "platform" "dwt_uwb_driver/lib/qmath/include"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_DW3000_CHIP_DW3000=1 PUBLIC USE_DRV_DW3000=1)
