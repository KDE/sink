project(toynadi_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(common_path "../common/")

set(toynadiclient_SRCS
    ${common_path}/console.cpp
    main.cpp
    resourceaccess.cpp
)
add_executable(${PROJECT_NAME} ${toynadiclient_SRCS})
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
