set(configplugin_SRCS
    configplugin.cpp
    icalcalendarfile.cpp
    imap.cpp
    smtp.cpp
    identity.cpp
    imapaccount.cpp
)

add_library(configplugin SHARED ${configplugin_SRCS})

qt5_use_modules(configplugin Core Quick Qml)

install(TARGETS configplugin DESTINATION ${QML_INSTALL_DIR}/org/kde/akonadi2/config)
install(FILES qmldir DESTINATION ${QML_INSTALL_DIR}/org/kde/akonadi2/config)