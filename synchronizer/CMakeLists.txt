project(sink_synchronizer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(sinksynchronizer_SRCS
    main.cpp
)

add_executable(${PROJECT_NAME} ${sinksynchronizer_SRCS})
target_link_libraries(${PROJECT_NAME}
    sink
    Qt5::Core
    Qt5::Gui
    Qt5::Network
    KAsync
    ${CMAKE_DL_LIBS}
)
if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreFoundation")
endif()
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
