add_executable(integration_test integration_test.cpp
    ${CMAKE_SOURCE_DIR}/src/core/session.cpp
    ${CMAKE_SOURCE_DIR}/src/network/p2p_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/network/signaling.cpp
    ${CMAKE_SOURCE_DIR}/src/audio/audio_engine.cpp
)
target_include_directories(integration_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(integration_test PRIVATE p2p_core)
