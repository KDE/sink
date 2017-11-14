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
target_link_libraries(sink_test
    sink
    libhawd
    Qt5::Core
    Qt5::Concurrent
    Qt5::Test
    KF5::Mime
)

add_executable(dbwriter dbwriter.cpp)
target_link_libraries(dbwriter sink)

include(SinkTest)

manual_tests (
    storagebenchmark
    dummyresourcebenchmark
    mailquerybenchmark
    pipelinebenchmark
    dummyresourcewritebenchmark
    databasepopulationandfacadequerybenchmark
)

auto_tests (
    clientapitest
    resourceconfigtest
    storagetest
    dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
    resourcecommunicationtest
    pipelinetest
    querytest
    modelinteractivitytest
    inspectiontest
    accountstest
    testaccounttest
    dummyresourcemailtest
    interresourcemovetest
    notificationtest
    entitystoretest
)
generate_flatbuffers(sink_test calendar)
target_link_libraries(dummyresourcetest sink_resource_dummy)
target_link_libraries(dummyresourcebenchmark sink_resource_dummy)
target_link_libraries(dummyresourcewritebenchmark sink_resource_dummy)
target_link_libraries(querytest sink_resource_dummy)
target_link_libraries(modelinteractivitytest sink_resource_dummy)
target_link_libraries(inspectiontest sink_resource_dummy)
target_link_libraries(notificationtest sink_resource_dummy)
