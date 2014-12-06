project(akonadi2common)
generate_flatbuffers(commands/handshake
                     commands/revisionupdate)

if (STORAGE_KYOTO)
    set(storage_SRCS storage_kyoto.cpp)
    set(storage_LIBS kyotocabinet)
else (STORAGE_KYOTO)
    set(storage_SRCS storage_lmdb.cpp)
    set(storage_LIBS lmdb)
endif (STORAGE_KYOTO)

set(command_SRCS
   commands.cpp
   console.cpp
   storage_common.cpp
   ${storage_SRCS})

add_library(${PROJECT_NAME} ${command_SRCS})
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
qt5_use_modules(${PROJECT_NAME} Widgets)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS})
