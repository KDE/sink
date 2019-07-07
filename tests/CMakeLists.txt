add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/hawd
    ${CMAKE_CURRENT_BINARY_DIR}/../examples/dummyresource
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/
    )

add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_definitions(-DTHREADTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/threaddata")

find_package(KF5 COMPONENTS REQUIRED Mime)

add_library(sink_test SHARED testimplementations.cpp getrssusage.cpp mailtest.cpp mailsynctest.cpp mailthreadtest.cpp utils.cpp)
generate_export_header(sink_test BASE_NAME SinkTest EXPORT_FILE_NAME sinktest_export.h)
target_link_libraries(sink_test
    sink
    libhawd
    Qt5::Core
    Qt5::Concurrent
    Qt5::Test
    KF5::Mime
    KF5::CalendarCore
)

add_executable(dbwriter dbwriter.cpp)
target_link_libraries(dbwriter sink)

include(SinkTest)

manual_tests (
    storagebenchmark
    mailquerybenchmark
    pipelinebenchmark
    databasepopulationandfacadequerybenchmark
)

auto_tests (
    clientapitest
    resourceconfigtest
    storagetest
    domainadaptortest
    messagequeuetest
    indextest
    fulltextindextest
    resourcecommunicationtest
    pipelinetest
    synchronizertest
    querytest
    modelinteractivitytest
    inspectiontest
    accountstest
    testaccounttest
    dummyresourcemailtest
    interresourcemovetest
    notificationtest
    entitystoretest
    upgradetest
    resourcecontroltest
)

if (WIN32)
    message("Not building dummy resource tests on windows")
else()
    manual_tests (
        dummyresourcebenchmark
        dummyresourcewritebenchmark
    )
    auto_tests (
        dummyresourcetest
    )
    target_link_libraries(dummyresourcetest sink_resource_dummy)
    target_link_libraries(dummyresourcebenchmark sink_resource_dummy)
    target_link_libraries(dummyresourcewritebenchmark sink_resource_dummy)
endif()

