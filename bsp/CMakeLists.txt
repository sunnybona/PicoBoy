# bsp/CMakeLists.txt — PicoBoy 共用板级库
#
# 只由顶层工程 add_subdirectory(bsp) 引入，因此这里不写 project()/pico_sdk_init()：
# SDK 初始化由顶层完成一次，BSP 只负责产出一个 picoboy 静态库。
# GB（picoboy_gbc）和 NES（pico_nes）两个 target 都链到这一个 picoboy，
# 板级代码全工程只有这一份。

add_library(picoboy STATIC
    src/pb_display.cpp
    src/pb_audio.cpp
    src/pb_input.cpp
    src/pb_settings.cpp
    src/pb_rom_menu.cpp      # GB/NES 共用的 ROM 菜单（外观按 GB 定版）
)

target_include_directories(picoboy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# PicoBoy 板载 flash 是 16MB，SDK 默认按 2MB 算。pb_settings 要把设置放最后
# 一扇区，必须知道真实容量；顺带让 flash.c 的范围断言正确。
# PUBLIC 传出，保证库内编译单元和目标里的 flash.c 看到同一个值。
if(NOT DEFINED PB_FLASH_SIZE_BYTES)
    set(PB_FLASH_SIZE_BYTES 16777216)
endif()
target_compile_definitions(picoboy PUBLIC PICO_FLASH_SIZE_BYTES=${PB_FLASH_SIZE_BYTES})

# 生成 PIO 头（st7789_lcd.pio.h），挂到 picoboy 目标
pico_generate_pio_header(picoboy ${CMAKE_CURRENT_SOURCE_DIR}/src/st7789_lcd.pio)

target_link_libraries(picoboy PUBLIC
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_pwm
    hardware_dma
    hardware_clocks
    hardware_flash          # pb_settings 存亮度/音量要用
)
