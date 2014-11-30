cmake_minimum_required(VERSION 2.8)


# ECM setup
find_package(ECM 0.0.10 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH
        ${ECM_MODULE_PATH}
        #        ${ECM_KDE_MODULE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        ${CMAKE_MODULE_PATH})

include(FeatureSummary)
include(GenerateExportHeader)
#include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMPackageConfigHelpers)
# include(KDEInstallDirs)
# include(KDEFrameworkCompilerSettings)
# include(KDECMakeSettings)

find_package(Qt5Core REQUIRED)
find_package(FlatBuffers REQUIRED)
function(generate_flatbuffers dest)
    # TODO: move the file from ${fbs}_generated.h to just ${fbs}?
    foreach(fbs ${ARGN})
       execute_process(COMMAND ${FLATBUFFERS_FLATC_EXECUTABLE} -c -b --gen-includes -o ${dest} ${CMAKE_CURRENT_SOURCE_DIR}/${fbs})
    endforeach(fbs)
endfunction(generate_flatbuffers)

set(CMAKE_AUTOMOC ON)
add_definitions("-Wall -std=c++0x")
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

# common, eventually a lib but right now just the command buffers
add_subdirectory(common)

# the client
add_subdirectory(client)

# the resource
add_subdirectory(resource)

# the buffertest
add_subdirectory(buffertest)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
