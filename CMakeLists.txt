# PicoBoy — Game Boy (Walnut-CGB) 与 NES (InfoNES) 双核掌机工程
#
#   gbc/   Walnut-CGB 核心 + minigb_apu
#   nes/   InfoNES 核心 + 133 个 mapper
#   bsp/   唯一的板级支持包，两个 target 共用
#   tools/ PC 侧菜单渲染自检
#
# 两个 target：
#   picoboy_gbc   Game Boy / Game Boy Color，运行时从 flash ROM 区读 ROM
#   pico_nes      NES / FC，运行时从 flash ROM 区读 ROM
# ROM 由 rom_uploader 打成未压缩 ustar tar 烧到固定 ROM 区（XIP 直读），
# 固件不再在编译期内嵌 ROM，也不依赖 Python。
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(PicoBoy C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---------------- 共用 BSP（唯一一份） ----------------
add_subdirectory(bsp)

# ---------------- Game Boy / GBC ----------------
# minigb_apu：Game Boy APU。采样率必须与 BSP PWM 载波一致（61035）
# 才能音画平衡，所以宏挂在库上 PUBLIC 传下去。
add_library(minigb_apu STATIC gbc/minigb_apu/minigb_apu.cpp)
target_include_directories(minigb_apu PUBLIC gbc/minigb_apu)
target_compile_definitions(minigb_apu PUBLIC
    MINIGB_APU_AUDIO_FORMAT_S16SYS
    AUDIO_SAMPLE_RATE=61035
)

add_executable(picoboy_gbc
    gbc/main.cpp
    gbc/rom_menu.cpp        # GB 侧适配：把 flash ROM 条目转成共用菜单项
)
target_include_directories(picoboy_gbc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/gbc
    ${CMAKE_CURRENT_LIST_DIR}/gbc/third_party/Walnut-CGB
)
target_link_libraries(picoboy_gbc PRIVATE picoboy minigb_apu)
target_compile_options(picoboy_gbc PRIVATE -O3)

pico_enable_stdio_usb(picoboy_gbc 1)
pico_enable_stdio_uart(picoboy_gbc 0)

# GBC 应用链接到固定槽位 0x10010000（由 bootloader 跳转进入），向量表在槽+0x100
set_target_properties(picoboy_gbc PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_gbc_slot.ld)

pico_add_extra_outputs(picoboy_gbc)

# ---------------- NES / FC ----------------
# infones/mapper/*.cpp 不是独立编译单元（上游在 InfoNES_Mapper.cpp 里逐个
# #include），所以这里只收顶层 .cpp。
file(GLOB INFONES_CORE ${CMAKE_CURRENT_LIST_DIR}/nes/infones/*.cpp)

add_library(infones STATIC ${INFONES_CORE})
target_include_directories(infones PUBLIC ${CMAKE_CURRENT_LIST_DIR}/nes/infones)
target_link_libraries(infones PUBLIC pico_stdlib)
target_compile_options(infones PRIVATE -O3)

add_executable(pico_nes
    nes/main.cpp
    nes/src/InfoNES_System_Pico.cpp
)
target_include_directories(pico_nes PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/nes/include
)
target_link_libraries(pico_nes PRIVATE picoboy infones)
target_compile_options(pico_nes PRIVATE -O3)

# 每核 4KB 栈：InfoNES 调用链深（K6502_Step → $2007 写 → mapper → HSync），
# SDK 默认 2KB 偏紧。PICO_STACK_SIZE 是编译宏而非缓存变量，必须传给 crt0.S。
target_compile_definitions(pico_nes PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)

pico_enable_stdio_usb(pico_nes 1)
pico_enable_stdio_uart(pico_nes 0)

# NES 应用链接到固定槽位 0x10080000（由 bootloader 跳转进入），向量表在槽+0x100
set_target_properties(pico_nes PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_nes_slot.ld)

pico_add_extra_outputs(pico_nes)

# ---------------- Bootloader（开机系统选择） ----------------
# 位于 flash 起点 0x10000000，开机出 GBC/NES 菜单，设置 VTOR 后跳到槽位应用。
# 只做菜单和跳转，不启动双核推屏，保持单帧缓冲即可。
add_executable(picoboy_boot
    boot/bootloader.cpp
)
target_link_libraries(picoboy_boot PRIVATE picoboy)
target_compile_options(picoboy_boot PRIVATE -O3)

pico_enable_stdio_usb(picoboy_boot 1)
pico_enable_stdio_uart(picoboy_boot 0)

set_target_properties(picoboy_boot PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_pico16m.ld)

pico_add_extra_outputs(picoboy_boot)
