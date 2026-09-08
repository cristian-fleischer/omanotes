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
    Q_PROPERTY(QString themeMarker READ themeMarker NOTIFY themeColorsChanged)
    // Padding inside a rendered block, in pixels at text scale 1. The editor
    // insets the text of code blocks and tables by this much; QML bleeds the
    // slab out by the same amount on all four sides.
    Q_PROPERTY(int blockPadding READ blockPadding CONSTANT)

public:
    // How the open file separates its lines, and whether it starts with a UTF-8
    // byte order mark. Both are properties of the bytes on disk, not of the
    // document, so they are carried across a load and replayed on every save.
    enum class LineEnding { Lf, CrLf };

    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);

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
    QString themeMarker() const;
    int blockPadding() const { return m_blockPadding; }
    static int countWords(const QString &text);
    static QString decodeFileContents(const QByteArray &bytes, LineEnding *lineEnding,
                                      bool *hasByteOrderMark);
    static QByteArray encodeFileContents(const QString &text, LineEnding lineEnding,
                                         bool hasByteOrderMark);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);

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
    // "iA Writer Mono S Bold". Resolve it to a family the font database
    // knows, or to nothing, which means the bundled font.
    Q_INVOKABLE static QString resolveFontFamily(const QString &family);
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

private:
    void loadDocumentText(const QString &text);
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QString currentDocumentText() const;
    QString resolveLocalPath(const QString &token) const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void applyDocumentTypography();
    void updateTableGrids();
    int tableRunStart(const QTextBlock &block) const;
    void reapplyTypographyToChange();
    bool isCodeBlock(const QTextBlock &block) const;
    qreal lineHeightForBlock(const QTextBlock &block) const;
    qreal blockMarginFor(const QTextBlock &block) const;
    bool hasWantedTypography(const QTextBlock &block) const;
    void applyBlockTypography(QTextCursor &cursor, const QTextBlock &block);
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
    bool m_modified = false;
    QString m_status;
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
