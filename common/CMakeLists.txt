project(akonadi2common)
generate_flatbuffers(
    commands/commandcompletion
    commands/createentity
    commands/deleteentity
    commands/fetchentity
    commands/handshake
    commands/modifyentity
    commands/revisionupdate
    commands/synchronize
    commands/notification
    domain/event
    entity
    metadata
    queuedcommand
)

if (STORAGE_unqlite)
    add_definitions(-DUNQLITE_ENABLE_THREADS -fpermissive)
    set(storage_SRCS unqlite/unqlite.c storage_unqlite.cpp)
else (STORAGE_unqlite)
    set(storage_SRCS storage_lmdb.cpp)
    set(storage_LIBS lmdb)
endif (STORAGE_unqlite)

set(command_SRCS
    log.cpp
    entitybuffer.cpp
    clientapi.cpp
    commands.cpp
    console.cpp
    facade.cpp
    pipeline.cpp
    domainadaptor.cpp
    resource.cpp
    resourceaccess.cpp
    storage_common.cpp
    threadboundary.cpp
    messagequeue.cpp
    index.cpp
    ${storage_SRCS})

add_library(${PROJECT_NAME} SHARED ${command_SRCS})
generate_export_header(${PROJECT_NAME} BASE_NAME Akonadi2Common EXPORT_FILE_NAME akonadi2common_export.h)
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS} akonadi2async)
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

add_subdirectory(test)
