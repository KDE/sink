project(sink_resource_imap)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

find_package(KF5 COMPONENTS REQUIRED Mime IMAP KIO)
find_package(KF5CoreAddons REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_library(${PROJECT_NAME} SHARED facade.cpp imapresource.cpp domainadaptor.cpp imapserverproxy.cpp)
qt5_use_modules(${PROJECT_NAME} Core Network)
#We need CoreAddons for KJob and KIOCore for KTcpSocket. Both used in KIMAP
target_link_libraries(${PROJECT_NAME} sink KF5::Mime KF5::IMAP KF5::CoreAddons KF5::KIOCore)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${SINK_RESOURCE_PLUGINS_PATH})

add_subdirectory(tests)
