#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QFileSystemWatcher>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

class MarkdownHighlighter;
class QTextBlock;
class QTextCursor;
class QTextDocument;
class QWindow;
class QLockFile;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool untitled READ untitled NOTIFY fileUrlChanged)
    Q_PROPERTY(QStringList draftPaths READ draftPaths NOTIFY draftsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordCountChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeCodeBackground READ themeCodeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeInlineCodeBackground READ themeInlineCodeBackground NOTIFY themeColorsChanged)
    // Whether this run was handed a file to read. Fixed for the life of the
    // process: a note opened later is a note, not the reason the window exists.
    Q_PROPERTY(bool startedWithFile READ startedWithFile CONSTANT)
    Q_PROPERTY(QString themeMarker READ themeMarker NOTIFY themeColorsChanged)
    // Padding inside a rendered block, in pixels at text scale 1. The editor
    // insets the text of code blocks and tables by this much; QML bleeds the
    // slab out by the same amount on all four sides.
    Q_PROPERTY(int blockPadding READ blockPadding CONSTANT)
    // What the sidebar should call the open note while its file is still the
    // placeholder a new note is created with. Empty once it has a real name.
    Q_PROPERTY(QString placeholderTitle READ placeholderTitle NOTIFY placeholderTitleChanged)

public:
    // How the open file separates its lines, and whether it starts with a UTF-8
    // byte order mark. Both are properties of the bytes on disk, not of the
    // document, so they are carried across a load and replayed on every save.
    enum class LineEnding { Lf, CrLf };

    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);
    // The file named on the command line, before the interface is loaded, so
    // the window can lay itself out for reading a document rather than for
    // working through a vault. main() opens it once there is a document to
    // open it into.
    void setStartupFile(const QString &path) { m_startupFile = path; }
    QString startupFile() const { return m_startupFile; }
    bool startedWithFile() const { return !m_startupFile.isEmpty(); }

    QUrl fileUrl() const { return m_fileUrl; }
    QString fileName() const;

    bool modified() const { return m_modified; }
    // No file behind the buffer yet: a jotting, not a note.
    bool untitled() const { return !m_fileUrl.isValid() || m_fileUrl.isEmpty(); }
    QString status() const { return m_status; }
    int wordCount() const { return m_wordCount; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QString themeCodeBackground() const;
    QString themeInlineCodeBackground() const;
    QString themeMarker() const;
    int blockPadding() const { return m_blockPadding; }
    static int countWords(const QString &text);
    static QString decodeFileContents(const QByteArray &bytes, LineEnding *lineEnding,
                                      bool *hasByteOrderMark);
    static QByteArray encodeFileContents(const QString &text, LineEnding lineEnding,
                                         bool hasByteOrderMark);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);
    // The first line that has anything on it, without the marks that make it a
    // heading, a list item, a task or a quote, capped at 64 characters. Empty
    // when the text is blank.
    static QString titleFromText(const QString &text);
    // A name `VaultModel::createNote` hands out: untitled.md, untitled-2.md.
    static bool isPlaceholderName(const QString &fileName);
    QString placeholderTitle() const;

    Q_INVOKABLE void attachDocument(QObject *textDocument);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveForClose();
    Q_INVOKABLE void saveAsDialog();
    Q_INVOKABLE void saveAs(const QUrl &url);
    Q_INVOKABLE void fileDialogCanceled();
    // Every file that has text not on disk, the open one included.
    QStringList draftPaths() const;
    Q_INVOKABLE void persistDraft();
    Q_INVOKABLE void discardDraftFor(const QUrl &url);
    Q_INVOKABLE void discardRecovery();
    // Lets go of the open file without touching it: the buffer empties, the
    // watcher stops, and the draft under it is dropped. For deleting the note
    // that is open, where the watcher would otherwise call our own delete an
    // outside change.
    Q_INVOKABLE void closeFile();
    // Throws the unsaved text away and reads the note back off the disk.
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE void reloadFromDisk();
    Q_INVOKABLE void keepExternalVersion();
    Q_INVOKABLE void printDocument();
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool editorTextChanged();
    Q_INVOKABLE QVariantList hiddenRangesAt(int position) const;
    Q_INVOKABLE void setSearchHighlight(const QString &query, int currentMatchStart);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    // The link or path under a document position, empty when there is none.
    Q_INVOKABLE QString linkTargetAt(int position) const;
    Q_INVOKABLE void openTarget(const QString &target) const;
    // Document position of a task item's mark, or -1.
    Q_INVOKABLE int taskMarkerAt(int position) const;
    // The portal font chooser can hand back a styled name such as
    // "JetBrains Mono NL Bold". Resolve it to a family the font database
    // knows, or to nothing, which means the bundled font.
    Q_INVOKABLE static QString resolveFontFamily(const QString &family);
    // The writing surface's family when view/fontFamily is not set: the
    // desktop's own monospace, then the font in the binary. Monospace either
    // way, because a table only lines up if every glyph is the same width.
    Q_INVOKABLE static QString defaultEditorFontFamily();
    // First and last document position of every run of fenced-code lines, so
    // QML can draw one slab behind each of them.
    Q_INVOKABLE QVariantList fencedCodeRegions() const;
    // Document position of each thematic break, so QML can draw the rule.
    Q_INVOKABLE QList<int> thematicBreakPositions() const;
    // One entry per run of table rows: where it starts and ends, and where
    // its separator row sits so a rule can be drawn there.
    Q_INVOKABLE QVariantList tableRegions() const;
    // Document position of each hidden `*` list marker.
    Q_INVOKABLE QList<int> asteriskBulletPositions() const;
    // One rectangle per inline code span, in the editor's own coordinates, so
    // QML can draw a rounded chip behind it. A span that wraps gets one
    // rectangle per line it runs through.
    Q_INVOKABLE QVariantList inlineCodeRegions() const;
    // Pad the table under the caret so its columns line up. An explicit
    // edit, undoable, and the only thing here that rewrites the buffer.
    Q_INVOKABLE bool alignTableAt(int position);
    // For tests: the grid set is normally refreshed by an edit.
    void updateTableGridsForTest() { updateTableGrids(); }
    // The caret's block shows its markers as written.
    Q_INVOKABLE void setCursorPosition(int position);
    Q_INVOKABLE QVariantMap viewState() const;
    Q_INVOKABLE void saveViewState(qreal zoom, bool fullWidth,
                                   const QString &fontFamily, int contentColumns,
                                   const QString &interfaceFontFamily);
    // view/interfaceFontFamily, resolved to a family the font database knows.
    // Empty means the bundled font. Read before any Backend exists, so static.
    static QString interfaceFontFamily();
    Q_INVOKABLE QVariantMap sidebarState() const;
    Q_INVOKABLE void saveSidebarState(bool visible, int width);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height,
                                        bool maximized, bool fullScreen);

signals:
    void fileUrlChanged();
    void modifiedChanged();
    void statusChanged();
    void wordCountChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void closeAfterSave();
    void openDialogRequested();
    void saveDialogRequested(const QUrl &suggestedUrl);
    void saveSucceeded();
    void draftsChanged();
    void externalChangeDetected(bool deleted, bool locallyModified);
    void placeholderTitleChanged();
    // A draft's text has just been written to its own file, so the sidebar can
    // pick up its title.
    void draftWritten();
    // A note's text has just been put in the buffer, by an open or a restored
    // draft. Setting the text leaves the document cursor at the end, so the
    // view has to be told to go back to the top.
    void documentLoaded();

private:
    void loadDocumentText(const QString &text);
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QString currentDocumentText() const;
    // The first save of a still-empty placeholder names the file after the
    // text. Returns the URL to carry on with, which is the old one if there is
    // nothing to name it or the rename fails.
    QUrl renameToTitle(const QUrl &url, const QString &text);
    void updateCurrentTitle(const QString &text);
    // True for a note that has no name of its own yet: one in a drafts folder,
    // or an untitled.md left by an older version.
    bool currentIsUnnamed() const;
    // Drafts are the app's own scratch files, so they are written as you go.
    // Returns true when something was written.
    bool writeDraftFile();
    QString resolveLocalPath(const QString &token) const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void applyDocumentTypography();
    void updateTableGrids();
    int tableRunStart(const QTextBlock &block) const;
    void reapplyTypographyToChange();
    bool isCodeBlock(const QTextBlock &block) const;
    // What a block is styled as. Working it out costs a regex, so the
    // typography pass does it once per block and passes it down.
    enum class BlockKind { Prose, Code, TableRow };
    BlockKind blockKind(const QTextBlock &block) const;
    qreal lineHeightFor(BlockKind kind) const;
    qreal blockMarginFor(BlockKind kind) const;
    bool hasWantedTypography(const QTextBlock &block, BlockKind kind) const;
    void applyBlockTypography(QTextCursor &cursor, const QTextBlock &block, BlockKind kind);
    void scheduleRecovery();
    void writeRecovery();
    void stashDraft();
    QString draftKey() const;
    void restoreRecovery();
    void clearRecovery();
    QString recoveryPath() const;
    void watchCurrentFile();
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    QUrl m_fileUrl;
    QString m_startupFile;
    bool m_modified = false;
    QString m_status;
    QString m_currentTitle;
    int m_wordCount = 0;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    bool m_loading = false;
    bool m_closeAfterSave = false;
    bool m_formattingTypography = false;
    int m_formattedBlockCount = 0;
    int m_activeBlockNumber = -1;
    int m_revealedFirstBlock = -1;
    int m_revealedLastBlock = -1;
    // A table is tidied when the caret leaves it, and only if it was typed
    // in: visiting one must not rewrite it, and opening a file must not
    // touch anything at all.
    int m_editedTableFirstBlock = -1;
    // The caret is put at the top of a freshly loaded note by the view, not by
    // the reader. That placement does not count as visiting what is there.
    bool m_cursorFollowsLoad = false;
    bool m_aligningTable = false;
    // Percentages of the line's own font size, from settings.
    qreal m_lineHeight = 140;
    qreal m_codeLineHeight = 125;
    qreal m_tableLineHeight = 120;
    // Pixels at text scale 1, from settings.
    int m_blockPadding = 12;
    QString m_codeFontFamily;
    int m_lastChangePos = 0;
    int m_lastChangeAdded = 0;
    QTimer m_wordCountTimer;
    QTimer m_recoveryTimer;
    QFileSystemWatcher m_fileWatcher;
    QPointer<QTextDocument> m_document;
    QPointer<QWindow> m_parentWindow;
    QPointer<MarkdownHighlighter> m_highlighter;
    QString m_lastDocumentText;
    QByteArray m_lastKnownFileContents;
    bool m_hasKnownFileContents = false;
    LineEnding m_lineEnding = LineEnding::Lf;
    bool m_hasByteOrderMark = false;
    // Unsaved text per file, so switching notes never asks and never loses
    // anything. Keyed by file URL; the empty key is the untitled buffer.
    QHash<QString, QString> m_drafts;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
