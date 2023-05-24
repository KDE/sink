set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)
add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(SinkTest)

integration_tests (
    maildirmailtest
    maildirmailsynctest
    maildirthreadtest
)
if (${ENABLE_INTEGRATION_TESTS})
    target_link_libraries(maildirmailtest sink_resource_maildir)
    target_link_libraries(maildirmailsynctest sink_resource_maildir)
    target_link_libraries(maildirthreadtest sink_resource_maildir)
endif()
