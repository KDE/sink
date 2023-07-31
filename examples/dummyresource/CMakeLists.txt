project(sink_resource_dummy)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})


add_library(${PROJECT_NAME} STATIC resourcefactory.cpp dummystore.cpp)
generate_flatbuffers(${PROJECT_NAME} dummycalendar)
target_link_libraries(${PROJECT_NAME} PUBLIC sink Qt5::Core Qt5::Network)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})
