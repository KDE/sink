project(toynadi_buffertest)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(toynadinbuffertest_SRCS
    main.cpp
)

set(SCHEMAS calendar.fbs)
set(SCHEMA_SOURCEFILES calendar_generated.h)

add_custom_command(OUTPUT ${SCHEMA_SOURCEFILES}
    COMMAND flatc -c ${CMAKE_CURRENT_SOURCE_DIR}/calendar.fbs
    COMMENT "Generating buffers"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${SCHEMAS}
    VERBATIM
)

SET_SOURCE_FILES_PROPERTIES(${SCHEMA_SOURCEFILES} PROPERTIES GENERATED 1)
ADD_CUSTOM_TARGET(generate_buffers ALL DEPENDS ${SCHEMA_SOURCEFILES})

add_executable(${PROJECT_NAME} ${toynadinbuffertest_SRCS})
qt5_use_modules(${PROJECT_NAME} Core)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

