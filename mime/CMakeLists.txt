include_directories(. ${GPGME_INCLUDE_DIRS})

add_subdirectory(mimetreeparser)

add_library(crypto STATIC
    crypto.cpp
)
target_link_libraries(crypto
    PUBLIC
    Qt5::Core
    Gpgme::Gpgme
)
