project(akonadi2_resource_dummy)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

generate_flatbuffers(dummycalendar)

#Client plugin
add_library(${PROJECT_NAME} SHARED facade.cpp)
target_link_libraries(${PROJECT_NAME} akonadi2common akonadi2_client)
qt5_use_modules(${PROJECT_NAME} Core)
#install(TARGETS ${PROJECT_NAME} DESTINATION lib)

#Syncronizer

#add_subdirectory(test)
