set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    )

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        # generate_flatbuffers(${_testname} calendar)
        add_test(${_testname} ${_testname})
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} sink libhawd)
    endforeach(_testname)
endmacro(auto_tests)

auto_tests (
    imapresourcetest
    imapserverproxytest
    imapmailtest
)
target_link_libraries(imapresourcetest sink_resource_imap)
target_link_libraries(imapserverproxytest sink_resource_imap)
target_link_libraries(imapmailtest sink_test sink_resource_imap)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resetmailbox.sh DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)
