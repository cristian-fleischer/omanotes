import QtQuick
import QtQuick.Dialogs as Dialogs

// The desktop's own choosers. In a file of their own so that the module they
// come from is loaded with the first of them, not with the window.
Item {
    id: root

    signal openAccepted(url file)
    signal saveAccepted(url file)
    signal saveRejected()
    signal fontAccepted(string family)
    signal folderAccepted(url folder)

    function openFile() {
        openFileDialog.open();
    }

    function saveFile(suggestedUrl) {
        saveFileDialog.selectedFile = suggestedUrl;
        saveFileDialog.open();
    }

    function chooseFont(font) {
        fontDialog.selectedFont = font;
        fontDialog.open();
    }

    function chooseFolder() {
        vaultRootDialog.open();
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: root.openAccepted(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: root.saveAccepted(selectedFile)
        onRejected: root.saveRejected()
    }

    Dialogs.FontDialog {
        id: fontDialog
        objectName: "fontDialog"
        title: "Editor font"
        // Only the family carries over. Size follows the zoom and the desktop
        // text scale, and weight and style are what the Markdown means.
        onAccepted: root.fontAccepted(selectedFont.family)
    }

    Dialogs.FolderDialog {
        id: vaultRootDialog
        title: "Choose the vault folder"
        onAccepted: root.folderAccepted(selectedFolder)
    }
}
