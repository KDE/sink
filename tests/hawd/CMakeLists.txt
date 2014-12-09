project(hawd)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(lib_SRCS
    dataset.cpp
    datasetdefinition.cpp
    state.cpp
)

set(SRCS
    main.cpp
    module.cpp
    modules/list.cpp
)

add_library(libhawd ${lib_SRCS})
qt5_use_modules(libhawd Core)
target_link_libraries(libhawd akonadi2common)
install(TARGETS libhawd DESTINATION lib)

add_executable(${PROJECT_NAME} ${SRCS})
qt5_use_modules(${PROJECT_NAME} Core)
target_link_libraries(${PROJECT_NAME} libhawd)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

