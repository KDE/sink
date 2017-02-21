project(sink_resource_dav)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)
find_package(KPimKDAV REQUIRED)

add_library(${PROJECT_NAME} SHARED facade.cpp davresource.cpp domainadaptor.cpp)
qt5_use_modules(${PROJECT_NAME} Core Network)
target_link_libraries(${PROJECT_NAME} sink KPim::KDAV)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

#add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

#add_subdirectory(libmaildir)
#add_subdirectory(tests)
