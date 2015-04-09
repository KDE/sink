add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/hawd)
include_directories (${CMAKE_CURRENT_BINARY_DIR}/../dummyresource)

generate_flatbuffers(calendar)

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} akonadi2common libhawd)
    endforeach(_testname)
endmacro(manual_tests)

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        add_test(${_testname} ${_testname})
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} akonadi2common libhawd)
    endforeach(_testname)
endmacro(auto_tests)

manual_tests (
    storagebenchmark
    dummyresourcebenchmark
)

auto_tests (
    storagetest
    dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
)

target_link_libraries(dummyresourcetest akonadi2_resource_dummy)
target_link_libraries(dummyresourcebenchmark akonadi2_resource_dummy)

