project(akonadi2_resource_dummy)

add_definitions(-DQT_PLUGIN)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

generate_flatbuffers(dummycalendar)

add_library(${PROJECT_NAME} SHARED facade.cpp resourcefactory.cpp domainadaptor.cpp resourcefacade.cpp)
qt5_use_modules(${PROJECT_NAME} Core Network)
target_link_libraries(${PROJECT_NAME} akonadi2common)

install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${AKONADI2_RESOURCE_PLUGINS_PATH})
