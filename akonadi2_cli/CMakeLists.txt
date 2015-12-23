project(akonadi2_cli)

find_package(Readline REQUIRED)


set(akonadi2_SRCS
    main.cpp
    module.cpp
    modules/exit/exit.cpp
    modules/help/help.cpp
    repl/repl.cpp
    repl/replStates.cpp
    state.cpp)

add_executable(${PROJECT_NAME} ${akonadi2_SRCS})

