set(CMAKE_AUTOMOC ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(store_path "../")
set(store_SRCS
    ${store_path}/database.cpp
)

generate_flatbuffers(calendar)

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp ${store_SRCS})
        qt5_use_modules(${_testname} Core Test)
        target_link_libraries(${_testname} kyotocabinet)
    endforeach(_testname)
endmacro(auto_tests)

manual_tests (
    storagebenchmark
)
