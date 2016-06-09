include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(domain)

project(sink)

ecm_setup_version("0.1" VARIABLE_PREFIX Sink
                        VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/sink_version.h"
                        PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/SinkConfigVersion.cmake"
                        SOVERSION 0
)

########### CMake Config Files ###########
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/Sink")

ecm_configure_package_config_file(
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

add_definitions("-fvisibility=hidden")

install(EXPORT SinkTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE SinkTargets.cmake)

set(storage_SRCS storage_lmdb.cpp)
set(storage_LIBS lmdb)

set(command_SRCS
    store.cpp
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
    domain/applicationdomaintype.cpp
    domain/event.cpp
    domain/mail.cpp
    domain/folder.cpp
    test.cpp
    query.cpp
    changereplay.cpp
    adaptorfactoryregistry.cpp
    synchronizer.cpp
    entitystore.cpp
    remoteidmap.cpp
    sourcewriteback.cpp
    entityreader.cpp
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
    commands/inspection
    domain/event
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
    VERSION "0.1"
    SOVERSION 0
    EXPORT_NAME ${PROJECT_NAME}
)
qt5_use_modules(${PROJECT_NAME} Network)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS} KF5::Async)
install(TARGETS ${PROJECT_NAME}
    EXPORT SinkTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} ${LIBRARY_NAMELINK} )

add_clang_static_analysis(${PROJECT_NAME})

install(FILES
    store.h
    notifier.h
    resourcecontrol.h
    domain/applicationdomaintype.h
    query.h
    inspection.h
    notification.h
    bufferadaptor.h
    test.h
    log.h
    ${CMAKE_CURRENT_BINARY_DIR}/sink_export.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/${PROJECT_NAME} COMPONENT Devel
)

