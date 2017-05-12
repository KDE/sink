project(sink_resource_dav)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KPimKDAV2 REQUIRED)

add_library(${PROJECT_NAME} SHARED davresource.cpp)
target_link_libraries(${PROJECT_NAME} sink Qt5::Core Qt5::Network KPim::KDAV2)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})
