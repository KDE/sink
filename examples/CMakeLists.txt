# the client
add_subdirectory(client)

# a simple dummy resource implementation
add_subdirectory(dummyresource)

if (BUILD_MAILDIR)
    # a maildir resource implementation
    add_subdirectory(maildirresource)
endif()
