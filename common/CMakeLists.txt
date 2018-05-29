include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(domain)

ecm_setup_version(${sink_VERSION}
    VARIABLE_PREFIX Sink
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/sink_version.h"
    PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/SinkConfigVersion.cmake"
    SOVERSION 0
)

########### CMake Config Files ###########
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/Sink")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SinkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SinkConfig.cmake"
    INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SinkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SinkConfigVersion.cmake"
    DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
    COMPONENT Devel
)

set(CMAKE_CXX_VISIBILITY_PRESET default)

install(EXPORT SinkTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE SinkTargets.cmake)

add_library(${PROJECT_NAME} SHARED
    store.cpp
    secretstore.cpp
    notifier.cpp
    resourcecontrol.cpp
    modelresult.cpp
    definitions.cpp
    log.cpp
    entitybuffer.cpp
    facadefactory.cpp
    commands.cpp
    facade.cpp
    pipeline.cpp
    propertymapper.cpp
    domainadaptor.cpp
    resource.cpp
    genericresource.cpp
    resourceaccess.cpp
    queryrunner.cpp
    listener.cpp
    storage_common.cpp
    threadboundary.cpp
    messagequeue.cpp
    index.cpp
    typeindex.cpp
    resourcefacade.cpp
    resourceconfig.cpp
    configstore.cpp
    resultset.cpp
    domain/propertyregistry.cpp
    domain/applicationdomaintype.cpp
    domain/typeimplementations.cpp
    test.cpp
    query.cpp
    changereplay.cpp
    adaptorfactoryregistry.cpp
    synchronizer.cpp
    synchronizerstore.cpp
    contactpreprocessor.cpp
    mailpreprocessor.cpp
    eventpreprocessor.cpp
    todopreprocessor.cpp
    specialpurposepreprocessor.cpp
    datastorequery.cpp
    storage/entitystore.cpp
    indexer.cpp
    mail/threadindexer.cpp
    mail/fulltextindexer.cpp
    notification.cpp
    commandprocessor.cpp
    inspector.cpp
    propertyparser.cpp
    utils.cpp
    fulltextindex.cpp
    storage_lmdb.cpp)


generate_flatbuffers(
    ${PROJECT_NAME}

    commands/commandcompletion
    commands/createentity
    commands/deleteentity
    commands/handshake
    commands/modifyentity
    commands/revisionupdate
    commands/synchronize
    commands/notification
    commands/revisionreplayed
    commands/inspection
    commands/flush
    commands/secret
    domain/contact
    domain/addressbook
    domain/event
    domain/todo
    domain/calendar
    domain/mail
    domain/folder
    domain/dummy
    entity
    metadata
    queuedcommand
)

generate_export_header(${PROJECT_NAME} BASE_NAME Sink EXPORT_FILE_NAME sink_export.h)
SET_TARGET_PROPERTIES(${PROJECT_NAME}
    PROPERTIES LINKER_LANGUAGE CXX
    VERSION ${Sink_VERSION}
    SOVERSION ${Sink_SOVERSION}
    EXPORT_NAME ${PROJECT_NAME}
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
    KAsync
    Qt5::Network
PRIVATE
    ${LMDB_LIBRARIES}
    Qt5::Gui
    KF5::Mime
    KF5::Contacts
    KF5::CalendarCore
    ${XAPIAN_LIBRARIES}
)
install(TARGETS ${PROJECT_NAME}
    EXPORT SinkTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} ${LIBRARY_NAMELINK} )

add_clang_static_analysis(${PROJECT_NAME})

install(FILES
    store.h
    notifier.h
    resourcecontrol.h
    domain/applicationdomaintype.h
    query.h
    standardqueries.h
    inspection.h
    notification.h
    bufferadaptor.h
    test.h
    log.h
    flush.h
    secretstore.h
    ${CMAKE_CURRENT_BINARY_DIR}/sink_export.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/${PROJECT_NAME} COMPONENT Devel
)

