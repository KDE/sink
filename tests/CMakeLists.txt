add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/hawd)

generate_flatbuffers(calendar)

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        qt5_use_modules(${_testname} Core Test Concurrent)
        target_link_libraries(${_testname} akonadi2common libhawd)
    endforeach(_testname)
endmacro(manual_tests)

manual_tests (
    storagebenchmark
    storagetest
    dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
    dummyresourcebenchmark
)

target_link_libraries(dummyresourcetest akonadi2_resource_dummy)
target_link_libraries(dummyresourcebenchmark akonadi2_resource_dummy)

