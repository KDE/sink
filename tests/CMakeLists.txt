add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/hawd
    ${CMAKE_CURRENT_BINARY_DIR}/../examples/dummyresource
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/
    )

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp testimplementations.cpp)
        generate_flatbuffers(${_testname} calendar)
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} akonadi2common libhawd)
    endforeach(_testname)
endmacro(manual_tests)

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp testimplementations.cpp)
        generate_flatbuffers(${_testname} calendar)
        add_test(${_testname} ${_testname})
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} akonadi2common libhawd)
    endforeach(_testname)
endmacro(auto_tests)

manual_tests (
    storagebenchmark
    dummyresourcebenchmark
#     genericresourcebenchmark
#     genericfacadebenchmark
)

auto_tests (
    clientapitest
    storagetest
    # dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
    # genericresourcetest
    # genericfacadetest
    resourcecommunicationtest
    pipelinetest
    querytest
)

# target_link_libraries(dummyresourcetest akonadi2_resource_dummy)
target_link_libraries(dummyresourcebenchmark akonadi2_resource_dummy)
target_link_libraries(querytest akonadi2_resource_dummy)

