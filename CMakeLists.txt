cmake_minimum_required(VERSION 2.8.12)


# ECM setup
find_package(ECM 0.0.10 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH
        ${ECM_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        ${CMAKE_MODULE_PATH})

include(FeatureSummary)
include(GenerateExportHeader)
include(ECMGenerateHeaders)
include(ECMPackageConfigHelpers)
include(KDEInstallDirs)

find_package(Qt5 COMPONENTS REQUIRED Core Widgets Network)
find_package(KF5 COMPONENTS REQUIRED Async)
find_package(FlatBuffers REQUIRED)
add_custom_target(generate_bindings)
function(generate_flatbuffers _target)
    foreach(fbs ${ARGN})
       message("making ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h from ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs")
       add_custom_command(
              OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h
              COMMAND ${FLATBUFFERS_FLATC_EXECUTABLE} -c -b -o ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs
              DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs
            )
       set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h PROPERTIES GENERATED 1)
       string(REGEX REPLACE "/" "_" target_name ${fbs})
       add_custom_target(${_target}-generate_bindings${target_name} DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h)
       add_dependencies(generate_bindings ${_target}-generate_bindings${target_name})
    endforeach(fbs)
endfunction(generate_flatbuffers)

set(CMAKE_AUTOMOC ON)
add_definitions("-Wall -std=c++0x -g")
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/common)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/common ${CMAKE_SOURCE_DIR}/common/domain)

configure_file(hawd.conf hawd.conf)

enable_testing()

set(AKONADI2_RESOURCE_PLUGINS_PATH ${QT_PLUGIN_INSTALL_DIR}/akonadi2/resources)

# common, eventually a lib but right now just the command buffers
add_subdirectory(common)

# the synchronizer
add_subdirectory(synchronizer)

# example implementations
add_subdirectory(examples)

# some tests
add_subdirectory(tests)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
