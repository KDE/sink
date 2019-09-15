set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_BINARY_DIR})

include(SinkTest)

auto_tests (
    carddavtest
)
target_link_libraries(carddavtest sink_resource_carddav KF5::Contacts)
