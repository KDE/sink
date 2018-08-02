project(sink_resource_caldav)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KPimKDAV2 REQUIRED)
find_package(KF5CalendarCore REQUIRED)

add_library(${PROJECT_NAME} SHARED caldavresource.cpp)
target_link_libraries(${PROJECT_NAME} sink_webdav_common sink Qt5::Core Qt5::Gui Qt5::Network KPim::KDAV2
	KF5::CalendarCore)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

if (WIN32)
    message("Not building caldavresource resource tests on windows")
else()
    add_subdirectory(tests)
endif()
