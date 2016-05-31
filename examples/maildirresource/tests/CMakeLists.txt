set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)

include(SinkTest)

auto_tests (
    maildirmailtest
    maildirmailsynctest
)
target_link_libraries(maildirmailtest sink_resource_maildir)
target_link_libraries(maildirmailsynctest sink_resource_maildir)
