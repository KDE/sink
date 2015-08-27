import QtQuick 2.0
import QtQuick.Layouts 1.1
import QtQuick.Controls 1.2
import QtQuick.Dialogs 1.2

import org.kde.akonadi2.config 0.1 as Config

Item {
    id: root

    Config.ICalFile {
        id: config

        accountId: "testICalAccount" //TODO
    }

    GroupBox {

        width: parent.width

        title: i18n("ICal Calendar File Config")

        ColumnLayout {

            width: parent.width

            RowLayout {

                width: parent.width

                Label {
                    text: i18n("Filename:")
                }

                TextField {

                    Layout.fillWidth: true

                    text: config.filePath
                    onTextChanged: config.filePath = text

                    //FIXME display a pretty file url / just the filename
                    //FIXME maybe make this not editable.
                }

                Button {
                    iconName: "folder"

                    onClicked: fileDialog.open()

                    FileDialog {
                        id: fileDialog

                        onAccepted: config.filePath = fileUrl
                    }
                }
            }

            RowLayout {
                width: parent.width

                Label {
                    text: i18n("Name:") //FIXME propper layout instead of spaces
                }

                TextField {
                    Layout.fillWidth: true

                    text: config.displayName

                    onTextChanged: config.displayName = text
                }
            }

            CheckBox {
                text: i18n("Read only")

                checked: config.readOnly
                onCheckedChanged: {
                    config.readOnly = checked
                    checked = Qt.binding(function() { return config.readOnly; })
                }
            }

            CheckBox {
                text: i18n("Enable file monitoring")

                checked: config.monitoringEnabled
                onCheckedChanged: {
                    config.monitoringEnabled = checked
                    checked = Qt.binding(function() { return config.monitoringEnabled; })
                }
            }
        }
    }

    Button {

        anchors {
            bottom: parent.bottom
            right: parent.right
        }

        text: i18n("save")

        onClicked: {
            config.saveConfig();
        }
    }
}


