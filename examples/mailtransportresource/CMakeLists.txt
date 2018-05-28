project(sink_resource_mailtransport)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)
find_package(CURL 7.20.0 REQUIRED)

include_directories(${CURL_INCLUDE_DIRS})


add_library(${PROJECT_NAME} SHARED mailtransportresource.cpp mailtransport.cpp)
target_link_libraries(${PROJECT_NAME}
    sink
    Qt5::Core
    Qt5::Network
    KF5::Mime
    ${CURL_LIBRARIES}
)
install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

add_executable(sink_smtp_test smtptest.cpp mailtransport.cpp)
target_link_libraries(sink_smtp_test
    sink
    Qt5::Core
    Qt5::Network
    KF5::Mime
    ${CURL_LIBRARIES}
)
install(TARGETS sink_smtp_test ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if (WIN32)
    message("Not building mailtransport resource tests on windows")
else()
    add_subdirectory(tests)
endif()
