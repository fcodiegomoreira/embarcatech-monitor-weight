# Criar o executável com os arquivos .c
add_executable(rp2040-freertos-template
    main.c
    hx711.c
    ssd1306.c
)

# Avisa o compilador onde está o hx711.h (que está na pasta include)
target_include_directories(rp2040-freertos-template PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link das bibliotecas necessárias
target_link_libraries(rp2040-freertos-template
    pico_stdlib
    pico_cyw43_arch_lwip_sys_freertos  # Versão correta para FreeRTOS + WiFi
    FreeRTOS-Kernel-Heap4
    FreeRTOS-Kernel
    hardware_i2c
    pico_lwip_http
)

# Habilitar USB e gerar arquivo UF2
pico_enable_stdio_usb(rp2040-freertos-template 1)
pico_enable_stdio_uart(rp2040-freertos-template 0)
pico_add_extra_outputs(rp2040-freertos-template)