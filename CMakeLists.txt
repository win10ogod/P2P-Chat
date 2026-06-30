cmake_minimum_required(VERSION 3.15)
project(P2PChat
    VERSION     2.1.0
    LANGUAGES   CXX
    DESCRIPTION "Cross-platform P2P Chat with Dear ImGui GUI"
)

# ─── C++ Standard ─────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Options ──────────────────────────────────────────────────────────────────
option(ENABLE_VOICE  "Enable voice chat (requires PortAudio + Opus)" OFF)
option(BUILD_SERVER  "Build the signaling server executable"         ON)
option(BUILD_TESTS   "Build integration tests"                       ON)

# ─── Platform Detection ───────────────────────────────────────────────────────
if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS -D_WIN32_WINNT=0x0601 -DNOMINMAX)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
else()
    add_definitions(-DPLATFORM_LINUX)
endif()

# ─── Compiler Warnings ────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ─── Dependencies ─────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 IMPORTED_TARGET glfw3)
    endif()
endif()

# ─── Optional: PortAudio + Opus for voice ─────────────────────────────────────
if(ENABLE_VOICE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PORTAUDIO portaudio-2.0)
        pkg_check_modules(OPUS opus)
    endif()
    if(NOT PORTAUDIO_FOUND OR NOT OPUS_FOUND)
        message(WARNING "PortAudio or Opus not found. Voice will be disabled.")
        set(ENABLE_VOICE OFF)
    endif()
endif()

# ─── Dear ImGui (vendored) ────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# ─── Core Library ─────────────────────────────────────────────────────────────
add_library(p2p_core STATIC
    src/core/types.cpp
    src/protocol/protocol.cpp
    src/network/udp_socket.cpp
    src/network/stun_client.cpp
    src/network/turn_client.cpp
)
target_include_directories(p2p_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(p2p_core PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(p2p_core PUBLIC ws2_32 iphlpapi)
endif()

# ─── Client Executable ────────────────────────────────────────────────────────
add_executable(p2pchat
    src/main.cpp
    src/core/session.cpp
    src/network/p2p_connection.cpp
    src/network/signaling.cpp
    src/audio/audio_engine.cpp
    src/ui/chat_window.cpp
)
target_include_directories(p2pchat PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(p2pchat PRIVATE p2p_core imgui)

if(ENABLE_VOICE)
    target_compile_definitions(p2pchat PRIVATE HAS_VOICE=1)
    target_include_directories(p2pchat PRIVATE ${PORTAUDIO_INCLUDE_DIRS} ${OPUS_INCLUDE_DIRS})
    target_link_libraries(p2pchat PRIVATE ${PORTAUDIO_LIBRARIES} ${OPUS_LIBRARIES})
endif()

# ─── Signaling Server ─────────────────────────────────────────────────────────
if(BUILD_SERVER)
    add_executable(signaling_server src/server_main.cpp)
    target_include_directories(signaling_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(signaling_server PRIVATE p2p_core)
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ─── Install ──────────────────────────────────────────────────────────────────
install(TARGETS p2pchat DESTINATION bin)
if(BUILD_SERVER)
    install(TARGETS signaling_server DESTINATION bin)
endif()
