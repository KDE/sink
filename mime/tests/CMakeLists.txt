include(${CMAKE_SOURCE_DIR}/cmake/modules/add_gpg_crypto_test.cmake)
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
include_directories(${GPGME_INCLUDE_DIRS})

include(ECMAddTests)

add_executable(cryptotest cryptotest.cpp)
add_gpg_crypto_test(cryptotest cryptotest)
target_link_libraries(cryptotest
    crypto
    Qt5::Core
    Qt5::Test
    KF5::Mime
)
