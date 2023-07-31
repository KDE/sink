

function(add_memcheck_test name binary)
    if (${ENABLE_MEMCHECK})
        add_manual_memcheck_test(${name} ${binary})
        add_test(memcheck_${name} ${memcheck_command} ./${binary} ${ARGN})
    endif()
endfunction(add_memcheck_test)

function(add_manual_memcheck_test name binary)
    if (${ENABLE_MEMCHECK})
        set(memcheck_command "${MEMORYCHECK_COMMAND} ${MEMORYCHECK_COMMAND_OPTIONS}")
        if (NOT memcheck_command)
            message(FATAL_ERROR "memcheck_command not defined. ${memcheck_command}")
        endif()
        separate_arguments(memcheck_command)
    endif()
endfunction(add_memcheck_test)

macro(auto_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        add_test(NAME ${_testname} COMMAND $<TARGET_FILE:${_testname}>)
        add_memcheck_test(${_testname} ${_testname})
        target_link_libraries(${_testname}
            sink libhawd
            sink_test
            Qt5::Core
            Qt5::Concurrent
            Qt5::Test
        )
    endforeach(_testname)
endmacro(auto_tests)

macro(integration_tests)
    if (${ENABLE_INTEGRATION_TESTS})
        auto_tests(${ARGN})
    endif()
endmacro(integration_tests)

macro(manual_tests)
    foreach(_testname ${ARGN})
        add_executable(${_testname} ${_testname}.cpp)
        add_manual_memcheck_test(${_testname} ${_testname})
        target_link_libraries(${_testname}
            sink
            libhawd
            sink_test
            Qt5::Core
            Qt5::Concurrent
            Qt5::Test
        )
    endforeach(_testname)
endmacro(manual_tests)
