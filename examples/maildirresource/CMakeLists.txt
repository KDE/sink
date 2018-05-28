project(sink_resource_maildir)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)

add_library(${PROJECT_NAME} SHARED facade.cpp maildirresource.cpp libmaildir/maildir.cpp libmaildir/keycache.cpp)
target_link_libraries(${PROJECT_NAME} sink Qt5::Core Qt5::Network KF5::Mime)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")

add_subdirectory(tests)
