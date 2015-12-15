# add_subdirectory( tests )

set(maildir_LIB_SRCS keycache.cpp maildir.cpp)

add_library(maildir ${LIBRARY_TYPE} ${maildir_LIB_SRCS})
qt5_use_modules(maildir Core Network)
# set_target_properties(maildir PROPERTIES VERSION ${GENERIC_LIB_VERSION} SOVERSION ${GENERIC_LIB_SOVERSION} )
install(TARGETS maildir ${INSTALL_TARGETS_DEFAULT_ARGS})
