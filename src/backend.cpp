#include "backend.h"
#include "vaultmodel.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

// Percentages of the line's own font size. Prose gets Typora's 140. Code gets
// 100, the font's natural line spacing, because box-drawing characters only
// tile into continuous lines at that height: any leading breaks a diagram's
// verticals into dashes. Tables sit between, since a pipe never joins anyway.
// All three are settings, because the right answer depends on the font.
constexpr qreal defaultLineHeightPercent = 140;
constexpr qreal defaultCodeLineHeightPercent = 100;
constexpr qreal defaultTableLineHeightPercent = 120;
// Pixels at text scale 1, inside a code block or a table.
constexpr int defaultBlockPadding = 12;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    QSettings settings;
    m_lineHeight = settings.value(QStringLiteral("typography/lineHeight"),
                                  defaultLineHeightPercent).toDouble();
    m_codeLineHeight = settings.value(QStringLiteral("typography/codeLineHeight"),
                                      defaultCodeLineHeightPercent).toDouble();
    m_tableLineHeight = settings.value(QStringLiteral("typography/tableLineHeight"),
                                       defaultTableLineHeightPercent).toDouble();
    m_blockPadding = qBound(0, settings.value(QStringLiteral("typography/blockPadding"),
                                              defaultBlockPadding).toInt(), 80);
    m_codeFontFamily = settings.value(QStringLiteral("typography/codeFontFamily")).toString();

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    // Block margins are in pixels, so they do not follow the scale on their own.
    if (m_document)
        applyDocumentTypography();
    emit textScaleChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    m_highlighter->setCodeFontFamily(m_codeFontFamily);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
            });

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

QString Backend::draftKey() const {
    return m_fileUrl.isValid() ? m_fileUrl.toString() : QString();
}

QStringList Backend::draftPaths() const {
    QStringList paths;
    for (auto it = m_drafts.constBegin(); it != m_drafts.constEnd(); ++it) {
        const QUrl url(it.key());
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    // The buffer on screen is not in the map until it is left.
    if (m_modified && m_fileUrl.isLocalFile()) {
        const QString current = m_fileUrl.toLocalFile();
        if (!paths.contains(current))
            paths.append(current);
    }
    return paths;
}

// Leaving a note keeps what was typed in it rather than asking about it.
void Backend::stashDraft() {
    if (writeDraftFile())
        emit draftWritten();

    if (!m_modified || !m_document)
        return;
    m_drafts.insert(draftKey(), currentDocumentText());
    emit draftsChanged();
}

void Backend::discardDraftFor(const QUrl &url) {
    if (m_drafts.remove(url.toString()) > 0) {
        writeRecovery();
        emit draftsChanged();
    }
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    stashDraft();

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QFile file(url.toLocalFile());
    // Not QIODevice::Text: that mode folds CRLF to LF on the way in, and the
    // save path would then write the file back with the endings changed.
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    const QString onDisk = decodeFileContents(contents, &m_lineEnding, &m_hasByteOrderMark);

    // A note left with unsaved text comes back with it, still unsaved.
    const auto draft = m_drafts.constFind(url.toString());
    const bool hadDraft = draft != m_drafts.constEnd();
    loadDocumentText(hadDraft ? *draft : onDisk);

    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(hadDraft);
    setStatus(hadDraft ? QStringLiteral("Unsaved changes in %1").arg(fileName())
                       : QStringLiteral("Opened %1").arg(fileName()));
    writeRecovery();
    emit draftsChanged();
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

// Closing the window is not a decision about the text. Whatever is unsaved goes
// to the snapshot now, rather than on the next tick of the timer, and comes back
// in the window that opens next.
void Backend::persistDraft() {
    m_recoveryTimer.stop();
    writeRecovery();
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::closeFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    m_drafts.remove(m_fileUrl.toString());
    setFileUrl(QUrl());
    m_lastKnownFileContents.clear();
    m_hasKnownFileContents = false;
    loadDocumentText(QString());
    setModified(false);
    setStatus(QString());
    emit draftsChanged();
    writeRecovery();
}

void Backend::discardChanges() {
    if (!m_fileUrl.isLocalFile())
        return;

    const QUrl url = m_fileUrl;
    // Clearing the mark first is what makes this a discard rather than a
    // reopen: open() stashes a modified buffer on the way out and hands it
    // straight back on the way in, which is right everywhere else.
    setModified(false);
    m_drafts.remove(url.toString());
    open(url);
    writeRecovery();
    emit draftsChanged();
}

void Backend::reloadFromDisk() {
    discardChanges();
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    if (m_document) {
        // Not only when blocks are added: typing a pipe turns a prose line into
        // a table row, which wants a different line height at the same count.
        reapplyTypographyToChange();
        m_formattedBlockCount = m_document->blockCount();
        updateTableGrids();

        // Remember that this table was typed in, so it can be tidied when the
        // caret leaves it.
        if (!m_aligningTable) {
            const QTextBlock caret = m_document->findBlockByNumber(m_activeBlockNumber);
            const int run = tableRunStart(caret);
            if (run >= 0)
                m_editedTableFirstBlock = run;
        }
    }

    scheduleWordCount();
    updateCurrentTitle(text);
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    return true;
}

QVariantList Backend::hiddenRangesAt(int position) const {
    QVariantList ranges;
    if (!m_document)
        return ranges;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return ranges;

    const int lineStart = block.position();
    QList<QPair<int, int>> spans;
    // A line inside a fence has no hidden markers, so the caret must not skip
    // over the asterisks in a shell glob.
    const QList<MarkdownHighlighter::InlineMarkup> markup =
        MarkdownHighlighter::inlineMarkup(
            block.text(), MarkdownHighlighter::isFencedState(block.userState()));
    for (const MarkdownHighlighter::InlineMarkup &item : markup) {
        for (const MarkdownHighlighter::Span &marker : item.markers) {
            spans.append({lineStart + marker.start,
                          lineStart + marker.start + marker.length});
        }
    }
    std::sort(spans.begin(), spans.end());

    for (const auto &span : spans) {
        ranges.append(QVariantMap{{QStringLiteral("start"), span.first},
                                  {QStringLiteral("end"), span.second}});
    }
    return ranges;
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

int Backend::taskMarkerAt(int position) const {
    if (!m_document)
        return -1;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return -1;

    const int mark = MarkdownHighlighter::taskMarkColumn(block.text());
    if (mark < 0)
        return -1;

    // The whole `[ ]` is the target, so the click does not have to land on the
    // one character between the brackets.
    const int inBlock = position - block.position();
    if (inBlock < mark - 1 || inBlock > mark + 1)
        return -1;
    return block.position() + mark;
}

QString Backend::linkTargetAt(int position) const {
    if (!m_document)
        return {};

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return {};

    const QString text = block.text();
    const int inBlock = position - block.position();

    // A Markdown link wins: the destination is what the reader means, not the
    // words they clicked on.
    static const QRegularExpression linkRe(
        QStringLiteral("!?\\[[^\\]]*\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator links = linkRe.globalMatch(text);
    while (links.hasNext()) {
        const QRegularExpressionMatch link = links.next();
        if (inBlock >= link.capturedStart(0) && inBlock <= link.capturedEnd(0))
            return link.captured(1).trimmed();
    }

    // Then anything that looks like a URL or a path, bounded by whitespace and
    // the brackets Markdown wraps things in.
    static const QRegularExpression tokenRe(QStringLiteral("[^\\s<>()\\[\\]\"\'`]+"));
    QRegularExpressionMatchIterator tokens = tokenRe.globalMatch(text);
    while (tokens.hasNext()) {
        const QRegularExpressionMatch token = tokens.next();
        if (inBlock < token.capturedStart(0) || inBlock > token.capturedEnd(0))
            continue;

        QString candidate = token.captured(0);
        // Trailing punctuation belongs to the sentence, not the link.
        while (!candidate.isEmpty()
                && QStringLiteral(".,;:!?").contains(candidate.back()))
            candidate.chop(1);
        if (candidate.isEmpty())
            return {};
        if (!normalizedLinkUrl(candidate).isEmpty())
            return candidate;
        if (!resolveLocalPath(candidate).isEmpty())
            return candidate;
        return {};
    }

    return {};
}

// Absolute path for a token, if it names something that exists. Relative names
// are resolved next to the open note, which is what a note means by them.
QString Backend::resolveLocalPath(const QString &token) const {
    if (token.isEmpty() || token.contains(QLatin1Char(':')))
        return {};

    QString path = token;
    if (path == QStringLiteral("~") || path.startsWith(QStringLiteral("~/")))
        path.replace(0, 1, QDir::homePath());

    QFileInfo info(path);
    if (info.isRelative() && m_fileUrl.isLocalFile()) {
        const QDir noteDirectory = QFileInfo(m_fileUrl.toLocalFile()).absoluteDir();
        info = QFileInfo(noteDirectory.filePath(path));
    }
    return info.exists() ? info.absoluteFilePath() : QString();
}

void Backend::openTarget(const QString &target) const {
    const QString url = normalizedLinkUrl(target);
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
        return;
    }

    // A path is only opened when it actually names something, so a stray word
    // in a note cannot hand an arbitrary string to the desktop.
    const QString path = resolveLocalPath(target);
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

// Every getter here converts before returning. QSettings hands INI values back
// as strings, and the string "false" is a true bool the moment QML touches it.
QString Backend::resolveFontFamily(const QString &family) {
    const QString wanted = family.trimmed();
    if (wanted.isEmpty())
        return {};

    const QStringList known = QFontDatabase::families();
    QStringList words = wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Longest prefix wins, so "iA Writer Mono S Bold" resolves to the family and
    // not to "iA Writer", which is also a real one.
    while (!words.isEmpty()) {
        const QString candidate = words.join(QLatin1Char(' '));
        for (const QString &name : known) {
            if (name.compare(candidate, Qt::CaseInsensitive) == 0)
                return name;
        }
        words.removeLast();
    }
    return {};
}

QList<int> Backend::thematicBreakPositions() const {
    QList<int> positions;
    if (!m_document)
        return positions;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        // The line being edited shows the marks it was written with, so no rule
        // is drawn over it. `***` is a thematic break and the opening of
        // `***bold italic***`, and drawing a line across the page the moment
        // the third asterisk lands makes typing one alarming.
        if (block.userState() == MarkdownHighlighter::ThematicBreak
                && block.blockNumber() != m_activeBlockNumber)
            positions.append(block.position());
    }
    return positions;
}

QList<int> Backend::asteriskBulletPositions() const {
    QList<int> positions;
    if (!m_document)
        return positions;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        // The line being edited shows the asterisk it was written with, so no
        // bullet is drawn over it.
        if (MarkdownHighlighter::isFencedState(block.userState())
                || block.blockNumber() == m_activeBlockNumber)
            continue;
        const int column = MarkdownHighlighter::asteriskBulletColumn(block.text());
        if (column >= 0)
            positions.append(block.position() + column);
    }
    return positions;
}

QVariantList Backend::tableRegions() const {
    QVariantList regions;
    if (!m_document)
        return regions;

    QTextBlock first;
    QTextBlock previous;
    int separator = -1;

    // Character columns holding a pipe in every row of the run. Only those can
    // be drawn as one continuous rule; a table whose source is not aligned has
    // none in common and keeps the pipes it was written with.
    QList<int> shared;
    bool firstRow = true;
    bool aligned = true;

    const auto flush = [&]() {
        if (!first.isValid())
            return;
        // Column rules are only drawn through a table whose source lines up in
        // every row. Half a grid, drawn through the one column that happens to
        // agree, reads worse than no grid at all.
        // A table being edited shows the source it is written in: no rules
        // drawn over it, nothing folded away.
        const bool editing = m_revealedFirstBlock >= 0
            && first.blockNumber() <= m_revealedLastBlock
            && previous.blockNumber() >= m_revealedFirstBlock;

        QVariantList columns;
        if (aligned && !editing) {
            for (int column : std::as_const(shared))
                columns.append(column);
        }
        // The rule where the separator row was belongs to the grid. Drawn on
        // its own across a table with no column rules it is a line sticking
        // out past both ends of the text, which is the half grid again.
        const bool drawsGrid = !columns.isEmpty();
        regions.append(QVariantMap{{QStringLiteral("start"), first.position()},
                                   {QStringLiteral("end"), previous.position()},
                                   {QStringLiteral("separator"), drawsGrid ? separator : -1},
                                   {QStringLiteral("editing"), editing},
                                   {QStringLiteral("columns"), columns}});
        first = QTextBlock();
        separator = -1;
        shared.clear();
        firstRow = true;
        aligned = true;
    };

    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        if (block.userState() == MarkdownHighlighter::TableRow) {
            if (!first.isValid())
                first = block;
            if (separator < 0 && MarkdownHighlighter::isTableSeparator(block.text()))
                separator = block.position();

            const QString row = block.text();
            QList<int> pipes;
            for (int i = 0; i < row.length(); ++i) {
                if (row.at(i) == QLatin1Char('|'))
                    pipes.append(i);
            }
            if (firstRow) {
                shared = pipes;
                firstRow = false;
            } else if (pipes != shared) {
                aligned = false;
            }

            previous = block;
            continue;
        }
        flush();
    }
    flush();
    return regions;
}

// Which table rows get a drawn grid, so the highlighter knows whose pipes to
// fold away. Only Backend can tell: it takes a whole run of rows to decide.
void Backend::updateTableGrids() {
    if (!m_document || !m_highlighter)
        return;

    QSet<int> gridded;
    const QVariantList regions = tableRegions();
    for (const QVariant &entry : regions) {
        const QVariantMap region = entry.toMap();
        if (region.value(QStringLiteral("columns")).toList().isEmpty())
            continue;
        const QTextBlock last = m_document->findBlock(region.value(QStringLiteral("end")).toInt());
        for (QTextBlock block = m_document->findBlock(region.value(QStringLiteral("start")).toInt());
                block.isValid(); block = block.next()) {
            gridded.insert(block.blockNumber());
            if (block == last)
                break;
        }
    }
    m_highlighter->setGriddedRows(gridded);
}

// The block number the caret's table run starts at, or -1.
int Backend::tableRunStart(const QTextBlock &block) const {
    if (!block.isValid() || block.userState() != MarkdownHighlighter::TableRow)
        return -1;
    QTextBlock first = block;
    while (first.previous().isValid()
            && first.previous().userState() == MarkdownHighlighter::TableRow)
        first = first.previous();
    return first.blockNumber();
}

bool Backend::alignTableAt(int position) {
    if (!m_document)
        return false;

    const QTextBlock caret =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!caret.isValid() || caret.userState() != MarkdownHighlighter::TableRow)
        return false;

    QTextBlock first = caret;
    while (first.previous().isValid()
            && first.previous().userState() == MarkdownHighlighter::TableRow)
        first = first.previous();
    QTextBlock last = caret;
    while (last.next().isValid() && last.next().userState() == MarkdownHighlighter::TableRow)
        last = last.next();

    // Split each row on its pipes. The outer pipes bound the row, so the cells
    // are what lies between them.
    QList<QStringList> rows;
    QList<bool> separators;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        QStringList cells = block.text().split(QLatin1Char('|'));
        if (cells.size() >= 2) {
            cells.removeFirst();
            cells.removeLast();
        }
        for (QString &cell : cells)
            cell = cell.trimmed();
        rows.append(cells);
        separators.append(MarkdownHighlighter::isTableSeparator(block.text()));
        if (block == last)
            break;
    }
    if (rows.isEmpty())
        return false;

    int columns = 0;
    for (const QStringList &row : std::as_const(rows))
        columns = qMax(columns, int(row.size()));

    QList<int> widths(columns, 3);
    for (int r = 0; r < rows.size(); ++r) {
        if (separators.at(r))
            continue;
        for (int c = 0; c < rows.at(r).size(); ++c)
            widths[c] = qMax(widths.at(c), int(rows.at(r).at(c).size()));
    }

    QStringList aligned;
    for (int r = 0; r < rows.size(); ++r) {
        QString line = QStringLiteral("|");
        for (int c = 0; c < columns; ++c) {
            const QString cell = c < rows.at(r).size() ? rows.at(r).at(c) : QString();
            if (separators.at(r)) {
                // Keep whichever alignment colons the row was written with.
                const bool left = cell.startsWith(QLatin1Char(':'));
                const bool right = cell.endsWith(QLatin1Char(':'));
                // Two wider than the cell, to cover the spaces a data row puts
                // either side of its content.
                QString rule(widths.at(c) + 2, QLatin1Char('-'));
                if (left)
                    rule[0] = QLatin1Char(':');
                if (right)
                    rule[rule.size() - 1] = QLatin1Char(':');
                line += rule + QLatin1Char('|');
            } else {
                line += QLatin1Char(' ') + cell.leftJustified(widths.at(c))
                      + QStringLiteral(" |");
            }
        }
        aligned.append(line);
    }

    // Compare row by row rather than against a slice of the document: raw text
    // separates blocks with U+2029, so a join on "\n" never matched and every
    // pass rewrote a table that was already aligned.
    QStringList current;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        current.append(block.text());
        if (block == last)
            break;
    }
    if (aligned == current)
        return false;

    const QString replacement = aligned.join(QLatin1Char('\n'));

    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    cursor.setPosition(first.position());
    cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
    cursor.endEditBlock();
    return true;
}

void Backend::setCursorPosition(int position) {
    if (!m_document || !m_highlighter)
        return;

    // Setting the text moves the caret to the end of it, and applying the
    // typography moves it again. Neither is the reader putting it anywhere, so
    // neither may reveal a table or count as visiting one.
    if (m_loading || m_formattingTypography)
        return;

    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    if (!block.isValid())
        return;

    // Leaving a table tidies it up, the way an editor reflows a paragraph when
    // you move on. Putting the caret in one is what counts as working on it;
    // opening a note, reading it and scrolling past change nothing.
    bool tidied = false;
    if (m_editedTableFirstBlock >= 0) {
        const QTextBlock edited = m_document->findBlockByNumber(m_editedTableFirstBlock);
        const bool stillInside = edited.isValid()
            && block.userState() == MarkdownHighlighter::TableRow
            && tableRunStart(block) == m_editedTableFirstBlock;
        if (!stillInside) {
            m_editedTableFirstBlock = -1;
            if (edited.isValid()) {
                m_aligningTable = true;
                tidied = alignTableAt(edited.position());
                m_aligningTable = false;
            }
        }
    }

    const bool followsLoad = m_cursorFollowsLoad;
    m_cursorFollowsLoad = false;
    if (!followsLoad && !m_aligningTable
            && block.userState() == MarkdownHighlighter::TableRow) {
        const int run = tableRunStart(block);
        if (run >= 0)
            m_editedTableFirstBlock = run;
    }

    m_activeBlockNumber = block.blockNumber();
    m_highlighter->setActiveBlock(block.blockNumber());

    // Which run of fenced code or table rows the caret is in, if any. A
    // QSyntaxHighlighter sees one block at a time and cannot pair an opening
    // fence with its closing one, or know where a table starts.
    const auto runContaining = [this, &block](bool code) {
        int first = -1;
        int last = -1;
        int runStart = -1;
        int runEnd = -1;
        const auto belongs = [this, code](const QTextBlock &candidate) {
            return code ? isCodeBlock(candidate)
                        : candidate.userState() == MarkdownHighlighter::TableRow;
        };
        for (QTextBlock scan = m_document->begin(); scan.isValid(); scan = scan.next()) {
            if (belongs(scan)) {
                if (runStart < 0)
                    runStart = scan.blockNumber();
                runEnd = scan.blockNumber();
                continue;
            }
            if (runStart >= 0) {
                if (block.blockNumber() >= runStart && block.blockNumber() <= runEnd) {
                    first = runStart;
                    last = runEnd;
                    break;
                }
                runStart = -1;
            }
        }
        if (first < 0 && runStart >= 0 && block.blockNumber() >= runStart
                && block.blockNumber() <= runEnd) {
            first = runStart;
            last = runEnd;
        }
        return QPair<int, int>{first, last};
    };

    QPair<int, int> revealed = runContaining(true);
    if (revealed.first < 0)
        revealed = runContaining(false);

    const bool revealChanged = revealed.first != m_revealedFirstBlock
        || revealed.second != m_revealedLastBlock;
    m_revealedFirstBlock = revealed.first;
    m_revealedLastBlock = revealed.second;
    m_highlighter->setRevealedRange(revealed.first, revealed.second);

    // A revealed table is not a gridded one, so which rows carry a grid has to
    // be worked out again once the caret is recorded where it now is. Only when
    // the range moved, which is when the caret enters or leaves a table or a
    // fenced block, and not on every keystroke.
    if (tidied || revealChanged)
        updateTableGrids();
}

QVariantMap Backend::viewState() const {
    QSettings settings;
    return {{QStringLiteral("zoom"),
             settings.value(QStringLiteral("view/zoom"), 1.0).toDouble()},
            {QStringLiteral("fullWidth"),
             settings.value(QStringLiteral("view/fullWidth"), false).toBool()},
            // Empty means the bundled iA Writer Mono S.
            {QStringLiteral("fontFamily"),
             settings.value(QStringLiteral("view/fontFamily")).toString()},
            // How wide the text column is, in characters, when it is not set to
            // fill the window.
            {QStringLiteral("contentColumns"),
             qBound(20, settings.value(QStringLiteral("view/contentColumns"), 65).toInt(), 300)},
            // The chrome's family. Read at startup by main(), never from the
            // interface, so it only round-trips through here to stay in the file.
            {QStringLiteral("interfaceFontFamily"),
             settings.value(QStringLiteral("view/interfaceFontFamily")).toString()}};
}

QString Backend::interfaceFontFamily() {
    QSettings settings;
    return resolveFontFamily(
        settings.value(QStringLiteral("view/interfaceFontFamily")).toString());
}

void Backend::saveViewState(qreal zoom, bool fullWidth, const QString &fontFamily,
                            int contentColumns, const QString &interfaceFontFamily) {
    QSettings settings;
    settings.setValue(QStringLiteral("view/zoom"), zoom);
    settings.setValue(QStringLiteral("view/fullWidth"), fullWidth);
    // Written even when empty, so every knob is visible in the file rather than
    // only after it has been changed once.
    settings.setValue(QStringLiteral("view/fontFamily"), fontFamily);
    settings.setValue(QStringLiteral("view/contentColumns"), contentColumns);
    settings.setValue(QStringLiteral("view/interfaceFontFamily"), interfaceFontFamily);
}

QVariantMap Backend::sidebarState() const {
    QSettings settings;
    return {{QStringLiteral("visible"),
             settings.value(QStringLiteral("vault/sidebarVisible"), false).toBool()},
            {QStringLiteral("width"),
             settings.value(QStringLiteral("vault/sidebarWidth"), 260).toInt()}};
}

void Backend::saveSidebarState(bool visible, int width) {
    QSettings settings;
    settings.setValue(QStringLiteral("vault/sidebarVisible"), visible);
    settings.setValue(QStringLiteral("vault/sidebarWidth"), width);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1).toInt()},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1).toInt()},
            {QStringLiteral("width"),
             settings.value(QStringLiteral("window/width"), 1280).toInt()},
            {QStringLiteral("height"),
             settings.value(QStringLiteral("window/height"), 820).toInt()},
            {QStringLiteral("maximized"),
             settings.value(QStringLiteral("window/maximized"), false).toBool()},
            {QStringLiteral("fullScreen"),
             settings.value(QStringLiteral("window/fullScreen"), false).toBool()}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized,
                                 bool fullScreen) {
    QSettings settings;
    // The caller passes the last geometry the window had while it was windowed,
    // not the size it has now: a maximised or full-screen window reports the
    // screen's size, which is not the size to come back to.
    settings.setValue(QStringLiteral("window/x"), x);
    settings.setValue(QStringLiteral("window/y"), y);
    settings.setValue(QStringLiteral("window/width"), width);
    settings.setValue(QStringLiteral("window/height"), height);
    settings.setValue(QStringLiteral("window/maximized"), maximized);
    settings.setValue(QStringLiteral("window/fullScreen"), fullScreen);
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
    updateCurrentTitle(text);
    m_editedTableFirstBlock = -1;
    m_cursorFollowsLoad = true;
    emit documentLoaded();
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit placeholderTitleChanged();
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();

    // The first save of a note the app created as untitled.md names the file
    // after its first line. Only while the file is still empty on disk: a note
    // you called untitled yourself keeps the name you gave it.
    const bool namesItself = url == m_fileUrl
        && (VaultModel::isDraftPath(url.toLocalFile())
            || (m_hasKnownFileContents && m_lastKnownFileContents.isEmpty()
                && isPlaceholderName(targetName)));

    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QString text = currentDocumentText();
    const QByteArray contents = encodeFileContents(text, m_lineEnding, m_hasByteOrderMark);
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    const QUrl saved = namesItself ? renameToTitle(url, text) : url;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(saved);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(saved.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    m_drafts.remove(saved.toString());
    writeRecovery();
    emit draftsChanged();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    // The same beat writes the draft's own file, so a crash leaves the text in
    // two places and the sidebar can read a title off the second.
    writeDraftFile();
    if (!m_modified && m_drafts.isEmpty()) {
        QFile::remove(recoveryPath());
        return;
    }
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonObject drafts;
    for (auto it = m_drafts.constBegin(); it != m_drafts.constEnd(); ++it)
        drafts.insert(it.key(), it.value());
    if (m_modified)
        drafts.insert(draftKey(), currentDocumentText());

    const QJsonObject recovery{
        {QStringLiteral("fileUrl"), m_fileUrl.toString()},
        {QStringLiteral("text"), currentDocumentText()},
        {QStringLiteral("drafts"), drafts},
        {QStringLiteral("crlf"), m_lineEnding == LineEnding::CrLf},
        {QStringLiteral("bom"), m_hasByteOrderMark}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    const QJsonObject drafts = recovery.value(QStringLiteral("drafts")).toObject();
    for (auto it = drafts.constBegin(); it != drafts.constEnd(); ++it)
        m_drafts.insert(it.key(), it.value().toString());

    m_lineEnding = recovery.value(QStringLiteral("crlf")).toBool() ? LineEnding::CrLf
                                                                  : LineEnding::Lf;
    m_hasByteOrderMark = recovery.value(QStringLiteral("bom")).toBool();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    setStatus(QStringLiteral("Unsaved draft restored"));
    emit draftsChanged();
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    m_drafts.clear();
    QFile::remove(recoveryPath());
    emit draftsChanged();
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile()) {
        // A draft has no name worth proposing. Offer what it would be called,
        // in the folder it would land in.
        if (currentIsUnnamed() && !m_currentTitle.isEmpty()) {
            const QString path = m_fileUrl.toLocalFile();
            const QString owner = VaultModel::folderForDraft(path);
            const QDir directory = owner.isEmpty() ? QFileInfo(path).dir() : QDir(owner);
            return QUrl::fromLocalFile(
                directory.filePath(suggestedFileName(m_currentTitle)));
        }
        return m_fileUrl;
    }

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir::home()
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    if (!m_document)
        return QString();

    // QTextDocument::toPlainText() rewrites the buffer on the way out: it folds
    // U+00A0 to a plain space and U+2028 to a newline. Take the raw text and
    // translate only the block separator, so a save writes back what was read.
    QString text = m_document->toRawText();
    text.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
    // Frame boundaries, which a plain-text document has none of. Cheap
    // insurance against a stray one reaching the file.
    text.remove(QChar(0xfdd0));
    text.remove(QChar(0xfdd1));
    return text;
}

QString Backend::decodeFileContents(const QByteArray &bytes, LineEnding *lineEnding,
                                    bool *hasByteOrderMark) {
    static const QByteArray utf8Bom("\xef\xbb\xbf", 3);
    const bool bom = bytes.startsWith(utf8Bom);
    const QByteArray payload = bom ? bytes.mid(utf8Bom.size()) : bytes;

    int newlines = 0;
    int carriageReturnNewlines = 0;
    for (qsizetype i = 0; i < payload.size(); ++i) {
        if (payload.at(i) != '\n')
            continue;
        ++newlines;
        if (i > 0 && payload.at(i - 1) == '\r')
            ++carriageReturnNewlines;
    }

    // CRLF only when every newline is one. A file with mixed endings keeps its
    // bytes as they are rather than being normalised into something nobody wrote.
    const LineEnding ending = newlines > 0 && carriageReturnNewlines == newlines
        ? LineEnding::CrLf
        : LineEnding::Lf;

    // QTextDocument turns every carriage return into a block break, so the
    // buffer can never hold one. Normalise on the way in, and put CRLF back on
    // the way out for a file whose newlines are all CRLF. A file with mixed
    // endings is the one case that changes shape: it is written back with LF.
    QString text = QString::fromUtf8(payload);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    if (lineEnding)
        *lineEnding = ending;
    if (hasByteOrderMark)
        *hasByteOrderMark = bom;
    return text;
}

QByteArray Backend::encodeFileContents(const QString &text, LineEnding lineEnding,
                                       bool hasByteOrderMark) {
    QString out = text;
    if (lineEnding == LineEnding::CrLf)
        out.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QByteArray bytes = out.toUtf8();
    if (hasByteOrderMark)
        bytes.prepend("\xef\xbb\xbf", 3);
    return bytes;
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::titleFromText(const QString &text) {
    // Scans to the first line with anything on it and stops, so this costs the
    // same on a 60 KB note as on a one-liner.
    QStringView line;
    for (qsizetype start = 0; start < text.size();) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0)
            end = text.size();
        const QStringView candidate = QStringView(text).mid(start, end - start).trimmed();
        if (!candidate.isEmpty()) {
            line = candidate;
            break;
        }
        start = end + 1;
    }
    if (line.isEmpty())
        return {};

    // A title is what the line says, not how it is marked up: drop the quote,
    // heading, list and task marks in front of it, and any emphasis wrapped
    // around the whole of it.
    static const QRegularExpression marks(QStringLiteral(
        "^(?:>\\s*)*(?:#{1,6}\\s+|[-*+]\\s+|\\d+[.)]\\s+)?(?:\\[[ xX]\\]\\s+)?"));
    static const QRegularExpression closingHashes(QStringLiteral("\\s+#+$"));
    static const QRegularExpression unsafe(QStringLiteral("[/\\x00-\\x1f\\x7f]"));
    static const QRegularExpression whitespaceRuns(QStringLiteral("\\s+"));

    QString title = line.toString();
    title.remove(marks);
    title.remove(closingHashes);
    while (!title.isEmpty() && QStringLiteral("*_`").contains(title.at(0)))
        title.remove(0, 1);
    while (!title.isEmpty() && QStringLiteral("*_`").contains(title.at(title.size() - 1)))
        title.chop(1);
    // Whitespace first: a tab inside a title is a space, not a stray byte.
    title.replace(whitespaceRuns, QStringLiteral(" "));
    title.replace(unsafe, QStringLiteral("-"));
    title = title.left(64).trimmed();
    if (title == QStringLiteral(".") || title == QStringLiteral(".."))
        return {};
    return title;
}

bool Backend::isPlaceholderName(const QString &fileName) {
    static const QRegularExpression placeholder(
        QStringLiteral("^untitled(?:-\\d+)?\\.md$"), QRegularExpression::CaseInsensitiveOption);
    return placeholder.match(fileName).hasMatch();
}

bool Backend::currentIsUnnamed() const {
    if (!m_fileUrl.isLocalFile())
        return false;
    const QString path = m_fileUrl.toLocalFile();
    return VaultModel::isDraftPath(path)
        || isPlaceholderName(QFileInfo(path).fileName());
}

QString Backend::placeholderTitle() const {
    return currentIsUnnamed() ? m_currentTitle : QString();
}

// A draft is the app's own file, in its own folder, so it is written as it is
// typed. That is what lets the sidebar keep calling it by its first line after
// you have moved on to another note.
bool Backend::writeDraftFile() {
    if (!m_fileUrl.isLocalFile() || !VaultModel::isDraftPath(m_fileUrl.toLocalFile()))
        return false;

    QSaveFile file(m_fileUrl.toLocalFile());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray contents =
        encodeFileContents(currentDocumentText(), m_lineEnding, m_hasByteOrderMark);
    file.write(contents);

    // The open file is watched, and QSaveFile commits by replacing it. Without
    // dropping the watch first, the app reports its own autosave as somebody
    // else editing the note.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (!file.commit()) {
        watchCurrentFile();
        return false;
    }
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    watchCurrentFile();
    return true;
}

void Backend::updateCurrentTitle(const QString &text) {
    const QString title = titleFromText(text);
    if (title == m_currentTitle)
        return;
    m_currentTitle = title;
    emit placeholderTitleChanged();
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = titleFromText(text);
    if (name.isEmpty())
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

QUrl Backend::renameToTitle(const QUrl &url, const QString &text) {
    QString stem = titleFromText(text);
    // A leading dot would hide the note, and the vault skips dotfiles.
    while (stem.startsWith(QLatin1Char('.')))
        stem.remove(0, 1);
    if (stem.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        stem.chop(3);
    stem = stem.trimmed();
    if (stem.isEmpty())
        return url;

    const QFileInfo info(url.toLocalFile());
    // A draft leaves the drafts folder for the one it belongs to. Anything
    // else keeps the folder it is already in.
    const QString owner = VaultModel::folderForDraft(info.absoluteFilePath());
    const QDir directory = owner.isEmpty() ? info.dir() : QDir(owner);
    QString name = stem + QStringLiteral(".md");
    for (int suffix = 2; directory.exists(name); ++suffix)
        name = QStringLiteral("%1-%2.md").arg(stem).arg(suffix);

    if (name == info.fileName() && directory.canonicalPath() == info.dir().canonicalPath())
        return url;
    if (!QFile::rename(info.absoluteFilePath(), directory.filePath(name)))
        return url;

    m_drafts.remove(url.toString());
    return QUrl::fromLocalFile(directory.filePath(name));
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

// Line height is a block property, so it is the one part of the styling the
// highlighter cannot do: QSyntaxHighlighter only sets character formats.
bool Backend::isCodeBlock(const QTextBlock &block) const {
    if (MarkdownHighlighter::isFencedState(block.userState()))
        return true;
    // A closing fence carries the Normal state, so the row itself still has to
    // be recognised. This runs for every block of the document on every edit,
    // so rule out the ones that cannot be a fence before reaching for a regex.
    const QString text = block.text();
    if (!text.contains(QLatin1Char('`')) && !text.contains(QLatin1Char('~')))
        return false;
    return MarkdownHighlighter::isFenceLine(text);
}

Backend::BlockKind Backend::blockKind(const QTextBlock &block) const {
    if (isCodeBlock(block))
        return BlockKind::Code;
    if (MarkdownHighlighter::isTableRow(block.text()))
        return BlockKind::TableRow;
    return BlockKind::Prose;
}

qreal Backend::lineHeightFor(BlockKind kind) const {
    switch (kind) {
    case BlockKind::Code: return m_codeLineHeight;
    case BlockKind::TableRow: return m_tableLineHeight;
    case BlockKind::Prose: break;
    }
    return m_lineHeight;
}

// Code and tables are drawn on a slab, so their text is inset from the column
// the prose uses. Everything else sits flush against it. The margin follows the
// desktop's text scale rather than the zoom, which is what the slab drawn
// around it in QML does.
qreal Backend::blockMarginFor(BlockKind kind) const {
    return kind == BlockKind::Prose ? 0.0 : m_blockPadding * m_textScale;
}

bool Backend::hasWantedTypography(const QTextBlock &block, BlockKind kind) const {
    const QTextBlockFormat format = block.blockFormat();
    const qreal margin = blockMarginFor(kind);
    return format.lineHeightType() == QTextBlockFormat::ProportionalHeight
        && qFuzzyCompare(format.lineHeight(), lineHeightFor(kind))
        && qFuzzyCompare(format.leftMargin() + 1.0, margin + 1.0)
        && qFuzzyCompare(format.rightMargin() + 1.0, margin + 1.0);
}

void Backend::applyBlockTypography(QTextCursor &cursor, const QTextBlock &block, BlockKind kind) {
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(lineHeightFor(kind), QTextBlockFormat::ProportionalHeight);
    const qreal margin = blockMarginFor(kind);
    blockFormat.setLeftMargin(margin);
    blockFormat.setRightMargin(margin);
    cursor.setPosition(block.position());
    cursor.mergeBlockFormat(blockFormat);
}

QString Backend::themeMarker() const {
    return MarkdownHighlighter::markerColorFor(m_darkMode).name();
}

QString Backend::themeCodeBackground() const {
    return MarkdownHighlighter::codeBackgroundFor(m_themeBackground, m_darkMode).name();
}

QVariantList Backend::fencedCodeRegions() const {
    QVariantList regions;
    if (!m_document)
        return regions;

    QTextBlock first;
    QTextBlock previous;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        if (isCodeBlock(block)) {
            if (!first.isValid())
                first = block;
            previous = block;
            continue;
        }
        if (first.isValid()) {
            regions.append(QVariantMap{{QStringLiteral("start"), first.position()},
                                       {QStringLiteral("end"), previous.position()}});
            first = QTextBlock();
        }
    }
    if (first.isValid()) {
        regions.append(QVariantMap{{QStringLiteral("start"), first.position()},
                                   {QStringLiteral("end"), previous.position()}});
    }
    return regions;
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);

    m_formattingTypography = true;
    QTextCursor cursor(m_document);

    // One operation for the whole document, then a second pass over the few
    // blocks that want something else. Merging a format into every block on
    // its own laid the document out once per block: 660 ms on a 1,500-line
    // note, against 20 ms this way.
    cursor.select(QTextCursor::Document);
    QTextBlockFormat prose;
    prose.setLineHeight(m_lineHeight, QTextBlockFormat::ProportionalHeight);
    cursor.setBlockFormat(prose);

    cursor.beginEditBlock();
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const BlockKind kind = blockKind(block);
        if (kind != BlockKind::Prose && !hasWantedTypography(block, kind))
            applyBlockTypography(cursor, block, kind);
    }
    cursor.endEditBlock();
    m_formattingTypography = false;

    m_document->setUndoRedoEnabled(undoEnabled);

    m_formattedBlockCount = m_document->blockCount();
    updateTableGrids();
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);

    QList<QTextBlock> touched;
    const QTextBlock lastChanged = m_document->findBlock(end);
    for (QTextBlock block = m_document->findBlock(start); block.isValid();
            block = block.next()) {
        touched.append(block);
        if (block == lastChanged)
            break;
    }

    // Opening or closing a fence restates every line under it. The highlighter
    // is the only thing that knows which, so drain its list even when nothing
    // else needs doing.
    if (m_highlighter) {
        const QList<int> restated = m_highlighter->takeRestatedBlocks();
        for (int number : restated) {
            const QTextBlock block = m_document->findBlockByNumber(number);
            if (block.isValid())
                touched.append(block);
        }
    }

    QList<QPair<QTextBlock, BlockKind>> stale;
    for (const QTextBlock &block : std::as_const(touched)) {
        const BlockKind kind = blockKind(block);
        if (!hasWantedTypography(block, kind))
            stale.append({block, kind});
    }
    if (stale.isEmpty())
        return;

    m_formattingTypography = true;
    QTextCursor cursor(m_document);
    // Fold the formatting into the edit that caused it, so a single undo
    // reverts both the text and its line height.
    cursor.joinPreviousEditBlock();
    for (const auto &entry : std::as_const(stale))
        applyBlockTypography(cursor, entry.first, entry.second);
    cursor.endEditBlock();
    m_formattingTypography = false;
}
