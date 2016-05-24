project(sink_resource_maildir)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)

add_library(${PROJECT_NAME} SHARED facade.cpp maildirresource.cpp domainadaptor.cpp)
# generate_flatbuffers(${PROJECT_NAME} dummycalendar)
qt5_use_modules(${PROJECT_NAME} Core Network)
target_link_libraries(${PROJECT_NAME} sink maildir KF5::Mime)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

add_subdirectory(libmaildir)
add_subdirectory(tests)
