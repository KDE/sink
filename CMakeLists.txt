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

set(CMAKE_AUTOMOC ON)
add_definitions("-Wall -std=c++0x")
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

# the client
add_subdirectory(client)

# the resource
add_subdirectory(resource)

# the buffertest
add_subdirectory(buffertest)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
