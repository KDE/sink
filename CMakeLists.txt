cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0028 NEW)
cmake_policy(SET CMP0063 NEW)

project(sink VERSION 0.9.0)

option(BUILD_MAILDIR "BUILD_MAILDIR" ON)
option(BUILD_DAV "BUILD_DAV" ON)
option(CATCH_ERRORS "CATCH_ERRORS" OFF)
# Requires the docker container from the kube repo
option(ENABLE_INTEGRATION_TESTS "Enable integration tests" OFF)

# ECM setup
find_package(ECM 5.29.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH
        ${ECM_MODULE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_MODULE_PATH})
set(CMAKE_CXX_STANDARD 17)

include(FeatureSummary)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(ECMSetupVersion)
include(KDEInstallDirs)
include(KDECompilerSettings)
#Avoid building appstreamtest
set(KDE_SKIP_TEST_SETTINGS true)
#Pick up rpath settings
include(KDECMakeSettings NO_POLICY_SCOPE)
#We only have console applications here
set(CMAKE_MACOSX_BUNDLE OFF)
set(CMAKE_WIN32_EXECUTABLE OFF)

ecm_setup_version(PROJECT
                  SOVERSION sink_VERSION_MAJOR
                  VERSION_HEADER sink_version.h
                  )

find_package(Qt5 COMPONENTS REQUIRED Core Concurrent Network Gui Test)
find_package(KF5 COMPONENTS REQUIRED Mime Contacts CalendarCore)
find_package(FlatBuffers REQUIRED 1.4.0)
find_package(KAsync REQUIRED 0.3)
find_package(LMDB REQUIRED 0.9)
find_package(Xapian REQUIRED 1.4)
find_package(Gpgme REQUIRED)
find_package(zstd REQUIRED)

#Clang-format support
add_custom_command(
    OUTPUT  format.dummy
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND clang-format -i ${CMAKE_SOURCE_DIR}/**.{cpp,h}
)
add_custom_target(format DEPENDS format.dummy)

function(generate_flatbuffers _target)
    foreach(fbs ${ARGN})
        #Necessary because we can get relative paths as name, e.g. commands/create_entity
        get_filename_component(filename ${fbs} NAME)
        #We first generate into a temporary directory to avoid changing the timestamp of the actual dependency unnecessarily.
        #Otherwise we'd end up unnecessarily rebuilding the target.
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h
            COMMAND ${FLATBUFFERS_FLATC_EXECUTABLE} -c -b -o ${CMAKE_CURRENT_BINARY_DIR}/flatbufferstmp ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_BINARY_DIR}/flatbufferstmp/${filename}_generated.h
                ${CMAKE_CURRENT_BINARY_DIR}/${filename}_generated.h
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs

        )
        target_sources(${_target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h)
        set_property(SOURCE ${fbs}_generated.h PROPERTY SKIP_AUTOMOC ON)
    endforeach(fbs)
endfunction(generate_flatbuffers)


set(CMAKE_AUTOMOC ON)
if (${CATCH_ERRORS})
    add_definitions("-Werror -Wall -Weverything -Wno-unused-function -Wno-cast-align -Wno-used-but-marked-unused -Wno-shadow -Wno-weak-vtables -Wno-global-constructors -Wno-deprecated -Wno-weak-template-vtables -Wno-exit-time-destructors -Wno-covered-switch-default -Wno-shorten-64-to-32 -Wno-documentation -Wno-old-style-cast -Wno-extra-semi -Wno-unused-parameter -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded -Wno-missing-noreturn -Wno-missing-prototypes -Wno-documentation-unknown-command -Wno-sign-conversion -Wno-gnu-zero-variadic-macro-arguments -Wno-disabled-macro-expansion -Wno-vla-extension -Wno-vla -Wno-undefined-func-template -Wno-#warnings -Wno-unused-template -Wno-inconsistent-missing-destructor-override -Wno-zero-as-null-pointer-constant -Wno-unused-lambda-capture -Wno-switch-enum -Wno-redundant-parens -Wno-extra-semi-stmt -Wno-suggest-destructor-override")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    # Workaround for older cmake versions
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++20")
    # We get way to many warnings for this
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nonportable-include-path")
endif()

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/common 3rdparty)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/common ${CMAKE_SOURCE_DIR}/common/domain ${CMAKE_SOURCE_DIR}/mime)

configure_file(hawd.conf hawd.conf)

enable_testing()

set(SINK_RESOURCE_PLUGINS_PATH ${QT_PLUGIN_INSTALL_DIR}/sink/resources)

# mime support
add_subdirectory(mime)

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
