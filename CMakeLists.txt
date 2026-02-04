# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 1.5.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.0.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

# Configurações da Placa e SDK
set(PICO_BOARD pico_w)
set(PICO_SDK_PATH "C:/Users/fcodi/.pico-sdk/sdk/1.5.1")

include(pico_sdk_import.cmake)

project(rp2040-freertos-template C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
pico_sdk_init()

# Configuração do FreeRTOS
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/FreeRTOS-Kernel)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
# Silencia avisos de procura de compilador que poluem a aba Problems
set(CMAKE_MESSAGE_LOG_LEVEL STATUS)
# Chama a pasta src (onde o código realmente será compilado)
add_subdirectory(src)