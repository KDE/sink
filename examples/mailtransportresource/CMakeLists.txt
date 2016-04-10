project(sink_resource_mailtransport)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)
find_package(CURL 7.20.0 REQUIRED)

include_directories(${CURL_INCLUDE_DIRS})


add_library(${PROJECT_NAME} SHARED facade.cpp mailtransportresource.cpp mailtransport.cpp)
qt5_use_modules(${PROJECT_NAME} Core Network)
target_link_libraries(${PROJECT_NAME} sink KF5::Mime ${CURL_LIBRARIES})

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})
