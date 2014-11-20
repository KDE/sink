project(toynadi_resource)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(common_path "../common/")

set(toynadiresource_SRCS
    ${common_path}/console.cpp
    main.cpp
    listener.cpp
)

add_executable(${PROJECT_NAME} ${toynadiresource_SRCS})
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
