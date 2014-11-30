project(toynadi_resource)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(toynadiresource_SRCS
    main.cpp
    listener.cpp
    ../common/handshake_generated.h
)

add_executable(${PROJECT_NAME} ${toynadiresource_SRCS})
target_link_libraries(${PROJECT_NAME} toynadicommon)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
