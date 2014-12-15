project(akonadi2_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(akonadi2client_SRCS
    resourceaccess.cpp
    threadboundary.cpp
)

add_library(${PROJECT_NAME} SHARED ${akonadi2client_SRCS})
target_link_libraries(${PROJECT_NAME} akonadi2common)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION lib)

add_executable(${PROJECT_NAME}_bin main.cpp)
set_target_properties(${PROJECT_NAME}_bin PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(${PROJECT_NAME}_bin ${PROJECT_NAME} akonadi2common)
qt5_use_modules(${PROJECT_NAME}_bin Widgets Network)
install(TARGETS ${PROJECT_NAME}_bin DESTINATION bin)

add_subdirectory(test)
