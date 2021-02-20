project(sink_webdav_common)

find_package(KPimKDAV2 REQUIRED 0.4)

add_library(${PROJECT_NAME} STATIC webdav.cpp)
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Gui Qt5::Network KPim::KDAV2 sink)
