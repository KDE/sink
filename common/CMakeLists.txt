include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(domain)

project(akonadi2common)

if (STORAGE_unqlite)
    add_definitions(-DUNQLITE_ENABLE_THREADS -fpermissive)
    set(storage_SRCS unqlite/unqlite.c storage_unqlite.cpp)
else (STORAGE_unqlite)
    set(storage_SRCS storage_lmdb.cpp)
    set(storage_LIBS lmdb)
endif (STORAGE_unqlite)

set(command_SRCS
    definitions.cpp
    log.cpp
    entitybuffer.cpp
    clientapi.cpp
    facadefactory.cpp
    commands.cpp
    facade.cpp
    pipeline.cpp
    propertymapper.cpp
    domainadaptor.cpp
    resource.cpp
    genericresource.cpp
    resourceaccess.cpp
    listener.cpp
    storage_common.cpp
    threadboundary.cpp
    messagequeue.cpp
    index.cpp
    resourcefacade.cpp
    resourceconfig.cpp
    domain/applicationdomaintype.cpp
    domain/event.cpp
    domain/mail.cpp
    domain/folder.cpp
    ${storage_SRCS})

add_library(${PROJECT_NAME} SHARED ${command_SRCS})

generate_flatbuffers(
    ${PROJECT_NAME}

    commands/commandcompletion
    commands/createentity
    commands/deleteentity
    commands/fetchentity
    commands/handshake
    commands/modifyentity
    commands/revisionupdate
    commands/synchronize
    commands/notification
    commands/revisionreplayed
    domain/event
    domain/mail
    domain/folder
    domain/dummy
    entity
    metadata
    queuedcommand
)

generate_export_header(${PROJECT_NAME} BASE_NAME Akonadi2Common EXPORT_FILE_NAME akonadi2common_export.h)
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
qt5_use_modules(${PROJECT_NAME} Network)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS} KF5::Async)
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES
    clientapi.h
    domain/applicationdomaintype.h
    query.h
    threadboundary.h
    resultprovider.h
    facadefactory.h
    log.h
    listmodelresult.h
    bufferadaptor.h
    facadeinterface.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/${PROJECT_NAME} COMPONENT Devel
)
