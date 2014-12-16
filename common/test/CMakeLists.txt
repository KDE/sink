set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp ${store_SRCS})
        qt5_use_modules(${_testname} Core Test)
        target_link_libraries(${_testname} akonadi2common)
        add_test(NAME ${_testname} COMMAND ${_testname})
    endforeach(_testname)
endmacro(auto_tests)

auto_tests (
    clientapitest
)
