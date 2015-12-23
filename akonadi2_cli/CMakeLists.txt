project(akonadi2_cli)

find_package(Readline REQUIRED)


set(akonadi2_cli_SRCS
    main.cpp
    syntaxtree.cpp
    syntax_modules/core_syntax.cpp
    repl/repl.cpp
    repl/replStates.cpp
    state.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${PROJECT_NAME} ${akonadi2_cli_SRCS})
target_link_libraries(${PROJECT_NAME} Qt5::Core ${Readline_LIBRARY})
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

