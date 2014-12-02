project(akonadinext_client)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(akonadinextclient_SRCS
    main.cpp
    resourceaccess.cpp
)

add_executable(${PROJECT_NAME} ${akonadinextclient_SRCS})
target_link_libraries(${PROJECT_NAME} akonadinextcommon)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

add_subdirectory(test)
