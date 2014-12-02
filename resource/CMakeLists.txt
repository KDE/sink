project(akonadinext_resource)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(akonadinextresource_SRCS
    main.cpp
    listener.cpp
)

add_executable(${PROJECT_NAME} ${akonadinextresource_SRCS})
target_link_libraries(${PROJECT_NAME} akonadinextcommon)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
