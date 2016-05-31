add_subdirectory(hawd)

set(CMAKE_AUTOMOC ON)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/hawd
    ${CMAKE_CURRENT_BINARY_DIR}/../examples/dummyresource
    ${CMAKE_CURRENT_SOURCE_DIR}/../examples/
    )

add_definitions(-DTESTDATAPATH="${CMAKE_CURRENT_SOURCE_DIR}/data")

find_package(KF5 COMPONENTS REQUIRED Mime)

add_library(sink_test SHARED testimplementations.cpp getrssusage.cpp mailtest.cpp mailsynctest.cpp)
qt5_use_modules(sink_test Core Test Concurrent)
target_link_libraries(sink_test sink libhawd KF5::Mime)

include(SinkTest)

manual_tests (
    storagebenchmark
    dummyresourcebenchmark
#     genericresourcebenchmark
    mailquerybenchmark
    pipelinebenchmark
#     genericfacadebenchmark
)

auto_tests (
    clientapitest
    resourceconfigtest
    storagetest
    dummyresourcetest
    domainadaptortest
    messagequeuetest
    indextest
    # genericresourcetest
    # genericfacadetest
    resourcecommunicationtest
    pipelinetest
    querytest
    databasepopulationandfacadequerybenchmark
    dummyresourcewritebenchmark
    modelinteractivitytest
    inspectiontest
    accountstest
    testaccounttest
)
generate_flatbuffers(dummyresourcetest calendar)
target_link_libraries(dummyresourcetest sink_resource_dummy)
target_link_libraries(dummyresourcebenchmark sink_resource_dummy)
target_link_libraries(dummyresourcewritebenchmark sink_resource_dummy)
target_link_libraries(querytest sink_resource_dummy)
target_link_libraries(modelinteractivitytest sink_resource_dummy)
target_link_libraries(inspectiontest sink_resource_dummy)
