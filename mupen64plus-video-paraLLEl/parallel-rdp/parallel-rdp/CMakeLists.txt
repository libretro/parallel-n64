if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(PARALLEL_RDP_CXX_FLAGS -Wall -Wextra -Wno-missing-field-initializers -Wno-empty-body -ffast-math -Wno-unused-parameter -pthread -fwrapv)
elseif (MSVC)
    set(PARALLEL_RDP_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP /DNOMINMAX)
endif()

add_library(parallel-rdp STATIC
        rdp_common.hpp
        rdp_data_structures.hpp
        rdp_renderer.cpp rdp_renderer.hpp
        video_interface.cpp video_interface.hpp
        command_ring.cpp command_ring.hpp
        worker_thread.hpp luts.hpp
        rdp_device.cpp rdp_device.hpp)
target_link_libraries(parallel-rdp PRIVATE granite)
target_compile_options(parallel-rdp PRIVATE ${PARALLEL_RDP_CXX_FLAGS})
target_include_directories(parallel-rdp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(parallel-rdp PRIVATE PARALLEL_RDP_SHADER_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\")
