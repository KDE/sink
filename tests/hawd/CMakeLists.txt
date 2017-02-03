project(hawd)

find_package(Libgit2)

if (LIBGIT2_FOUND)
    add_definitions(-DHAVE_LIBGIT2)
    include_directories(${LIBGIT2_INCLUDE_DIR})
endif (LIBGIT2_FOUND)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(lib_SRCS
    dataset.cpp
    datasetdefinition.cpp
    state.cpp
    formatter.cpp
)

set(SRCS
    main.cpp
    module.cpp
    modules/list.cpp
    modules/check.cpp
    modules/print.cpp
)

add_library(lib${PROJECT_NAME} SHARED ${lib_SRCS})
generate_export_header(lib${PROJECT_NAME} BASE_NAME HAWD EXPORT_FILE_NAME hawd_export.h)
qt5_use_modules(lib${PROJECT_NAME} Core)
target_link_libraries(lib${PROJECT_NAME} sink)
if (LIBGIT2_FOUND)
   target_link_libraries(lib${PROJECT_NAME} ${LIBGIT2_LIBRARIES})
endif(LIBGIT2_FOUND)

install(TARGETS lib${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

add_executable(${PROJECT_NAME} ${SRCS})
qt5_use_modules(${PROJECT_NAME} Core)
target_link_libraries(${PROJECT_NAME} lib${PROJECT_NAME})
if (LIBGIT2_FOUND)
   target_link_libraries(${PROJECT_NAME} ${LIBGIT2_LIBRARIES})
endif(LIBGIT2_FOUND)
install(TARGETS ${PROJECT_NAME} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

