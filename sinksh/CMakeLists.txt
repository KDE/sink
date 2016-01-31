project(sinksh)

find_package(Readline REQUIRED)

set(sink_cli_SRCS
    main.cpp
    syntaxtree.cpp
    syntax_modules/core_syntax.cpp
    syntax_modules/sink_list.cpp
    syntax_modules/sink_clear.cpp
    syntax_modules/sink_count.cpp
    syntax_modules/sink_create.cpp
    syntax_modules/sink_modify.cpp
    syntax_modules/sink_remove.cpp
    syntax_modules/sink_stat.cpp
    syntax_modules/sink_sync.cpp
    sinksh_utils.cpp
    repl/repl.cpp
    repl/replStates.cpp
    state.cpp
    utils.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${PROJECT_NAME} ${sink_cli_SRCS})
target_link_libraries(${PROJECT_NAME} Qt5::Core ${Readline_LIBRARY} sinkcommon)
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

