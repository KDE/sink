add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/hawd
    ${CMAKE_CURRENT_BINARY_DIR}/../examples/dummyresource
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/
    )

add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/data")

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp testimplementations.cpp getrssusage.cpp)
        generate_flatbuffers(${_testname} calendar)
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} sink libhawd)
    endforeach(_testname)
endmacro(manual_tests)

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp testimplementations.cpp getrssusage.cpp)
        generate_flatbuffers(${_testname} calendar)
        add_test(${_testname} ${_testname})
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} sink libhawd)
    endforeach(_testname)
endmacro(auto_tests)

manual_tests (
    storagebenchmark
    dummyresourcebenchmark
#     genericresourcebenchmark
    mailquerybenchmark
#     genericfacadebenchmark
)

auto_tests (
    clientapitest
    storagetest
    dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
    # genericresourcetest
    # genericfacadetest
    resourcecommunicationtest
    pipelinetest
    querytest
    databasepopulationandfacadequerybenchmark
    dummyresourcewritebenchmark
    modelinteractivitytest
    inspectiontest
)
target_link_libraries(dummyresourcetest sink_resource_dummy)
target_link_libraries(dummyresourcebenchmark sink_resource_dummy)
target_link_libraries(dummyresourcewritebenchmark sink_resource_dummy)
target_link_libraries(querytest sink_resource_dummy)
target_link_libraries(modelinteractivitytest sink_resource_dummy)
target_link_libraries(inspectiontest sink_resource_dummy)

if (BUILD_MAILDIR)
    auto_tests (
        maildirresourcetest
    )
    manual_tests (
        maildirsyncbenchmark
    )
    target_link_libraries(maildirresourcetest sink_resource_maildir)
    target_link_libraries(maildirsyncbenchmark sink_resource_maildir)
endif()
