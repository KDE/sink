project(akonadi2_dummyresource)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

generate_flatbuffers(dummycalendar)

#Client plugin
add_library(${PROJECT_NAME}_facade SHARED facade.cpp)
target_link_libraries(${PROJECT_NAME}_facade akonadi2common akonadi2_client)
qt5_use_modules(${PROJECT_NAME}_facade Widgets Network)
#install(TARGETS ${PROJECT_NAME}_facade DESTINATION bin)

#Syncronizer

#add_subdirectory(test)
