import QtQuick 2.4
import QtQuick.Controls 1.3
import QtQuick.Layouts 1.0
import org.kde.akonadi2.config 0.1

Item {
    id: root

    property Imap imap

    TabView {
        id: tabView

        anchors.fill: parent

        Tab {
            title: "General"

            GroupBox  {
                id: generalBox

                ColumnLayout {

                    width: parent.width

                    GroupBox {
                        title: "Account Information"

                        Layout.fillWidth: true

                        GridLayout {

                            anchors {
                                horizontalCenter: parent.horizontalCenter
                            }

                            width: parent.width

                            columns: 2

                            Label {
                                text: "IMAP Server:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }
                            TextField {

                                Layout.fillWidth: true

                                text: imap.serverUrl
                                onTextChanged: imap.serverUrl = text;
                            }

                            Label {
                                text: "Username:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }
                            TextField {

                                Layout.fillWidth: true

                                text: imap.login
                                onTextChanged: imap.login = text
                            }

                            Label {
                                text: "Passphrase:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }
                            TextField {

                                Layout.fillWidth: true

                                text: imap.password
                                onTextChanged: imap.password = text
                            }
                        }
                    }

                    GroupBox {
                        title: "Mail Checking Options"

                        Layout.fillWidth: true

                        ColumnLayout {

                            CheckBox {
                                text: "Download all messages for offline use"

                                checked: imap.disconnectedModeEnabled
                                onCheckedChanged: imap.disconnectedModeEnabled = checked
                            }

                            CheckBox {
                                text: "Enable intervall mail checking"

                                checked: imap.intervalCheckEnabled
                                onCheckedChanged: imap.intervalCheckEnabled = checked
                            }

                            GridLayout {

                                width: parent.width

                                Label  {
                                    text: "Check mail intervall:"
                                }

                                SpinBox {

                                    value: imap.checkintervalInMinutes
                                    onValueChanged: imap.checkintervalInMinutes = value
                                }
                            }
                        }
                    }
                }
            }
        }

        Tab {
            title: "Filtering"

            GroupBox {

                ColumnLayout {

                    width: parent.width

                    CheckBox {
                        text: "Server supports Sieve"

                        checked: imap.serverSieveEnabled
                        onCheckedChanged: imap.serverSieveEnabled = checked
                    }

                    CheckBox {
                        text: "Reuse host and login configuration"

                        checked: imap.reuseLoginSieve
                        onCheckedChanged: imap.reuseLoginSieve = checked
                    }

                    RowLayout {

                        Label {
                            text: "Managesieve port:"
                        }

                        SpinBox {
                            value: imap.sievePort
                            onValueChanged: imap.sievePort = value
                        }
                    }

                    RowLayout {

                        Label {
                            text: "Alternate URL:"
                        }

                        TextField {
                            text: imap.sieveUrl
                            onTextChanged: imap.sieveUrl = text
                        }
                    }

                    GroupBox {
                        title: "Authentication"

                        Layout.fillWidth: true

                        ColumnLayout {
                            ExclusiveGroup {
                                id: sieveAuth
                                onCurrentChanged: {} //TODO

                            }

                            RadioButton {
                                text: "IMAP Username and Passphrase"
                                exclusiveGroup: sieveAuth
                            }
                            RadioButton {
                                text: "No Authentication"
                                exclusiveGroup: sieveAuth
                            }
                            RadioButton {
                                text: "Username and Passphrase"
                                exclusiveGroup: sieveAuth
                            }

                            RowLayout {

                                Label {
                                    text: "Username:"
                                }

                                TextField {
                                    text: imap.sieveLogin
                                    onTextChanged: imap.sieveLogin = text
                                }
                            }

                            RowLayout {

                                Label {
                                    text: "Password:"
                                }

                                TextField {
                                    text: imap.sievePassword
                                    onTextChanged: imap.sievePassword = text
                                }
                            }
                        }
                    }
                }
            }
        }

        Tab {
            title: "Advanced"

            GroupBox {

                ColumnLayout {

                    width: parent.width

                    GroupBox {
                        title: "IMAP Settings"

                        Layout.fillWidth: true

                        ColumnLayout {

                            width: parent.width

                            CheckBox {
                                text: "Enable server-side subscriptions"
                            }

                            //TODO server-side subsctiptions dialog

                            CheckBox {
                                text: "Automatically compact folders (expunges deleted messages)"
                            }

                            Label {
                                text: "Trashfolder:"
                            }

                            //TODO akoandi folder dialog

                        }
                    }

                    GroupBox {
                        title: "Connection Settings"

                        Layout.fillWidth: true

                        GridLayout {

                            anchors {
                                horizontalCenter: parent.horizontalCenter
                            }

                            columns: 2

                            Label {
                                text: "Encryption:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }

                            RowLayout  {

                                ExclusiveGroup { id: auth }

                                <RadioButton {
                                    text: "None"
                                    exclusiveGroup: auth
                                }
                                RadioButton {
                                    text: "SSL"
                                    exclusiveGroup: auth
                                }
                                RadioButton {
                                    text: "TLS"
                                    exclusiveGroup: auth
                                }
                            }

                            Label {
                                text: "Port:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }
                            SpinBox {
                                value: imap.port
                                maximumValue: 65536

                                onValueChanged: imap.port = value
                            }

                            Label {
                                text: "Authentication:"
                                Layout.alignment: Qt.AlignVCenter|Qt.AlignRight
                            }
                            ComboBox {

                                model: ListModel {
                                    ListElement { text: "LOGIN" }
                                    ListElement { text: "PLAIN" }
                                    ListElement { text: "CRAM-MD5" }
                                    ListElement { text: "DIGEST-MD5" }
                                    ListElement { text: "GSSAI" }
                                }

                                //currentIndex:
                                onCurrentIndexChanged: {
                                    //TODO
                                }
                            }
                        }
                    }
                }
            }
        }

        Tab {
            title: "Folder Archive"

            GroupBox {

                GroupBox {

                    anchors {
                        left: parent.left
                        right: parent.right
                    }

                    title: "Connection Settings"
                }
            }
        }
    }
}
