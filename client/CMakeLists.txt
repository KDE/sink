project(akonadi2_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(akonadi2client_SRCS
    resourceaccess.cpp
    threadboundary.cpp
)

add_library(${PROJECT_NAME}_lib SHARED ${akonadi2client_SRCS})
target_link_libraries(${PROJECT_NAME}_lib akonadi2common)
qt5_use_modules(${PROJECT_NAME}_lib Widgets Network)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} akonadi2_client_lib akonadi2common)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

add_subdirectory(test)
