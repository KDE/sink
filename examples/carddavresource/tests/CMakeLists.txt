set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_BINARY_DIR})

include(SinkTest)

integration_tests (
    carddavtest
)
if (${ENABLE_INTEGRATION_TESTS})
    target_link_libraries(carddavtest sink_resource_carddav KF5::Contacts)
endif()
