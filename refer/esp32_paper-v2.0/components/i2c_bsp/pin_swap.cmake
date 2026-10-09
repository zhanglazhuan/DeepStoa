# This source and its linker hooks must be enabled together.
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/i2c_pin_swap.c")
foreach(api i2c_new_master_bus i2c_del_master_bus i2c_master_bus_add_device
            i2c_master_bus_rm_device i2c_master_transmit i2c_master_receive
            i2c_master_transmit_receive i2c_master_multi_buffer_transmit
            i2c_master_execute_defined_operations i2c_master_probe i2c_master_bus_reset)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${api}")
endforeach()
