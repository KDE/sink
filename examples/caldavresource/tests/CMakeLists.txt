set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_BINARY_DIR})

include(SinkTest)

auto_tests (
    caldavtest
)
target_link_libraries(caldavtest sink_resource_caldav)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resetcalendar.sh DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)
