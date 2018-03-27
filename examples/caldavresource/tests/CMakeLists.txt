set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_BINARY_DIR})

include(SinkTest)

auto_tests (
    caldavtest
)
target_link_libraries(caldavtest sink_resource_caldav)
