project(hawd)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(SRCS
    datasetdefinition.cpp
    main.cpp
    module.cpp
    state.cpp
    modules/list.cpp
)

add_executable(${PROJECT_NAME} ${SRCS})
qt5_use_modules(${PROJECT_NAME} Core)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

