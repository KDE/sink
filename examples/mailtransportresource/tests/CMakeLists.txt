set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)

include(SinkTest)

integration_tests (
    mailtransporttest
)
if (${ENABLE_INTEGRATION_TESTS})
    target_link_libraries(mailtransporttest sink_resource_mailtransport)
endif()
