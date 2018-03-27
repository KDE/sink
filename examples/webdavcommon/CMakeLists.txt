project(sink_webdav_common)

set(CMAKE_CXX_STANDARD 14)

find_package(KPimKDAV2 REQUIRED)

add_library(${PROJECT_NAME} STATIC webdav.cpp)
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Network KPim::KDAV2)
