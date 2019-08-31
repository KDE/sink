project(sink_webdav_common)

find_package(KPimKDAV2 REQUIRED 0.3)

add_library(${PROJECT_NAME} STATIC webdav.cpp)
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Network KPim::KDAV2 sink)
