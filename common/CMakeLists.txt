include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(domain)

project(sinkcommon)

ecm_setup_version("0.1" VARIABLE_PREFIX SinkCommon
                        VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/sinkcommon_version.h"
                        PACKAGE_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/SinkCommonConfigVersion.cmake"
                        SOVERSION 0
)

########### CMake Config Files ###########
set(CMAKECONFIG_INSTALL_DIR "${KDE_INSTALL_CMAKEPACKAGEDIR}/SinkCommon")

ecm_configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/SinkCommonConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/SinkCommonConfig.cmake"
  INSTALL_DESTINATION  ${CMAKECONFIG_INSTALL_DIR}
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/SinkCommonConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SinkCommonConfigVersion.cmake"
  DESTINATION "${CMAKECONFIG_INSTALL_DIR}"
  COMPONENT Devel
)

add_definitions("-fvisibility=hidden")

install(EXPORT SinkCommonTargets DESTINATION "${CMAKECONFIG_INSTALL_DIR}" FILE SinkCommonTargets.cmake)

set(storage_SRCS storage_lmdb.cpp)
set(storage_LIBS lmdb)

set(command_SRCS
    modelresult.cpp
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
    queryrunner.cpp
    listener.cpp
    storage_common.cpp
    threadboundary.cpp
    messagequeue.cpp
    index.cpp
    typeindex.cpp
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
    commands/inspection
    domain/event
    domain/mail
    domain/folder
    domain/dummy
    entity
    metadata
    queuedcommand
)

generate_export_header(${PROJECT_NAME} BASE_NAME SinkCommon EXPORT_FILE_NAME sinkcommon_export.h)
SET_TARGET_PROPERTIES(${PROJECT_NAME}
    PROPERTIES LINKER_LANGUAGE CXX
    VERSION "0.1"
    SOVERSION 0
    EXPORT_NAME ${PROJECT_NAME}
)
qt5_use_modules(${PROJECT_NAME} Network)
target_link_libraries(${PROJECT_NAME} ${storage_LIBS} KF5::Async)
install(TARGETS ${PROJECT_NAME}
    EXPORT SinkCommonTargets ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} ${LIBRARY_NAMELINK} )

add_clang_static_analysis(${PROJECT_NAME})

install(FILES
    clientapi.h
    domain/applicationdomaintype.h
    query.h
    inspection.h
    notification.h
    bufferadaptor.h
    ${CMAKE_CURRENT_BINARY_DIR}/sinkcommon_export.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/${PROJECT_NAME} COMPONENT Devel
)

