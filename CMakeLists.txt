cmake_minimum_required(VERSION 3.0)

cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0028 NEW)

project(sink VERSION 0.1.0)

option(BUILD_MAILDIR "BUILD_MAILDIR" ON)
option(BUILD_DAV "BUILD_DAV" OFF)
option(AVOID_BINDING_REBUILD "AVOID_BINDING_REBUILD" OFF)
option(CATCH_ERRORS "CATCH_ERRORS" OFF)
option(ENABLE_MEMCHECK "Build valgrind tests" OFF)

# ECM setup
find_package(ECM 1.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH
        ${ECM_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_MODULE_PATH})

include(FeatureSummary)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(ECMSetupVersion)
include(KDEInstallDirs)

find_package(Qt5 COMPONENTS REQUIRED Core Network Gui)
find_package(KF5 COMPONENTS REQUIRED Mime Contacts)
find_package(FlatBuffers REQUIRED)
find_package(KAsync REQUIRED 0.1.0)
find_package(LMDB REQUIRED)

find_program(MEMORYCHECK_COMMAND valgrind)
set(MEMORYCHECK_COMMAND_OPTIONS "--trace-children=yes --leak-check=full")

#Clang-format support
add_custom_command(
    OUTPUT  format.dummy
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND clang-format -i ${CMAKE_SOURCE_DIR}/**.{cpp,h}
)
add_custom_target(format DEPENDS format.dummy)

#Generate flatbuffer bindings
if (${AVOID_BINDING_REBUILD})
    add_custom_target(generate_bindings)
else()
    add_custom_target(generate_bindings ALL)
endif()

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

#Clang-analyze support
add_custom_target(analyze)
function(add_clang_static_analysis target)
    get_target_property(SRCs ${target} SOURCES)
    get_target_property(INCLUDEs ${target} INCLUDE_DIRECTORIES)
    add_library(${target}_analyze OBJECT EXCLUDE_FROM_ALL ${SRCs})
    set_target_properties(${target}_analyze PROPERTIES
        COMPILE_OPTIONS "--analyze"
        EXCLUDE_FROM_DEFAULT_BUILD true
        INCLUDE_DIRECTORIES "${INCLUDEs};${KDE_INSTALL_FULL_INCLUDEDIR}/KF5/" # Had to hardcode include directory to find KAsync includes
        #COMPILE_FLAGS is deprecated, but the only way that -Xanalyzer isn't erronously deduplicated
        COMPILE_FLAGS "-Xanalyzer -analyzer-eagerly-assume -Xanalyzer -analyzer-opt-analyze-nested-blocks"
    )
    target_compile_options(${target}_analyze PRIVATE ${Qt5Core_EXECUTABLE_COMPILE_FLAGS})# Necessary to get options such as fPIC
    add_dependencies(analyze ${target}_analyze)
endfunction()

set(CMAKE_AUTOMOC ON)
if (${CATCH_ERRORS})
    add_definitions("-Werror -Wall -Weverything -Wno-unused-function -Wno-cast-align -Wno-used-but-marked-unused -Wno-shadow -Wno-weak-vtables -Wno-global-constructors -Wno-deprecated -Wno-weak-template-vtables -Wno-exit-time-destructors -Wno-covered-switch-default -Wno-shorten-64-to-32 -Wno-documentation -Wno-old-style-cast -Wno-extra-semi -Wno-unused-parameter -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded -Wno-missing-noreturn -Wno-missing-prototypes -Wno-documentation-unknown-command -Wno-sign-conversion -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -Wno-vla-extension -Wno-vla")
endif()
add_definitions("-std=c++0x -g")
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/common 3rdparty)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/common ${CMAKE_SOURCE_DIR}/common/domain)

configure_file(hawd.conf hawd.conf)

enable_testing()

set(SINK_RESOURCE_PLUGINS_PATH ${QT_PLUGIN_INSTALL_DIR}/sink/resources)

# common, eventually a lib but right now just the command buffers
add_subdirectory(common)

# the synchronizer
add_subdirectory(synchronizer)

# example implementations
add_subdirectory(examples)

# some tests
add_subdirectory(tests)

# cli
add_subdirectory(sinksh)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
