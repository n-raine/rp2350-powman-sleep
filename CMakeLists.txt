add_executable(powman_states
    powman_states.c
)

# pull in common dependencies
target_link_libraries(powman_states pico_stdlib)

# enable usb output, disable uart output
pico_enable_stdio_usb(powman_states 1)
pico_enable_stdio_uart(powman_states 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(powman_states)

