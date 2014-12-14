project(akonadi2common)
generate_flatbuffers(commands/handshake
                     commands/revisionupdate)

if (STORAGE_unqlite)
    add_definitions(-DUNQLITE_ENABLE_THREADS -fpermissive)
    set(storage_SRCS unqlite/unqlite.c storage_unqlite.cpp)
else (STORAGE_unqlite)
    set(storage_SRCS storage_lmdb.cpp)
    set(storage_LIBS lmdb)
endif (STORAGE_unqlite)

set(command_SRCS
    commands.cpp
    console.cpp
    storage_common.cpp
    ${storage_SRCS})

message("We have: ${storage_SRCS}")
add_library(${PROJECT_NAME} ${command_SRCS})
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
qt5_use_modules(${PROJECT_NAME} Widgets)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS})
