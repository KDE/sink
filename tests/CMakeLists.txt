set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

generate_flatbuffers(calendar)

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        qt5_use_modules(${_testname} Core Test)
        target_link_libraries(${_testname} akonadinextcommon)
    endforeach(_testname)
endmacro(manual_tests)

manual_tests (
    storagebenchmark
)
