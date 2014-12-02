project(akonadinextcommon)
generate_flatbuffers(commands/handshake
                     commands/revisionupdate)

set(command_SRCS
   console.cpp
   ${CMAKE_CURRENT_BINARY_DIR}/commands/handshake_generated.h)

add_library(${PROJECT_NAME} ${command_SRCS})
qt5_use_modules(${PROJECT_NAME} Widgets)
