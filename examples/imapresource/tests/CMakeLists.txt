set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
)

include(SinkTest)

auto_tests (
    imapserverproxytest
    imapmailtest
    imapmailsynctest
)
target_link_libraries(imapserverproxytest sink_resource_imap)
target_link_libraries(imapmailtest sink_resource_imap)
target_link_libraries(imapmailsynctest sink_resource_imap)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resetmailbox.sh DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)
