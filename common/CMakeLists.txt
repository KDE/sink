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
    clientapi.cpp
    commands.cpp
    console.cpp
    resource.cpp
    resourceaccess.cpp
    storage_common.cpp
    threadboundary.cpp
    ${storage_SRCS})

add_library(${PROJECT_NAME} SHARED ${command_SRCS})
generate_export_header(${PROJECT_NAME} BASE_NAME Akonadi2Common EXPORT_FILE_NAME akonadi2common_export.h)
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS})
install(TARGETS ${PROJECT_NAME} DESTINATION lib)

add_subdirectory(test)
