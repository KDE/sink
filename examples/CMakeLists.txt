# a simple dummy resource implementation
add_subdirectory(dummyresource)

if (BUILD_MAILDIR)
    # a maildir resource implementation
    add_subdirectory(maildirresource)
    # an imap resource implementation
    add_subdirectory(imapresource)
endif()
add_subdirectory(mailtransportresource)
add_subdirectory(davresource)
