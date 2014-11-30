project(toynadi_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(toynadiclient_SRCS
    main.cpp
    resourceaccess.cpp
)

add_executable(${PROJECT_NAME} ${toynadiclient_SRCS})
target_link_libraries(${PROJECT_NAME} toynadicommon)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
