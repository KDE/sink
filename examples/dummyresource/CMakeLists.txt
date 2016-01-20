project(sink_resource_dummy)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})


add_library(${PROJECT_NAME} SHARED facade.cpp resourcefactory.cpp domainadaptor.cpp dummystore.cpp)
generate_flatbuffers(${PROJECT_NAME} dummycalendar)
qt5_use_modules(${PROJECT_NAME} Core Network)
target_link_libraries(${PROJECT_NAME} sinkcommon)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})
