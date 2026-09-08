import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "EditorMutations.js" as EditorMutations

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: (backend.modified ? "* " : "") + backend.fileName + " - OmaNote"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color strongTextColor: backend.themeForeground
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property color selectionFill: backend.themeSelection
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) anchored so its 12px default leaves
    // the app at the sizes it was designed around.
    readonly property real textScale: backend.textScale
    readonly property int editorFontPixelSize:
        Math.max(8, Math.round(20 * win.textScale * win.editorZoom))
    // What the editor column actually gets, which is the window less the
    // sidebar and the Flickable's own 24px margins. Measuring the window
    // instead would push the text column off the left edge once a sidebar
    // takes a third of the width.
    readonly property int editorAreaWidth:
        Math.max(1, width - (sidebarVisible ? sidebarWidth : 0) - 48)
    readonly property int editorWidth: fullWidth ? editorAreaWidth : Math.min(
        Math.max(120,
                 Math.min(Math.round(writerFontMetrics.averageCharacterWidth * 65),
                          Math.max(360, editorAreaWidth
                                        - Math.round(writerFontMetrics.averageCharacterWidth * 20)))),
        editorAreaWidth)
    property bool closeConfirmed: false
    property bool searchOpen: false
    property bool searchUpdating: false
    property var searchMatches: []
    property int searchMatchIndex: -1
    property url pendingOpenUrl
    property string pendingAction: ""
    property bool replaceOpen: false
    property bool awaitingPendingSave: false
    property bool sidebarVisible: true
    property int sidebarWidth: 260
    // Ctrl+= and Ctrl+- scale the writing surface only. The chrome keeps
    // following the desktop's text size, which is a different knob.
    property real editorZoom: 1.0
    property bool fullWidth: false
    // The writing surface's family. The chrome stays on the bundled font, so
    // the footer and dialogs keep their proportions whatever you pick here.
    readonly property string bundledFontFamily: "iA Writer Mono S"
    property string editorFontFamily: bundledFontFamily
    // One entry per run of fenced-code lines: {y, height} in the editor's
    // coordinates. Recomputed after layout, never during it.
    property var codeSlabs: []

    // Component.onDestruction runs after the window is hidden, when visibility
    // reads Hidden and the size is whatever it last grew to. Both have to be
    // recorded while the window is still on screen.
    property int lastVisibility: Window.Windowed
    property rect lastWindowedGeometry: Qt.rect(x, y, width, height)

    onVisibilityChanged: {
        if (visibility === Window.Windowed || visibility === Window.Maximized
                || visibility === Window.FullScreen)
            win.lastVisibility = visibility;
    }

    onXChanged: recordWindowedGeometry()
    onYChanged: recordWindowedGeometry()
    onWidthChanged: recordWindowedGeometry()
    onHeightChanged: recordWindowedGeometry()

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    // An unsaved draft is kept, not argued about: closing the window writes it
    // to the recovery snapshot and the next window opens on it, with the file on
    // disk still untouched. Switching to another note still asks, because that
    // one buffer is about to be replaced by another.
    onClosing: function(close) {
        backend.persistDraft();
    }

    function requestOpen(url) {
        if (!backend.modified) {
            backend.open(url);
            return;
        }
        pendingOpenUrl = url;
        pendingAction = "open";
        unsavedChangesDialog.open();
    }

    function completePendingAction() {
        var action = pendingAction;
        pendingAction = "";
        if (action === "close") {
            closeConfirmed = true;
            close();
        } else if (action === "open") {
            backend.open(pendingOpenUrl);
        }
    }

    FontMetrics {
        id: writerFontMetrics
        font.family: win.editorFontFamily
        font.pixelSize: win.editorFontPixelSize
    }

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function openFromVault(fileUrl) {
        requestOpen(fileUrl);
        editor.forceActiveFocus();
    }

    function createNote() {
        var path = vault.createNote();
        if (path === "") {
            return;
        }
        win.requestOpen(vault.urlForPath(path));
    }

    function toggleSidebar() {
        win.sidebarVisible = !win.sidebarVisible;
        if (win.sidebarVisible)
            sidebar.focusFilter();
        else
            editor.forceActiveFocus();
    }

    function saveView() {
        backend.saveViewState(win.editorZoom, win.fullWidth,
                              win.editorFontFamily === win.bundledFontFamily
                                  ? "" : win.editorFontFamily);
    }

    function setZoom(zoom) {
        win.editorZoom = Math.max(0.6, Math.min(2.5, Math.round(zoom * 100) / 100));
        saveView();
    }

    function toggleFullWidth() {
        win.fullWidth = !win.fullWidth;
        saveView();
    }

    function setEditorFont(family) {
        var resolved = backend.resolveFontFamily(family);
        win.editorFontFamily = resolved.length > 0 ? resolved : win.bundledFontFamily;
        saveView();
    }

    // A maximised or full-screen window reports the screen's size, which is not
    // the size to come back to.
    // Qt Quick's text node paints character backgrounds only, so a fence drawn
    // that way comes out ragged where the lines are short and striped through
    // the leading. Measure each fenced run and paint one rectangle behind it.
    function updateCodeSlabs() {
        var regions = backend.fencedCodeRegions();
        var slabs = [];
        for (var i = 0; i < regions.length; ++i) {
            var top = editor.positionToRectangle(regions[i].start);
            var bottom = editor.positionToRectangle(regions[i].end);
            slabs.push({ "y": top.y, "height": bottom.y + bottom.height - top.y });
        }
        win.codeSlabs = slabs;
    }

    function scheduleCodeSlabs() {
        // positionToRectangle is only meaningful once the text has been laid
        // out, and the change that triggered this has not been laid out yet.
        Qt.callLater(win.updateCodeSlabs);
    }

    onEditorFontPixelSizeChanged: scheduleCodeSlabs()
    onEditorFontFamilyChanged: scheduleCodeSlabs()

    function recordWindowedGeometry() {
        if (win.visibility === Window.Windowed)
            win.lastWindowedGeometry = Qt.rect(win.x, win.y, win.width, win.height);
    }

    function saveWindowState() {
        backend.saveWindowGeometry(win.lastWindowedGeometry.x, win.lastWindowedGeometry.y,
                                   win.lastWindowedGeometry.width,
                                   win.lastWindowedGeometry.height,
                                   win.lastVisibility === Window.Maximized,
                                   win.lastVisibility === Window.FullScreen);
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    function updateSearch() {
        var matches = [];
        var query = searchField.text;
        if (query.length > 0) {
            var haystack = editor.text.toLocaleLowerCase();
            var needle = query.toLocaleLowerCase();
            var position = 0;
            while ((position = haystack.indexOf(needle, position)) !== -1) {
                matches.push(position);
                position += Math.max(1, needle.length);
            }
        }
        searchMatches = matches;
        searchMatchIndex = matches.length > 0 ? 0 : -1;
        showSearchMatch();
    }

    function showSearchMatch() {
        var start = searchMatchIndex >= 0 ? searchMatches[searchMatchIndex] : -1;
        searchUpdating = true;
        backend.setSearchHighlight(searchField.text, start);
        if (start >= 0) {
            editor.select(start, start + searchField.text.length);
            editorFlick.ensureCursorVisible();
        }
        searchUpdating = false;
    }

    function moveSearch(direction) {
        if (searchMatches.length === 0)
            return;
        searchMatchIndex = (searchMatchIndex + direction + searchMatches.length)
                           % searchMatches.length;
        showSearchMatch();
    }

    function closeSearch() {
        searchOpen = false;
        searchUpdating = true;
        backend.setSearchHighlight("", -1);
        editor.deselect();
        searchUpdating = false;
        replaceOpen = false;
        editor.forceActiveFocus();
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.save()
    }

    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            replaceOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++"]
        context: Qt.ApplicationShortcut
        onActivated: win.setZoom(win.editorZoom + 0.1)
    }

    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        onActivated: win.setZoom(win.editorZoom - 0.1)
    }

    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        onActivated: win.setZoom(1.0)
    }

    Shortcut {
        sequence: "Ctrl+M"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleFullWidth()
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        context: Qt.ApplicationShortcut
        onActivated: {
            fontDialog.selectedFont = editor.font;
            fontDialog.open();
        }
    }

    Shortcut {
        sequence: "Ctrl+L"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleSidebar()
    }

    Shortcut {
        sequence: "Ctrl+Shift+L"
        context: Qt.ApplicationShortcut
        onActivated: {
            win.sidebarVisible = true;
            sidebar.focusFilter();
        }
    }

    Shortcut {
        sequence: "Ctrl+Alt+N"
        context: Qt.ApplicationShortcut
        onActivated: win.createNote()
    }

    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("**", "**")
    }

    Shortcut {
        sequence: "Ctrl+I"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("*", "*")
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        onActivated: editor.insertLink()
    }

    Shortcut {
        sequence: "Ctrl+?"
        context: Qt.ApplicationShortcut
        onActivated: shortcutsDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        onActivated: backend.openDialog()
    }

    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        onActivated: backend.newWindow()
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.saveAsDialog()
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        onActivated: backend.printDocument()
    }

    Shortcut {
        sequences: ["Meta+F", "F11"]
        context: Qt.ApplicationShortcut
        onActivated: toggleFullScreen()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        onActivated: editor.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        onActivated: editor.redo()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.ApplicationShortcut
        enabled: win.searchOpen
        onActivated: win.moveSearch(1)
    }

    Connections {
        target: backend

        // Whatever route a file arrived by, the sidebar highlights it.
        function onFileUrlChanged() {
            vault.setCurrentUrl(backend.fileUrl);
        }

        function onOpenDialogRequested() {
            openFileDialog.open();
        }

        function onSaveDialogRequested(suggestedUrl) {
            saveFileDialog.selectedFile = suggestedUrl;
            saveFileDialog.open();
        }

        function onCloseAfterSave() {
            win.closeConfirmed = true;
            win.close();
        }

        function onSaveSucceeded() {
            win.awaitingPendingSave = false;
            if (win.pendingAction !== "")
                win.completePendingAction();
        }

        function onExternalChangeDetected(deleted, locallyModified) {
            externalChangeDialog.deleted = deleted;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: win.requestOpen(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: backend.saveAs(selectedFile)
        onRejected: {
            backend.fileDialogCanceled();
            win.awaitingPendingSave = false;
            win.pendingAction = "";
        }
    }

    Dialogs.FontDialog {
        id: fontDialog
        objectName: "fontDialog"
        title: "Editor font"
        // Only the family carries over. Size follows the zoom and the desktop
        // text scale, and weight and style are what the Markdown means.
        onAccepted: win.setEditorFont(selectedFont.family)
    }

    Dialogs.FolderDialog {
        id: vaultRootDialog
        title: "Choose the vault folder"
        onAccepted: vault.setRootUrl(selectedFolder)
    }

    UnsavedChangesDialog {
        id: unsavedChangesDialog
        fileName: backend.fileName
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        activeButtonColor: backend.themeAccent
        containerWidth: win.width
        containerHeight: win.height

        onDiscardRequested: {
            backend.discardRecovery();
            win.completePendingAction();
        }

        onSaveRequested: {
            win.awaitingPendingSave = true;
            backend.save();
        }
        onCancelRequested: win.pendingAction = ""
    }

    ExternalChangeDialog {
        id: externalChangeDialog
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        containerWidth: win.width
        containerHeight: win.height

        onKeepRequested: backend.keepExternalVersion()
        onReloadRequested: backend.reloadFromDisk()
    }

    Dialog {
        id: shortcutsDialog
        modal: true

        // Material's default dim washes the page towards light grey, which on a
        // dark palette reads as the dialog turning the lights on. Dim towards
        // black in both themes, the way a modal is supposed to.
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.55)
        }
        title: "Keyboard shortcuts"
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        contentItem: Column {
            spacing: 12

            Label {
                text: "Ctrl+S  Save\nCtrl+Shift+S  Save As\nCtrl+O  Open\nCtrl+N  New Window\nCtrl+Alt+N  New Note in the Vault\nCtrl+L  Toggle Sidebar\nCtrl+Shift+L  Focus the Note Filter\nCtrl+= / Ctrl+-  Zoom the Text\nCtrl+0  Reset the Zoom\nCtrl+M  Full Window Width\nCtrl+Shift+F  Editor Font\nCtrl+F  Find\nCtrl+H  Find and Replace\nCtrl+B  Bold\nCtrl+I  Italic\nCtrl+K  Link\nCtrl+P  Print\nF11 / Super+F  Fullscreen\nCtrl+?  Shortcuts"
                lineHeight: 1.5
            }

            // MIT requires the copyright notice to travel with the fork.
            Label {
                objectName: "attribution"
                text: "OmaNote. Fork of OmaWrite by David Heinemeier Hansson, MIT."
                color: win.mutedColor
                font.pixelSize: win.scaledSize(11)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        VaultSidebar {
            id: sidebar
            objectName: "vaultSidebar"
            vaultModel: vault
            visible: win.sidebarVisible
            Layout.fillHeight: true
            Layout.preferredWidth: panelWidth
            Layout.minimumWidth: minimumWidth
            Layout.maximumWidth: maximumWidth
            textScale: win.textScale
            darkMode: win.darkMode
            pageColor: win.pageColor
            textColor: win.textColor
            mutedColor: win.mutedColor
            accentColor: backend.themeAccent
            documentModified: backend.modified
            hasUntitledDraft: backend.modified && backend.untitled

            onNoteActivated: function(fileUrl) { win.openFromVault(fileUrl); }
            onDraftActivated: editor.forceActiveFocus()
            onNewNoteRequested: win.createNote()
            onRootChangeRequested: vaultRootDialog.open()
            onDismissed: {
                win.sidebarVisible = false;
                editor.forceActiveFocus();
            }
            onPanelWidthChanged: win.sidebarWidth = panelWidth
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: editorFlick
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                clip: true
                contentWidth: width
                contentHeight: Math.max(height, editor.y + editor.implicitHeight + 220)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    // Wheel scrolling moves contentY directly rather than
                    // flicking the Flickable, so the bar has to be told about
                    // that activity; linger briefly after the last event.
                    active: hovered || pressed || wheelScroll.running || scrollLinger.running
                    // Stop above the footer strip so the bar doesn't overlap
                    // the word count in the bottom-right corner. Padding and
                    // inset, not anchors: the attached-ScrollBar layout overrides
                    // anchors. Padding stops the thumb, the inset the track.
                    bottomPadding: win.scaledSize(32)
                    bottomInset: win.scaledSize(32)
                }

                Timer {
                    id: scrollLinger
                    interval: 600
                }

                // Flickable turns a wheel notch into a flick sized by the small
                // application font, which crawls next to a browser. Reproduce
                // Chromium's wheel physics instead (cc::ScrollOffsetAnimationCurve):
                // each notch moves 3 lines of 40px towards a running target, the
                // animation gets shorter as the outstanding distance grows, and a
                // notch landing mid-animation carries the current velocity into
                // the new curve, so sustained spinning keeps picking up speed.
                readonly property real wheelStep: win.scaledSize(120)

                FrameAnimation {
                    id: wheelScroll
                    running: false

                    property real startY: 0
                    property real targetY: 0
                    property real duration: 0.2
                    // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a
                    // fresh scroll, with y1 tilted on retarget so the curve's
                    // initial slope matches the velocity it inherits.
                    property real cx1: 0.42
                    property real cy1: 0
                    readonly property real cx2: 0.58
                    readonly property real cy2: 1

                    onTriggered: {
                        var x = elapsedTime / duration;
                        if (x >= 1) {
                            editorFlick.contentY = editorFlick.snapToPixel(targetY);
                            stop();
                            return;
                        }
                        editorFlick.contentY = editorFlick.snapToPixel(
                            startY + (targetY - startY) * curveY(solveCurve(x)));
                    }

                    function begin(from, to, dur, slope) {
                        startY = from;
                        targetY = to;
                        duration = dur;
                        cx1 = 0.42;
                        cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                        restart();
                    }

                    function retarget(newTarget) {
                        var s = solveCurve(Math.min(1, elapsedTime / duration));
                        var pos = startY + (targetY - startY) * curveY(s);
                        var delta = newTarget - pos;
                        if (Math.abs(delta) < 0.5) {
                            editorFlick.contentY = newTarget;
                            stop();
                            return;
                        }

                        var velocity = curveDY(s) / Math.max(1e-6, curveDX(s))
                            * (targetY - startY) / duration;
                        var dur = editorFlick.wheelDuration(delta);
                        // When already moving faster than the eased curve would,
                        // bound the duration by the time to target at the current
                        // velocity; the 2.5x covers the ease-out tail.
                        if (velocity !== 0 && delta / velocity > 0)
                            dur = Math.min(dur, delta / velocity * 2.5);
                        begin(pos, newTarget, dur, velocity * dur / delta);
                    }

                    // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
                    // evaluated by Newton-solving the curve parameter from x.
                    function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
                    function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
                    function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
                    function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

                    function solveCurve(x) {
                        var s = x;
                        for (var i = 0; i < 8; ++i) {
                            var error = curveX(s) - x;
                            if (Math.abs(error) < 0.001)
                                break;
                            var d = curveDX(s);
                            if (Math.abs(d) < 1e-6)
                                break;
                            s = Math.max(0, Math.min(1, s - error / d));
                        }
                        return s;
                    }
                }

                WheelHandler {
                    // Wayland compositors route every pointer's scroll through
                    // one seat device that Qt classifies as a touchpad, so the
                    // device type cannot tell a mouse wheel from two-finger
                    // scrolling. Distinguish by event shape instead: discrete
                    // wheel notches arrive with only angleDelta set, while
                    // finger scrolling carries pixel-precise pixelDelta.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(wheel) {
                        if (wheel.modifiers & Qt.ControlModifier) {
                            if (wheel.angleDelta.y !== 0)
                                win.setZoom(win.editorZoom + (wheel.angleDelta.y > 0 ? 0.1 : -0.1));
                            wheel.accepted = true;
                            return;
                        }
                        scrollLinger.restart();
                        if (wheel.pixelDelta.y !== 0)
                            editorFlick.scrollTo(editorFlick.clampContentY(editorFlick.contentY - wheel.pixelDelta.y));
                        else
                            editorFlick.scrollByWheel(wheel);
                        wheel.accepted = true;
                    }
                }

                onMovementStarted: wheelScroll.stop()

                function scrollByWheel(wheel) {
                    // High-resolution wheels report fractional notches; feed
                    // those through the same animated path, like Chromium does
                    // for every wheel-source event.
                    var notches = wheel.angleDelta.y / 120;
                    if (notches === 0)
                        return;

                    if (wheelScroll.running) {
                        wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                        return;
                    }

                    var target = clampContentY(contentY - notches * wheelStep);
                    if (target !== contentY)
                        wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
                }

                // Chromium's inverse-delta duration: 200ms for a single notch,
                // ramping down to 100ms once 480px are outstanding.
                function wheelDuration(delta) {
                    var pixels = Math.abs(delta) / win.textScale;
                    return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
                }

                function clampContentY(y) {
                    return Math.max(0, Math.min(Math.max(0, contentHeight - height), y));
                }

                // Whole device pixels keep natively hinted glyphs from
                // re-rasterizing mid-animation, which reads as shimmer.
                function snapToPixel(y) {
                    return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
                }

                // Jump to a position, abandoning any wheel animation still running.
                function scrollTo(y) {
                    wheelScroll.stop();
                    contentY = snapToPixel(y);
                }

                // Keep the editing caret within the viewport so writing past the
                // bottom edge scrolls the page along with the text.
                function ensureCursorVisible() {
                    var margin = win.editorFontPixelSize * 2;
                    var cursorTop = editor.y + editor.cursorRectangle.y;
                    var cursorBottom = cursorTop + editor.cursorRectangle.height;
                    var maxContentY = Math.max(0, contentHeight - height);

                    if (cursorBottom + margin > contentY + height)
                        scrollTo(Math.min(maxContentY, cursorBottom + margin - height));
                    else if (cursorTop - margin < contentY)
                        scrollTo(Math.max(0, cursorTop - margin));
                }

                Item {
                    id: codeSlabLayer
                    x: editor.x
                    y: editor.y
                    width: editor.width
                    height: editor.height

                    Repeater {
                        model: win.codeSlabs
                        Rectangle {
                            x: -win.scaledSize(12)
                            width: codeSlabLayer.width + win.scaledSize(24)
                            y: modelData.y
                            height: modelData.height
                            radius: win.scaledSize(4)
                            color: backend.themeCodeBackground
                        }
                    }
                }

                TextEdit {
                    id: editor
                    objectName: "sourceEditor"
                    x: Math.round((editorFlick.width - width) / 2)
                    y: Math.max(42, Math.round(win.height * 0.05))
                    width: win.editorWidth
                    height: Math.max(editorFlick.height - y - 96, implicitHeight + 20)
                    text: ""
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    persistentSelection: true
                    activeFocusOnPress: true
                    color: win.textColor
                    selectedTextColor: win.strongTextColor
                    selectionColor: win.selectionFill
                    font.family: win.editorFontFamily
                    font.pixelSize: win.editorFontPixelSize
                    font.weight: Font.Normal
                    // Native rendering hints glyphs to the pixel grid, which is
                    // crispest at whole scale factors but misplaces and unevenly
                    // rasterizes glyphs at fractional ones (and goes stale when
                    // the compositor delivers the fractional scale after the
                    // first frame). Fall back to Qt's scalable renderer there.
                    renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
                    cursorDelegate: Rectangle {
                        width: 1
                        color: win.strongTextColor
                    }
                    onCursorRectangleChanged: editorFlick.ensureCursorVisible()

                    function replaceSelectionWith(replacement) {
                        var start = Math.min(selectionStart, selectionEnd);
                        var end = Math.max(selectionStart, selectionEnd);
                        EditorMutations.replaceRange(editor, start, end, replacement);
                    }

                    function wrapSelection(before, after) {
                        forceActiveFocus();
                        var start = Math.min(selectionStart, selectionEnd);
                        var end = Math.max(selectionStart, selectionEnd);
                        var selected = text.slice(start, end);
                        EditorMutations.replaceRange(editor, start, end,
                                                     before + selected + after,
                                                     before.length,
                                                     before.length + selected.length);
                    }

                    function insertLink() {
                        var start = Math.min(selectionStart, selectionEnd);
                        var end = Math.max(selectionStart, selectionEnd);
                        var selected = text.slice(start, end);
                        var url = backend.clipboardUrl();
                        var label = selected.length > 0 ? selected : "link text";
                        var destination = url.length > 0 ? url : "https://";
                        var escapedLabel = escapeMarkdownLinkText(label);
                        var markdown = "[" + escapedLabel + "](" + escapeMarkdownLinkDestination(destination) + ")";
                        if (selected.length === 0) {
                            EditorMutations.replaceRange(editor, start, end, markdown,
                                                         1, 1 + escapedLabel.length);
                        } else if (url.length === 0) {
                            EditorMutations.replaceRange(editor, start, end, markdown,
                                                         escapedLabel.length + 3,
                                                         markdown.length - 1);
                        } else {
                            EditorMutations.replaceRange(editor, start, end, markdown);
                        }
                    }

                    function smartReturn(softBreak) {
                        if (softBreak) {
                            replaceSelectionWith("\n");
                            return;
                        }
                        var lineStart = text.lastIndexOf("\n", cursorPosition - 1) + 1;
                        var line = text.slice(lineStart, cursorPosition);
                        var before = text.slice(0, cursorPosition);
                        var fences = (before.match(/^\s*```/gm) || []).length;
                        if ((fences % 2) === 1) {
                            replaceSelectionWith("\n");
                            return;
                        }
                        var match = line.match(/^(\s*)([-+*]|\d+[.)]|>+)\s+(.*)$/);
                        if (match) {
                            if (match[3].length === 0) {
                                EditorMutations.replaceRange(editor, lineStart,
                                                             cursorPosition, "\n");
                            } else {
                                var marker = match[2];
                                if (/^\d/.test(marker))
                                    marker = (parseInt(marker) + 1) + marker.slice(-1);
                                replaceSelectionWith("\n" + match[1] + marker + " ");
                            }
                            return;
                        }
                        replaceSelectionWith("\n\n");
                    }

                    function escapeMarkdownLinkText(linkText) {
                        return linkText.replace(/\\/g, "\\\\")
                                       .replace(/\[/g, "\\[")
                                       .replace(/\]/g, "\\]");
                    }

                    function escapeMarkdownLinkDestination(linkUrl) {
                        return linkUrl.replace(/\\/g, "\\\\")
                                      .replace(/\(/g, "\\(")
                                      .replace(/\)/g, "\\)");
                    }

                    function pasteClipboardUrlAsMarkdownLink() {
                        var start = Math.min(selectionStart, selectionEnd);
                        var end = Math.max(selectionStart, selectionEnd);
                        if (start === end)
                            return false;

                        var url = backend.clipboardUrl();
                        if (url === "")
                            return false;

                        var selected = text.slice(start, end);
                        var leading = selected.match(/^\s*/)[0];
                        var trailing = selected.match(/\s*$/)[0];
                        var linkText = selected.slice(leading.length,
                                                      selected.length - trailing.length);
                        if (linkText === "")
                            return false;

                        replaceSelectionWith(leading + "[" + escapeMarkdownLinkText(linkText) + "]("
                                             + escapeMarkdownLinkDestination(url) + ")" + trailing);
                        return true;
                    }

                    function pasteClipboardAsPlainText() {
                        var pastedText = backend.clipboardText();
                        if (pastedText.length > 0)
                            replaceSelectionWith(pastedText);
                    }

                    function skipHiddenForward(position) {
                        var pos = position;
                        var ranges = backend.hiddenRangesAt(pos);
                        for (var i = 0; i < ranges.length; i++) {
                            if (pos >= ranges[i].start && pos < ranges[i].end) {
                                pos = ranges[i].end;
                                i = -1;
                            }
                        }
                        return pos;
                    }

                    function skipHiddenBackward(position) {
                        var pos = position;
                        var ranges = backend.hiddenRangesAt(pos);
                        for (var i = ranges.length - 1; i >= 0; i--) {
                            if (pos > ranges[i].start && pos <= ranges[i].end) {
                                pos = ranges[i].start;
                                i = ranges.length;
                            }
                        }
                        return pos;
                    }

                    function moveCursorVisibly(direction) {
                        if (selectionStart !== selectionEnd) {
                            cursorPosition = direction > 0
                                ? Math.max(selectionStart, selectionEnd)
                                : Math.min(selectionStart, selectionEnd);
                            return;
                        }

                        var pos = Math.max(0, Math.min(text.length, cursorPosition + direction));
                        cursorPosition = direction > 0
                            ? skipHiddenForward(pos)
                            : skipHiddenBackward(pos);
                    }

                    function movePage(direction, extendSelection) {
                        var pageStep = Math.max(win.editorFontPixelSize,
                                                editorFlick.height - win.editorFontPixelSize * 2);
                        var rect = cursorRectangle;
                        var targetY = rect.y + rect.height / 2 + direction * pageStep;
                        var target = positionAt(rect.x, Math.max(0, targetY));
                        if (extendSelection)
                            moveCursorSelection(target, TextEdit.SelectCharacters);
                        else
                            cursorPosition = target;
                    }

                    function deleteParagraphBreakBehindCursor() {
                        if (selectionStart !== selectionEnd || cursorPosition < 2)
                            return false;

                        if (text.slice(cursorPosition - 2, cursorPosition) !== "\n\n")
                            return false;

                        var start = cursorPosition - 2;
                        remove(start, cursorPosition);
                        cursorPosition = start;
                        return true;
                    }

                    Keys.priority: Keys.BeforeItem
                    Keys.onPressed: function(event) {
                        var pasteKey = (event.key === Qt.Key_V)
                            && (event.modifiers & Qt.ControlModifier)
                            && !(event.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier));
                        var shiftInsert = (event.key === Qt.Key_Insert)
                            && (event.modifiers & Qt.ShiftModifier)
                            && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
                        if (pasteKey || shiftInsert) {
                            if (!pasteClipboardUrlAsMarkdownLink())
                                pasteClipboardAsPlainText();
                            event.accepted = true;
                            return;
                        }

                        var returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                        var commandModifier = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
                        if (returnKey && !commandModifier) {
                            smartReturn(event.modifiers & Qt.ShiftModifier);
                            event.accepted = true;
                        } else if (!commandModifier && event.key === Qt.Key_Backspace
                                   && deleteParagraphBreakBehindCursor()) {
                            event.accepted = true;
                        } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                                   && event.key === Qt.Key_Right) {
                            moveCursorVisibly(1);
                            event.accepted = true;
                        } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                                   && event.key === Qt.Key_Left) {
                            moveCursorVisibly(-1);
                            event.accepted = true;
                        } else if (!commandModifier
                                   && (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)) {
                            movePage(event.key === Qt.Key_PageDown ? 1 : -1,
                                     event.modifiers & Qt.ShiftModifier);
                            event.accepted = true;
                        }
                    }

                    onWidthChanged: win.scheduleCodeSlabs()
                    onImplicitHeightChanged: win.scheduleCodeSlabs()

                    onTextChanged: {
                        win.scheduleCodeSlabs();
                        if (win.searchUpdating)
                            return;
                        var contentChanged = backend.editorTextChanged();
                        if (win.searchOpen && contentChanged)
                            win.updateSearch();
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: "# Start writing"
                        visible: editor.text.length === 0 && !editor.activeFocus
                        color: win.mutedColor
                        font.family: editor.font.family
                        font.pixelSize: editor.font.pixelSize
                        font.weight: editor.font.weight
                    }

                    Component.onCompleted: {
                        backend.attachDocument(textDocument);
                        forceActiveFocus();
                    }
                }
            }

            Row {
                id: footerStatus
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.bottomMargin: 10
                spacing: 12
                opacity: 0.55

                FooterIconButton {
                    objectName: "sidebarButton"
                    iconName: "sidebar"
                    iconColor: win.sidebarVisible ? win.textColor : win.mutedColor
                    tooltip: "Notes sidebar (Ctrl+L)"
                    onClicked: win.toggleSidebar()
                }

                FooterIconButton {
                    objectName: "saveButton"
                    iconName: "save"
                    iconColor: win.mutedColor
                    tooltip: "Save"
                    onClicked: backend.save()
                }

                FooterIconButton {
                    objectName: "openButton"
                    iconName: "open"
                    iconColor: win.mutedColor
                    tooltip: "Open"
                    onClicked: backend.openDialog()
                }

                Label {
                    text: backend.status
                    color: win.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(11)
                    visible: text !== ""
                    elide: Text.ElideRight
                    width: Math.min(360, win.width / 3)
                    height: win.scaledSize(16)
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 12
                anchors.bottomMargin: 10
                spacing: 12
                height: win.scaledSize(16)

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: backend.wordCount + (backend.wordCount === 1 ? " Word" : " Words")
                    color: win.mutedColor
                    opacity: 0.75
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(11)
                }

                FooterIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    objectName: "widthButton"
                    iconName: "width"
                    iconColor: win.fullWidth ? win.textColor : win.mutedColor
                    opacity: 0.55
                    tooltip: "Full window width (Ctrl+M)"
                    onClicked: win.toggleFullWidth()
                }
            }


            Pane {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 12
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                height: win.scaledSize(win.replaceOpen ? 104 : 56)
                visible: win.searchOpen
                z: 10
                leftPadding: 16
                rightPadding: 8
                topPadding: 0
                bottomPadding: 0
                Material.elevation: 8

                background: Rectangle {
                    radius: 9
                    color: win.darkMode ? "#22221f" : "#fffef2"
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        TextInput {
                            id: searchField
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: win.replaceOpen ? parent.height / 2 : parent.height
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true
                            color: win.textColor
                            selectionColor: win.selectionFill
                            selectedTextColor: win.strongTextColor
                            font.pixelSize: win.scaledSize(17)
                            clip: true
                            onTextChanged: win.updateSearch()
                            Keys.onReturnPressed: function(event) {
                                win.moveSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                                event.accepted = true;
                            }
                            Keys.onEscapePressed: function(event) {
                                win.closeSearch();
                                event.accepted = true;
                            }
                        }

                        TextInput {
                            id: replaceField
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: parent.height / 2
                            visible: win.replaceOpen
                            verticalAlignment: TextInput.AlignVCenter
                            color: win.textColor
                            selectionColor: win.selectionFill
                            selectedTextColor: win.strongTextColor
                            font.pixelSize: win.scaledSize(17)
                            Keys.onReturnPressed: replaceCurrentButton.clicked()
                        }

                        Label {
                            anchors.verticalCenter: replaceField.verticalCenter
                            text: "Replace with"
                            visible: win.replaceOpen && replaceField.text.length === 0
                            color: win.mutedColor
                            font.pixelSize: win.scaledSize(17)
                        }

                        Label {
                            anchors.verticalCenter: searchField.verticalCenter
                            text: "Find"
                            visible: searchField.text.length === 0
                            color: win.mutedColor
                            font.pixelSize: win.scaledSize(17)
                        }
                    }

                    Label {
                        Layout.preferredWidth: win.scaledSize(58)
                        Layout.fillHeight: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: win.searchMatches.length === 0
                            ? "0/0"
                            : (win.searchMatchIndex + 1) + "/" + win.searchMatches.length
                        color: win.darkMode ? win.textColor : "#62635f"
                        font.pixelSize: win.scaledSize(16)
                    }

                    Button {
                        id: replaceCurrentButton
                        visible: win.replaceOpen
                        text: "Replace"
                        onClicked: {
                            if (win.searchMatchIndex < 0) return;
                            var start = win.searchMatches[win.searchMatchIndex];
                            EditorMutations.replaceRange(editor, start,
                                                         start + searchField.text.length,
                                                         replaceField.text);
                            win.updateSearch();
                        }
                    }

                    Button {
                        visible: win.replaceOpen
                        text: "All"
                        onClicked: {
                            if (searchField.text.length === 0) return;
                            for (var i = win.searchMatches.length - 1; i >= 0; --i) {
                                var start = win.searchMatches[i];
                                EditorMutations.replaceRange(editor, start,
                                                             start + searchField.text.length,
                                                             replaceField.text);
                            }
                            win.updateSearch();
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 34
                        color: win.darkMode ? "#6f6f62" : "#d5d56e"
                    }

                    SearchIconButton {
                        iconName: "up"
                        iconColor: win.darkMode ? win.textColor : "#62635f"
                        onClicked: win.moveSearch(-1)
                    }

                    SearchIconButton {
                        iconName: "down"
                        iconColor: win.darkMode ? win.textColor : "#62635f"
                        onClicked: win.moveSearch(1)
                    }

                    SearchIconButton {
                        iconName: "close"
                        iconColor: win.darkMode ? win.textColor : "#62635f"
                        onClicked: win.closeSearch()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        if (geometry.x >= 0) x = geometry.x;
        if (geometry.y >= 0) y = geometry.y;
        width = geometry.width;
        height = geometry.height;
        if (geometry.fullScreen)
            win.visibility = Window.FullScreen;
        else if (geometry.maximized)
            showMaximized();

        vault.setCurrentUrl(backend.fileUrl);

        var view = backend.viewState();
        win.editorZoom = view.zoom;
        win.fullWidth = view.fullWidth;
        if (view.fontFamily.length > 0)
            win.editorFontFamily = view.fontFamily;

        var sidebarState = backend.sidebarState();
        win.sidebarWidth = sidebarState.width;
        sidebar.panelWidth = sidebarState.width;
        win.sidebarVisible = sidebarState.visible;
    }

    Component.onDestruction: {
        saveWindowState();
        backend.saveSidebarState(win.sidebarVisible, win.sidebarWidth);
    }

}
