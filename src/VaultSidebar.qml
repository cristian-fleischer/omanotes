import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The vault list: a filter field, every note under the root, and a footer that
// creates notes and changes the root. Opening always goes through
// Backend::open, which owns the unsaved-changes dialog and the file watcher.
Rectangle {
    id: sidebar

    property var vaultModel: null
    property real textScale: 1
    property bool darkMode: true
    property color pageColor: "#101010"
    property color textColor: "#eeeeee"
    property color mutedColor: "#909191"
    property color accentColor: "#5584aa"

    readonly property int minimumWidth: 180
    readonly property int maximumWidth: 420
    property int panelWidth: 260

    signal noteActivated(url fileUrl)
    signal newNoteRequested()
    signal rootChangeRequested()
    signal dismissed()

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * sidebar.textScale));
    }

    function focusFilter() {
        filterField.forceActiveFocus();
        filterField.selectAll();
    }

    function moveSelection(delta) {
        if (list.count === 0) {
            list.currentIndex = -1;
            return;
        }
        var next = list.currentIndex < 0 ? (delta > 0 ? 0 : list.count - 1)
                                         : list.currentIndex + delta;
        list.currentIndex = Math.max(0, Math.min(list.count - 1, next));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
    }

    function activate(row) {
        if (!sidebar.vaultModel || row < 0 || row >= list.count)
            return;
        list.currentIndex = row;
        sidebar.noteActivated(sidebar.vaultModel.urlAt(row));
    }

    function activateSelection() {
        activate(list.currentIndex >= 0 ? list.currentIndex : 0);
    }

    color: Qt.tint(sidebar.pageColor,
                   sidebar.darkMode ? Qt.rgba(1, 1, 1, 0.045) : Qt.rgba(0, 0, 0, 0.035))
    clip: true

    Connections {
        target: sidebar.vaultModel
        enabled: sidebar.vaultModel !== null

        // Opening a note from the portal dialog, or from a recovered draft,
        // still moves the selection here.
        function onCurrentPathChanged() {
            var row = sidebar.vaultModel.rowForPath(sidebar.vaultModel.currentPath);
            if (row >= 0) {
                list.currentIndex = row;
                list.positionViewAtIndex(row, ListView.Contain);
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.scaledSize(44)

            TextField {
                id: filterField
                objectName: "vaultFilter"
                anchors.fill: parent
                anchors.leftMargin: sidebar.scaledSize(8)
                anchors.rightMargin: sidebar.scaledSize(8)
                placeholderText: "Filter notes"
                color: sidebar.textColor
                placeholderTextColor: sidebar.mutedColor
                font.pixelSize: sidebar.scaledSize(13)
                selectByMouse: true
                background: Item {}

                onTextChanged: {
                    if (sidebar.vaultModel)
                        sidebar.vaultModel.filter = text;
                }

                // Down and Up move the selection without leaving the field, so
                // filtering and choosing are one uninterrupted gesture.
                Keys.onDownPressed: sidebar.moveSelection(1)
                Keys.onUpPressed: sidebar.moveSelection(-1)
                Keys.onReturnPressed: sidebar.activateSelection()
                Keys.onEnterPressed: sidebar.activateSelection()
                Keys.onEscapePressed: function(event) {
                    if (text.length > 0)
                        text = "";
                    else
                        sidebar.dismissed();
                    event.accepted = true;
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: sidebar.mutedColor
            opacity: 0.25
        }

        ListView {
            id: list
            objectName: "vaultList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            currentIndex: -1
            model: sidebar.vaultModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ItemDelegate {
                id: entry
                width: list.width
                height: relativeDir.length > 0 ? sidebar.scaledSize(46)
                                               : sidebar.scaledSize(32)
                padding: 0
                onClicked: sidebar.activate(index)

                background: Rectangle {
                    color: isCurrent
                        ? Qt.rgba(sidebar.accentColor.r, sidebar.accentColor.g,
                                  sidebar.accentColor.b, sidebar.darkMode ? 0.30 : 0.20)
                        : entry.ListView.isCurrentItem
                            ? Qt.rgba(sidebar.mutedColor.r, sidebar.mutedColor.g,
                                      sidebar.mutedColor.b, 0.22)
                            : entry.hovered
                                ? Qt.rgba(sidebar.mutedColor.r, sidebar.mutedColor.g,
                                          sidebar.mutedColor.b, 0.12)
                                : "transparent"
                }

                contentItem: Column {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: sidebar.scaledSize(12)
                    rightPadding: sidebar.scaledSize(10)
                    spacing: 1

                    Label {
                        width: entry.width - sidebar.scaledSize(22)
                        text: title
                        color: sidebar.textColor
                        elide: Text.ElideRight
                        font.pixelSize: sidebar.scaledSize(13)
                    }

                    Label {
                        width: entry.width - sidebar.scaledSize(22)
                        text: relativeDir
                        visible: relativeDir.length > 0
                        color: sidebar.mutedColor
                        elide: Text.ElideLeft
                        font.pixelSize: sidebar.scaledSize(10)
                    }
                }
            }

            // The root is empty, or the filter matched nothing. Either way, say
            // where we are looking and offer the one useful action.
            Column {
                anchors.centerIn: parent
                width: parent.width - sidebar.scaledSize(24)
                spacing: sidebar.scaledSize(10)
                visible: list.count === 0

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: sidebar.mutedColor
                    font.pixelSize: sidebar.scaledSize(11)
                    text: sidebar.vaultModel
                        ? (sidebar.vaultModel.filter.length > 0
                            ? "No note matches \"" + sidebar.vaultModel.filter + "\""
                            : "No Markdown files in\n" + sidebar.vaultModel.root)
                        : ""
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "New note"
                    color: emptyNoteHover.containsMouse ? sidebar.textColor : sidebar.accentColor
                    font.pixelSize: sidebar.scaledSize(12)

                    MouseArea {
                        id: emptyNoteHover
                        anchors.fill: parent
                        anchors.margins: -sidebar.scaledSize(6)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidebar.newNoteRequested()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: sidebar.mutedColor
            opacity: 0.25
        }

        // Material's Button is 40px tall before padding, which does not fit a
        // strip this thin. The footer uses the same understated text-and-hover
        // treatment as the editor's own footer instead.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: sidebar.scaledSize(30)

            Row {
                anchors.left: parent.left
                anchors.leftMargin: sidebar.scaledSize(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: sidebar.scaledSize(12)

                Text {
                    id: newNoteAction
                    objectName: "newNoteButton"
                    text: "New note"
                    color: newNoteHover.containsMouse ? sidebar.textColor : sidebar.mutedColor
                    font.pixelSize: sidebar.scaledSize(11)

                    MouseArea {
                        id: newNoteHover
                        anchors.fill: parent
                        anchors.margins: -sidebar.scaledSize(5)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidebar.newNoteRequested()
                    }
                }

                Text {
                    id: rootAction
                    objectName: "vaultRootButton"
                    text: "Folder"
                    color: rootHover.containsMouse ? sidebar.textColor : sidebar.mutedColor
                    font.pixelSize: sidebar.scaledSize(11)

                    ToolTip.visible: rootHover.containsMouse
                    ToolTip.text: sidebar.vaultModel ? sidebar.vaultModel.root : ""

                    MouseArea {
                        id: rootHover
                        anchors.fill: parent
                        anchors.margins: -sidebar.scaledSize(5)
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: sidebar.rootChangeRequested()
                    }
                }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: sidebar.scaledSize(10)
                anchors.verticalCenter: parent.verticalCenter
                color: sidebar.mutedColor
                font.pixelSize: sidebar.scaledSize(10)
                text: {
                    if (!sidebar.vaultModel)
                        return "";
                    return sidebar.vaultModel.count === sidebar.vaultModel.totalCount
                        ? sidebar.vaultModel.count + (sidebar.vaultModel.truncated ? "+" : "")
                        : sidebar.vaultModel.count + "/" + sidebar.vaultModel.totalCount;
                }
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: sidebar.mutedColor
        opacity: 0.3
    }

    MouseArea {
        id: resizer
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SplitHCursor
        acceptedButtons: Qt.LeftButton

        property real pressSceneX: 0
        property int pressWidth: 0

        onPressed: function(mouse) {
            pressSceneX = mapToItem(null, mouse.x, mouse.y).x;
            pressWidth = sidebar.width;
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return;
            var delta = mapToItem(null, mouse.x, mouse.y).x - pressSceneX;
            sidebar.panelWidth = Math.round(Math.max(sidebar.minimumWidth,
                Math.min(sidebar.maximumWidth, pressWidth + delta)));
        }
    }
}
