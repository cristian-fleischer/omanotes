import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

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
    // The open buffer has changes that are not on disk yet.
    property bool documentModified: false
    // Those changes belong to no file at all.
    property bool hasUntitledDraft: false
    // What to call the open note while its file is still untitled.md, so a new
    // note is labelled by what you typed before you get round to saving it.
    property string placeholderTitle: ""

    // Dragging a note onto a folder files it there. The row being dragged, the
    // row under the cursor, and where the label following the cursor sits.
    // -1 for the target means the empty space below the list, the vault root.
    property int dragRow: -1
    property int dropRow: -1
    property string dragTitle: ""
    property point dragPoint: Qt.point(0, 0)
    readonly property bool dragging: dragRow >= 0

    readonly property int minimumWidth: 180
    readonly property int maximumWidth: 420
    property int panelWidth: 260

    signal noteActivated(url fileUrl)
    signal draftActivated()
    // Empty means the vault root; a folder's path relative to it otherwise.
    signal newNoteRequested(string relativeDir)
    signal moveRequested(string path, string relativeDir)
    signal deleteRequested(string path, string title)
    signal discardRequested(string path, string title)
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
        if (sidebar.vaultModel.isHeaderAt(row))
            return;
        if (sidebar.vaultModel.isDirectoryAt(row)) {
            sidebar.vaultModel.toggleExpanded(row);
            // Expanding only adds rows below the folder, so its own index holds.
            list.currentIndex = row;
            return;
        }
        list.currentIndex = row;
        sidebar.noteActivated(sidebar.vaultModel.urlAt(row));
    }

    // Left closes the folder the selection is in, right opens the one it is on.
    // Both are no-ops while a filter is up, where the tree is flattened away.
    function expandSelection(expanded) {
        if (!sidebar.vaultModel || filterField.text.length > 0)
            return false;
        var row = list.currentIndex;
        if (row < 0)
            return false;
        if (!sidebar.vaultModel.isDirectoryAt(row)) {
            if (expanded)
                return false;
            row = sidebar.vaultModel.rowForParentOf(row);
            if (row < 0)
                return false;
            list.currentIndex = row;
            return true;
        }
        sidebar.vaultModel.setExpanded(row, expanded);
        list.currentIndex = row;
        return true;
    }

    // The row under the cursor, or -1 for the space past the last one.
    function rowUnderCursor(sceneX, sceneY) {
        if (!sidebar.vaultModel)
            return -1;
        var local = list.mapFromItem(null, sceneX, sceneY);
        if (local.y < 0 || local.y > list.height)
            return -2;
        return list.indexAt(list.width / 2, local.y + list.contentY);
    }

    // Where the note would land, or -2 for nowhere. Separate from the hit test
    // so the rules can be exercised without pixel coordinates.
    function setDropTarget(row) {
        sidebar.dropRow = row !== -2 && sidebar.vaultModel
                          && sidebar.vaultModel.canDropOnRow(sidebar.dragRow, row) ? row : -2;
        springLoad.restart();
        autoScroll.running = sidebar.dragging;
    }

    function updateDrag(sceneX, sceneY) {
        sidebar.dragPoint = sidebar.mapFromItem(null, sceneX, sceneY);
        setDropTarget(rowUnderCursor(sceneX, sceneY));
    }

    function finishDrag(dropped) {
        springLoad.stop();
        autoScroll.running = false;
        if (dropped && sidebar.dragRow >= 0 && sidebar.dropRow !== -2) {
            sidebar.moveRequested(sidebar.vaultModel.pathAt(sidebar.dragRow),
                                  sidebar.vaultModel.dropFolderForRow(sidebar.dropRow));
        }
        sidebar.dragRow = -1;
        sidebar.dropRow = -1;
        sidebar.dragTitle = "";
    }

    // Hold over a closed folder and it opens, so you can drop inside without
    // letting go first.
    Timer {
        id: springLoad
        interval: 650
        onTriggered: {
            if (!sidebar.dragging || sidebar.dropRow < 0 || !sidebar.vaultModel)
                return;
            if (sidebar.vaultModel.isDirectoryAt(sidebar.dropRow))
                sidebar.vaultModel.setExpanded(sidebar.dropRow, true);
        }
    }

    // Near the top or the bottom edge, keep the list moving.
    Timer {
        id: autoScroll
        interval: 16
        repeat: true
        onTriggered: {
            var margin = sidebar.scaledSize(28);
            var y = sidebar.dragPoint.y - list.y;
            var step = 0;
            if (y < margin)
                step = -Math.max(2, (margin - y) / 3);
            else if (y > list.height - margin)
                step = Math.max(2, (y - (list.height - margin)) / 3);
            if (step === 0)
                return;
            list.contentY = Math.max(0, Math.min(list.contentHeight - list.height,
                                                 list.contentY + step));
        }
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

            // A plain TextInput, not a TextField: Material floats its
            // placeholder into a label above the field, which has nowhere to go
            // in a strip this thin.
            TextInput {
                id: filterField
                objectName: "vaultFilter"
                anchors.fill: parent
                anchors.leftMargin: sidebar.scaledSize(10)
                anchors.rightMargin: sidebar.scaledSize(10)
                verticalAlignment: TextInput.AlignVCenter
                clip: true
                color: sidebar.textColor
                selectionColor: sidebar.accentColor
                selectedTextColor: sidebar.textColor
                font.pixelSize: sidebar.scaledSize(13)
                selectByMouse: true

                onTextChanged: {
                    if (sidebar.vaultModel)
                        sidebar.vaultModel.filter = text;
                }

                // Down and Up move the selection without leaving the field, so
                // filtering and choosing are one uninterrupted gesture.
                Keys.onDownPressed: sidebar.moveSelection(1)
                Keys.onUpPressed: sidebar.moveSelection(-1)
                Keys.onLeftPressed: function(event) {
                    event.accepted = sidebar.expandSelection(false);
                }
                Keys.onRightPressed: function(event) {
                    event.accepted = sidebar.expandSelection(true);
                }
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

            Label {
                anchors.left: filterField.left
                anchors.verticalCenter: filterField.verticalCenter
                text: "Filter notes"
                visible: filterField.text.length === 0
                color: sidebar.mutedColor
                font.pixelSize: sidebar.scaledSize(13)
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

            // A draft with no file behind it has no row in the vault, so it
            // gets one of its own at the top of the list.
            header: Item {
                width: list.width
                height: sidebar.hasUntitledDraft ? sidebar.scaledSize(30) : 0
                visible: sidebar.hasUntitledDraft

                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(sidebar.accentColor.r, sidebar.accentColor.g,
                                   sidebar.accentColor.b, sidebar.darkMode ? 0.30 : 0.20)
                }

                Text {
                    x: sidebar.scaledSize(10)
                    width: parent.width - x - sidebar.scaledSize(24)
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Untitled draft"
                    color: sidebar.textColor
                    elide: Text.ElideRight
                    font.pixelSize: sidebar.scaledSize(13)
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: sidebar.scaledSize(10)
                    anchors.verticalCenter: parent.verticalCenter
                    width: sidebar.scaledSize(6)
                    height: width
                    radius: width / 2
                    color: sidebar.accentColor
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidebar.draftActivated()
                }
            }
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            // Right-click on a row. What it offers depends on what the row is:
            // a folder can take a new note, a note can be moved or deleted.
            Menu {
                id: rowMenu
                property string targetPath: ""
                property string targetTitle: ""
                property string targetDir: ""
                property bool targetIsDirectory: false
                property bool targetHasDraft: false

                function openFor(row, item) {
                    if (!sidebar.vaultModel)
                        return;
                    rowMenu.targetPath = sidebar.vaultModel.pathAt(row);
                    rowMenu.targetDir = sidebar.vaultModel.relativeDirAt(row);
                    rowMenu.targetIsDirectory = sidebar.vaultModel.isDirectoryAt(row);
                    rowMenu.targetTitle = sidebar.vaultModel.titleAt(row);
                    rowMenu.targetHasDraft = sidebar.vaultModel.hasDraftAt(row);
                    rowMenu.popup(item);
                }

                MenuItem {
                    text: rowMenu.targetIsDirectory ? "New note in this folder" : "New note here"
                    onTriggered: sidebar.newNoteRequested(rowMenu.targetDir)
                }
                MenuSeparator { visible: !rowMenu.targetIsDirectory }
                MenuItem {
                    text: "Discard unsaved changes\u2026"
                    enabled: rowMenu.targetHasDraft
                    visible: rowMenu.targetHasDraft
                    onTriggered: sidebar.discardRequested(rowMenu.targetPath,
                                                          rowMenu.targetTitle)
                }
                Menu {
                    title: "Move to"
                    enabled: !rowMenu.targetIsDirectory
                    visible: !rowMenu.targetIsDirectory
                    MenuItem {
                        text: "Vault root"
                        onTriggered: sidebar.moveRequested(rowMenu.targetPath, "")
                    }
                    Repeater {
                        model: sidebar.vaultModel ? sidebar.vaultModel.folders() : []
                        MenuItem {
                            text: modelData
                            onTriggered: sidebar.moveRequested(rowMenu.targetPath, modelData)
                        }
                    }
                }
                MenuItem {
                    text: "Delete\u2026"
                    enabled: !rowMenu.targetIsDirectory
                    visible: !rowMenu.targetIsDirectory
                    onTriggered: sidebar.deleteRequested(rowMenu.targetPath, rowMenu.targetTitle)
                }
            }

            delegate: ItemDelegate {
                id: entry
                width: list.width
                // The parent path is only worth a second line while filtering,
                // where the tree is flattened and nesting no longer shows it.
                readonly property bool showsParent:
                    !isDirectory && relativeDir.length > 0
                    && sidebar.vaultModel !== null && sidebar.vaultModel.filter.length > 0
                readonly property real indent:
                    sidebar.scaledSize(10 + depth * 13)
                height: isHeader ? sidebar.scaledSize(26)
                                 : showsParent ? sidebar.scaledSize(44) : sidebar.scaledSize(30)
                padding: 0
                enabled: !isHeader
                onClicked: sidebar.activate(index)

                TapHandler {
                    enabled: !isHeader
                    acceptedButtons: Qt.RightButton
                    onSingleTapped: rowMenu.openFor(index, entry)
                }

                // Only a named note travels. target stays null so the row
                // itself never moves: the view recycles delegates as it
                // scrolls, and a label following the cursor is what is wanted
                // anyway. The threshold keeps an ordinary click a click.
                DragHandler {
                    id: rowDrag
                    enabled: !isHeader && !isDirectory && !isDraft
                    target: null
                    dragThreshold: sidebar.scaledSize(8)
                    // Without this the list takes the grab back and flicks
                    // itself the moment the drag passes the threshold.
                    grabPermissions: PointerHandler.CanTakeOverFromItems
                                     | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                                     | PointerHandler.ApprovesTakeOverByHandlersOfSameType
                    onActiveChanged: {
                        if (active) {
                            sidebar.dragRow = index;
                            sidebar.dragTitle = title;
                            sidebar.updateDrag(centroid.scenePosition.x,
                                               centroid.scenePosition.y);
                        } else {
                            sidebar.finishDrag(true);
                        }
                    }
                    onCentroidChanged: {
                        if (active)
                            sidebar.updateDrag(centroid.scenePosition.x,
                                               centroid.scenePosition.y);
                    }
                }

                background: Rectangle {
                    color: sidebar.dragging && sidebar.dropRow === index
                        ? Qt.rgba(sidebar.accentColor.r, sidebar.accentColor.g,
                                  sidebar.accentColor.b, 0.45)
                        : isHeader
                        ? "transparent"
                        : isCurrent
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

                contentItem: Item {
                    anchors.fill: parent

                    // A section label, not a row you can open.
                    Text {
                        visible: isHeader
                        x: sidebar.scaledSize(10)
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: sidebar.scaledSize(3)
                        text: title
                        color: sidebar.mutedColor
                        font.pixelSize: sidebar.scaledSize(10)
                    }

                    // Drawn rather than typed: iA Writer Mono S has no
                    // geometric-shape glyphs, and a fallback font puts a dot
                    // where the triangle should be.
                    Canvas {
                        id: disclosure
                        x: entry.indent
                        anchors.verticalCenter: parent.verticalCenter
                        visible: isDirectory
                        width: sidebar.scaledSize(9)
                        height: sidebar.scaledSize(9)

                        readonly property real dpr: Screen.devicePixelRatio
                        readonly property bool open: isDirectory && isExpanded
                        onOpenChanged: requestPaint()
                        onDprChanged: requestPaint()

                        onPaint: {
                            var context = getContext("2d");
                            context.reset();
                            context.fillStyle = sidebar.mutedColor;
                            var size = width;
                            context.beginPath();
                            if (open) {
                                context.moveTo(size * 0.1, size * 0.3);
                                context.lineTo(size * 0.9, size * 0.3);
                                context.lineTo(size * 0.5, size * 0.78);
                            } else {
                                context.moveTo(size * 0.3, size * 0.1);
                                context.lineTo(size * 0.78, size * 0.5);
                                context.lineTo(size * 0.3, size * 0.9);
                            }
                            context.closePath();
                            context.fill();
                        }
                    }

                    // A filled dot, the way an editor tab marks a dirty buffer.
                    Rectangle {
                        id: unsavedMark
                        anchors.right: parent.right
                        anchors.rightMargin: sidebar.scaledSize(10)
                        anchors.verticalCenter: parent.verticalCenter
                        width: sidebar.scaledSize(6)
                        height: width
                        radius: width / 2
                        color: sidebar.accentColor
                        visible: !isHeader && !isDraft
                                 && (hasDraft || (isCurrent && sidebar.documentModified))
                    }

                    Column {
                        visible: !isHeader
                        x: entry.indent + (isDirectory ? sidebar.scaledSize(13) : 0)
                        width: entry.width - x - sidebar.scaledSize(24)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            text: isCurrent && sidebar.placeholderTitle.length > 0
                                ? sidebar.placeholderTitle : title
                            color: isDirectory ? sidebar.mutedColor : sidebar.textColor
                            elide: Text.ElideRight
                            // A folder sits a step above the notes under it.
                            font.pixelSize: sidebar.scaledSize(isDirectory ? 14 : 13)
                        }

                        Text {
                            width: parent.width
                            text: relativeDir
                            visible: entry.showsParent
                            color: sidebar.mutedColor
                            elide: Text.ElideLeft
                            font.pixelSize: sidebar.scaledSize(10)
                        }
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
                        onClicked: sidebar.newNoteRequested("")
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
                opacity: 0.7

                FooterIconButton {
                    objectName: "newNoteButton"
                    iconName: "newnote"
                    iconColor: sidebar.mutedColor
                    tooltip: "New note (Ctrl+Alt+N)"
                    onClicked: sidebar.newNoteRequested("")
                }

                FooterIconButton {
                    objectName: "vaultRootButton"
                    iconName: "open"
                    iconColor: sidebar.mutedColor
                    tooltip: sidebar.vaultModel
                        ? "Vault folder: " + sidebar.vaultModel.root
                        : "Vault folder"
                    onClicked: sidebar.rootChangeRequested()
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

    // What the cursor is carrying, and where it would land. Late in the file so
    // it paints over the list.
    Rectangle {
        id: dragLabel
        visible: sidebar.dragging
        z: 10
        x: Math.max(sidebar.scaledSize(6),
                    Math.min(sidebar.width - width - sidebar.scaledSize(6),
                             sidebar.dragPoint.x + sidebar.scaledSize(12)))
        y: sidebar.dragPoint.y - height / 2
        width: Math.min(sidebar.width - sidebar.scaledSize(12),
                        dragLabelText.implicitWidth + sidebar.scaledSize(16))
        height: dragLabelText.implicitHeight + sidebar.scaledSize(10)
        radius: sidebar.scaledSize(4)
        color: Qt.tint(sidebar.pageColor,
                       sidebar.darkMode ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.10))
        border.width: 1
        border.color: sidebar.dropRow === -2 ? sidebar.mutedColor : sidebar.accentColor

        Text {
            id: dragLabelText
            anchors.centerIn: parent
            width: parent.width - sidebar.scaledSize(16)
            elide: Text.ElideMiddle
            color: sidebar.dropRow === -2 ? sidebar.mutedColor : sidebar.textColor
            font.pixelSize: sidebar.scaledSize(11)
            text: {
                if (sidebar.dropRow === -2 || !sidebar.vaultModel)
                    return sidebar.dragTitle;
                var folder = sidebar.vaultModel.dropFolderForRow(sidebar.dropRow);
                return sidebar.dragTitle + "  \u2192  "
                     + (folder.length > 0 ? folder : "vault root");
            }
        }
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
