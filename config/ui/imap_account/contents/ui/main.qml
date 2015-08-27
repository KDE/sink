import QtQuick 2.4
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0

import org.kde.akonadi2.config 0.1 as Config

Item {
    id: root

    Config.ImapAccount {
        id: account

        accountId: "testAccount"
    }

    Imap {
        anchors {
            top: parent.top
            bottom: saveButton.bottom
            left: parent.left
            right: parent.right
        }

        imap: account.imap
    }

    //TODO: Identity
    //TODO: SMTP

    Button {
        id: saveButton

        anchors {
            bottom: parent.bottom
            right: parent.right
        }

        text: "save"

        onClicked: {
            account.saveConfig()
        }
    }
}
