cmake_minimum_required(VERSION 3.10)
project(xmrig_hash_tests C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(XMRIG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${XMRIG_ROOT}/cmake")

include(CheckIncludeFile)
include(CheckSymbolExists)
include(${XMRIG_ROOT}/cmake/cpu.cmake)
include(${XMRIG_ROOT}/cmake/os.cmake)
include(${XMRIG_ROOT}/cmake/flags.cmake)
find_package(Threads REQUIRED)

add_definitions(
    -DXMRIG_MINER_PROJECT
    -DXMRIG_JSON_SINGLE_LINE_ARRAY
    -DXMRIG_ALGO_CN_LITE
    -DXMRIG_ALGO_CN_HEAVY
    -DXMRIG_ALGO_CN_PICO
    -DXMRIG_ALGO_CN_FEMTO
    -DXMRIG_ALGO_CN_GPU
    -DXMRIG_ALGO_RANDOMX
    -DXMRIG_ALGO_ARGON2
    -DXMRIG_ALGO_GHOSTRIDER
    -D__STDC_FORMAT_MACROS
    -DUNICODE
    -D_FILE_OFFSET_BITS=64
)

add_subdirectory(${XMRIG_ROOT}/src/3rdparty/argon2 ${CMAKE_BINARY_DIR}/argon2)
add_subdirectory(${XMRIG_ROOT}/src/crypto/ghostrider ${CMAKE_BINARY_DIR}/ghostrider)

set(HASH_TEST_SOURCES
    main.cpp
    cpu_threads_stubs.cpp
    log_stubs.cpp
    ${XMRIG_ROOT}/src/3rdparty/fmt/format.cc
    ${XMRIG_ROOT}/src/backend/cpu/Cpu.cpp
    ${XMRIG_ROOT}/src/base/crypto/Algorithm.cpp
    ${XMRIG_ROOT}/src/base/crypto/keccak.cpp
    ${XMRIG_ROOT}/src/base/tools/Chrono.cpp
    ${XMRIG_ROOT}/src/base/tools/String.cpp
    ${XMRIG_ROOT}/src/crypto/argon2/Impl.cpp
    ${XMRIG_ROOT}/src/crypto/cn/c_blake256.c
    ${XMRIG_ROOT}/src/crypto/cn/c_groestl.c
    ${XMRIG_ROOT}/src/crypto/cn/c_jh.c
    ${XMRIG_ROOT}/src/crypto/cn/c_skein.c
    ${XMRIG_ROOT}/src/crypto/cn/CnCtx.cpp
    ${XMRIG_ROOT}/src/crypto/cn/CnHash.cpp
    ${XMRIG_ROOT}/src/crypto/common/Assembly.cpp
    ${XMRIG_ROOT}/src/crypto/common/HugePagesInfo.cpp
    ${XMRIG_ROOT}/src/crypto/common/MemoryPool.cpp
    ${XMRIG_ROOT}/src/crypto/common/VirtualMemory.cpp
    ${XMRIG_ROOT}/src/crypto/flex/flex.cpp
    ${XMRIG_ROOT}/src/crypto/flex/flex_keccak.c
    ${XMRIG_ROOT}/src/crypto/randomx/aes_hash.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/allocator.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/blake2/blake2b.c
    ${XMRIG_ROOT}/src/crypto/randomx/blake2_generator.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/bytecode_machine.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/dataset.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/instructions_portable.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/panthera/KangarooTwelve.c
    ${XMRIG_ROOT}/src/crypto/randomx/panthera/KeccakP-1600-reference.c
    ${XMRIG_ROOT}/src/crypto/randomx/panthera/KeccakSpongeWidth1600.c
    ${XMRIG_ROOT}/src/crypto/randomx/panthera/sha256.c
    ${XMRIG_ROOT}/src/crypto/randomx/panthera/yespower-opt.c
    ${XMRIG_ROOT}/src/crypto/randomx/randomx.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/reciprocal.c
    ${XMRIG_ROOT}/src/crypto/randomx/soft_aes.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/superscalar.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/virtual_machine.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/virtual_memory.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/vm_compiled.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/vm_compiled_light.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/vm_interpreted.cpp
    ${XMRIG_ROOT}/src/crypto/randomx/vm_interpreted_light.cpp
    ${XMRIG_ROOT}/src/crypto/rx/RxAlgo.cpp
)

if (XMRIG_OS_WIN)
    list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/crypto/common/VirtualMemory_win.cpp)
else()
    list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/crypto/common/VirtualMemory_unix.cpp)
endif()

if (XMRIG_OS_LINUX)
    list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/crypto/common/LinuxMemory.cpp)
endif()

if (XMRIG_ARM)
    enable_language(ASM)
    list(APPEND HASH_TEST_SOURCES
        ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo_arm.cpp
        ${XMRIG_ROOT}/src/crypto/cn/gpu/cn_gpu_arm.cpp
        ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_a64.cpp
        ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_a64_static.S
    )

    if (XMRIG_OS_WIN)
        list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo_arm_win.cpp)
    elseif (XMRIG_OS_APPLE)
        list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo_arm_mac.cpp)
    else()
        list(APPEND HASH_TEST_SOURCES
            ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo_arm_unix.cpp
            ${XMRIG_ROOT}/src/backend/cpu/platform/lscpu_arm.cpp
        )
    endif()

    if (NOT CMAKE_GENERATOR STREQUAL Xcode)
        set_property(SOURCE ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_a64_static.S PROPERTY LANGUAGE C)
    endif()
elseif (XMRIG_RISCV)
    list(APPEND HASH_TEST_SOURCES
        ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo_riscv.cpp
        ${XMRIG_ROOT}/src/backend/cpu/platform/lscpu_riscv.cpp
    )
else()
    list(APPEND HASH_TEST_SOURCES
        ${XMRIG_ROOT}/src/backend/cpu/platform/BasicCpuInfo.cpp
        ${XMRIG_ROOT}/src/crypto/cn/gpu/cn_gpu_avx.cpp
        ${XMRIG_ROOT}/src/crypto/cn/gpu/cn_gpu_ssse3.cpp
    )

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_definitions(-DXMRIG_FEATURE_ASM)
        if ((WIN32 OR CMAKE_SYSTEM_NAME MATCHES "MSYS") AND CMAKE_C_COMPILER_ID MATCHES GNU)
            set(HASH_TEST_CN_ASM_SOURCES
                ${XMRIG_ROOT}/src/crypto/cn/asm/win64/cn_main_loop.S
                ${XMRIG_ROOT}/src/crypto/cn/asm/CryptonightR_template.S
            )
        else()
            set(HASH_TEST_CN_ASM_SOURCES
                ${XMRIG_ROOT}/src/crypto/cn/asm/cn_main_loop.S
                ${XMRIG_ROOT}/src/crypto/cn/asm/CryptonightR_template.S
            )
        endif()

        list(APPEND HASH_TEST_SOURCES
            ${HASH_TEST_CN_ASM_SOURCES}
            ${XMRIG_ROOT}/src/crypto/cn/r/CryptonightR_gen.cpp
            ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_x86_static.S
            ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_x86.cpp
        )
        set_property(SOURCE ${HASH_TEST_CN_ASM_SOURCES} PROPERTY LANGUAGE C)
        set_property(SOURCE ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_x86_static.S PROPERTY LANGUAGE C)
    else()
        list(APPEND HASH_TEST_SOURCES ${XMRIG_ROOT}/src/crypto/randomx/jit_compiler_fallback.cpp)
    endif()
endif()

add_executable(hash-tests ${HASH_TEST_SOURCES})
target_include_directories(hash-tests PRIVATE
    ${XMRIG_ROOT}/src
    ${XMRIG_ROOT}/src/3rdparty
    ${XMRIG_ROOT}/src/crypto/ghostrider
)
target_link_libraries(hash-tests PRIVATE argon2 ghostrider Threads::Threads)

if (XMRIG_OS_WIN)
    target_link_libraries(hash-tests PRIVATE advapi32)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set_source_files_properties(${XMRIG_ROOT}/src/crypto/cn/CnHash.cpp PROPERTIES COMPILE_FLAGS "-Ofast -fno-tree-vectorize")
    if (NOT XMRIG_ARM AND NOT XMRIG_RISCV)
        set_source_files_properties(${XMRIG_ROOT}/src/crypto/cn/gpu/cn_gpu_avx.cpp PROPERTIES COMPILE_FLAGS "-O3 -mavx2")
        set_source_files_properties(${XMRIG_ROOT}/src/crypto/cn/gpu/cn_gpu_ssse3.cpp PROPERTIES COMPILE_FLAGS "-O3")
    endif()
endif()

add_custom_target(check-hashes
    COMMAND hash-tests --suite small
    DEPENDS hash-tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(check-hashes-full
    COMMAND hash-tests --suite full
    DEPENDS hash-tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
