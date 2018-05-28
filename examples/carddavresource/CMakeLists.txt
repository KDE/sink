project(sink_resource_carddav)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KPimKDAV2 REQUIRED)

add_library(${PROJECT_NAME} SHARED carddavresource.cpp)
target_link_libraries(${PROJECT_NAME} sink_webdav_common sink Qt5::Core Qt5::Network KPim::KDAV2)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

if (WIN32)
    message("Not building carddav resource tests on windows")
else()
    add_subdirectory(tests)
endif()
