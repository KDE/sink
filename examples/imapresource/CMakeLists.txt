project(sink_resource_imap)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime)
find_package(KIMAP2 0.2 REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_library(${PROJECT_NAME} SHARED imapresource.cpp imapserverproxy.cpp)
target_link_libraries(${PROJECT_NAME} sink Qt5::Core Qt5::Network KF5::Mime KIMAP2)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH} RUNTIME DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

if (WIN32)
    message("Not building imap resource tests on windows")
else()
    add_subdirectory(tests)
endif()
