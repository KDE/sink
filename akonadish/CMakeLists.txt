project(akonadish)

find_package(Readline REQUIRED)


set(akonadi2_cli_SRCS
    main.cpp
    syntaxtree.cpp
    syntax_modules/core_syntax.cpp
    syntax_modules/akonadi_list.cpp
    syntax_modules/akonadi_clear.cpp
    syntax_modules/akonadi_count.cpp
    syntax_modules/akonadi_create.cpp
    syntax_modules/akonadi_stat.cpp
    syntax_modules/akonadi_sync.cpp
    akonadish_utils.cpp
    repl/repl.cpp
    repl/replStates.cpp
    state.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${PROJECT_NAME} ${akonadi2_cli_SRCS})
target_link_libraries(${PROJECT_NAME} Qt5::Core ${Readline_LIBRARY} akonadi2common)
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

