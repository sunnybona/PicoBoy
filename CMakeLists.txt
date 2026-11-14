# PicoBoy — Game Boy (Walnut-CGB) 与 NES (InfoNES) 双核掌机工程
#
#   gbc/   Walnut-CGB 核心 + minigb_apu
#   nes/   InfoNES 核心 + 133 个 mapper
#   bsp/   唯一的板级支持包，两个 target 共用
#   tools/ PC 侧菜单渲染自检
#
# 两个 target：
#   picoboy_gbc   Game Boy / Game Boy Color，ROM 编进 flash（gbc/rom.h）
#   pico_nes      NES / FC，ROM 编进 flash（nes/roms/*.nes，configure 时打包）
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(PicoBoy C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---------------- 共用 BSP（唯一一份） ----------------
add_subdirectory(bsp)

# ---------------- Game Boy / GBC ----------------
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR "需要 python3 来生成 gb_rom.h / nes_rom.h")
endif()

# ROM 表：把 gbc/roms/*.gb(/.gbc) 编进 flash。和 NES 那边同一套做法 —— 源文件
# 放目录、configure 时打包，生成物落在 build/，工程里不再存 19.5 MB 的 rom.h。
# 增删 ROM 后要重新 configure（cmake -S . -B build 会自动重跑）。
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/gbc/tools/gen_gb_rom.py
            --roms ${CMAKE_CURRENT_LIST_DIR}/gbc/roms
            --out  ${CMAKE_CURRENT_BINARY_DIR}/gb_rom.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    RESULT_VARIABLE PB_GB_ROM_RESULT
)
if(NOT PB_GB_ROM_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_gb_rom.py 失败（返回 ${PB_GB_ROM_RESULT}）")
endif()

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
    gbc/rom_menu.cpp        # GB 侧适配：把 pb_rom_entry_t 转成共用菜单项
)
target_include_directories(picoboy_gbc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/gbc
    ${CMAKE_CURRENT_LIST_DIR}/gbc/third_party/Walnut-CGB
    ${CMAKE_CURRENT_BINARY_DIR}          # 生成的 gb_rom.h
)
target_link_libraries(picoboy_gbc PRIVATE picoboy minigb_apu)
target_compile_options(picoboy_gbc PRIVATE -O3)

pico_enable_stdio_usb(picoboy_gbc 1)
pico_enable_stdio_uart(picoboy_gbc 0)

# 本机 flash 16MB：SDK 默认 memmap 只认 2MB，超出会链接报错
set_target_properties(picoboy_gbc PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_pico16m.ld)

pico_add_extra_outputs(picoboy_gbc)

# ---------------- NES / FC ----------------
# infones/mapper/*.cpp 不是独立编译单元（上游在 InfoNES_Mapper.cpp 里逐个
# #include），所以这里只收顶层 .cpp。
file(GLOB INFONES_CORE ${CMAKE_CURRENT_LIST_DIR}/nes/infones/*.cpp)

add_library(infones STATIC ${INFONES_CORE})
target_include_directories(infones PUBLIC ${CMAKE_CURRENT_LIST_DIR}/nes/infones)
target_link_libraries(infones PUBLIC pico_stdlib)
target_compile_options(infones PRIVATE -O3)

# ROM 表：把 nes/roms/*.nes 编进 flash。增删 ROM 后要重新 configure。
# （Python3 已在 GB 段 find_package 过）

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/nes/tools/gen_nes_rom.py
            --roms ${CMAKE_CURRENT_LIST_DIR}/nes/roms
            --out  ${CMAKE_CURRENT_BINARY_DIR}/nes_rom.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    RESULT_VARIABLE PB_NES_ROM_RESULT
)
if(NOT PB_NES_ROM_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_nes_rom.py 失败（返回 ${PB_NES_ROM_RESULT}）")
endif()

add_executable(pico_nes
    nes/main.cpp
    nes/src/InfoNES_System_Pico.cpp
)
target_include_directories(pico_nes PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/nes/include
    ${CMAKE_CURRENT_BINARY_DIR}          # 生成的 nes_rom.h
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

set_target_properties(pico_nes PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_pico16m.ld)

pico_add_extra_outputs(pico_nes)
