project(akonadi2_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} akonadi2common)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
