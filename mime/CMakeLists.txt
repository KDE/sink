include_directories(. ${GPGME_INCLUDE_DIRS})

add_library(crypto STATIC)
target_sources(crypto PUBLIC
    crypto.cpp
)
target_link_libraries(crypto
    PUBLIC
    Qt5::Core
    Gpgme::Gpgme
)

install(FILES
    crypto.h
    errors.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/sink COMPONENT Devel
)

add_subdirectory(mimetreeparser)
add_subdirectory(tests)
