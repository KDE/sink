set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)

include(SinkTest)

auto_tests (
    mailtransporttest
)
target_link_libraries(mailtransporttest sink_resource_mailtransport)
