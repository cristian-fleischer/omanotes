#include "vaultmodel.h"
#include "backend.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString rootSetting = QStringLiteral("vault/root");
const QString sortModeSetting = QStringLiteral("vault/sortMode");
const QString collapsedFoldersSetting = QStringLiteral("vault/collapsedFolders");

bool isMarkdown(const QString &fileName) {
    return fileName.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
        || fileName.endsWith(QStringLiteral(".markdown"), Qt::CaseInsensitive);
}

bool isInside(const QString &path, const QString &root) {
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

} // namespace

VaultModel::VaultModel(QObject *parent) : QAbstractListModel(parent) {
    // A `git checkout` in the vault fires one directoryChanged per touched
    // directory. Collapse the burst into a single rescan.
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(200);
    connect(&m_rescanTimer, &QTimer::timeout, this, &VaultModel::refresh);

    // Searching the text of every note costs a process, so wait until the
    // typing stops. The path filter is instant and carries the interim.
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(220);
    connect(&m_searchTimer, &QTimer::timeout, this, &VaultModel::startContentSearch);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { m_rescanTimer.start(); });
}

QString VaultModel::defaultRoot() {
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(home).filePath(QStringLiteral("Notes"));
}

void VaultModel::loadSettings() {
    QSettings settings;
    const QStringList collapsed = settings.value(collapsedFoldersSetting).toStringList();
    m_collapsedFolders = QSet<QString>(collapsed.begin(), collapsed.end());
    setSortMode(settings.value(sortModeSetting, QStringLiteral("name")).toString());
    setRoot(settings.value(rootSetting, defaultRoot()).toString());
}

void VaultModel::saveCollapsedFolders() {
    QStringList folders(m_collapsedFolders.begin(), m_collapsedFolders.end());
    folders.sort();
    QSettings().setValue(collapsedFoldersSetting, folders);
}

void VaultModel::setRoot(const QString &root) {
    const QString cleaned = root.isEmpty() ? root : QDir::cleanPath(root);
    if (m_root == cleaned)
        return;

    m_root = cleaned;
    QSettings().setValue(rootSetting, m_root);
    emit rootChanged();
    refresh();
}

void VaultModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;

    m_filter = filter;
    emit filterChanged();

    m_contentMatches.clear();
    if (m_search) {
        m_search->kill();
        m_search->deleteLater();
        m_search = nullptr;
    }
    // One character matches most of a vault, and the path filter already covers
    // it. Below that a text search is only noise and processes.
    if (m_filter.size() >= 2)
        m_searchTimer.start();
    else
        m_searchTimer.stop();

    if (m_searchRunning) {
        m_searchRunning = false;
        emit searchingChanged();
    }

    resetRows();
}

// ripgrep when it is installed, grep otherwise, and nothing at all if neither
// is. The pattern is passed as an argument and as a fixed string, so no shell
// sees it and nothing in a note's text is treated as a pattern.
void VaultModel::startContentSearch() {
    if (m_root.isEmpty() || m_filter.size() < 2)
        return;

    static const QString ripgrep = QStandardPaths::findExecutable(QStringLiteral("rg"));
    static const QString grep = QStandardPaths::findExecutable(QStringLiteral("grep"));

    QString program;
    QStringList arguments;
    if (!ripgrep.isEmpty()) {
        program = ripgrep;
        arguments = QStringList{QStringLiteral("--files-with-matches"),
                                QStringLiteral("--fixed-strings"),
                                QStringLiteral("--ignore-case"),
                                QStringLiteral("--no-messages"),
                                QStringLiteral("--glob=*.md"),
                                QStringLiteral("--glob=*.markdown"),
                                QStringLiteral("--"),
                                m_filter,
                                m_root};
    } else if (!grep.isEmpty()) {
        program = grep;
        arguments = QStringList{QStringLiteral("-r"), QStringLiteral("-i"),
                                QStringLiteral("-l"), QStringLiteral("-F"),
                                QStringLiteral("--include=*.md"),
                                QStringLiteral("--include=*.markdown"),
                                QStringLiteral("-e"), m_filter,
                                QStringLiteral("--"), m_root};
    } else {
        return;
    }

    m_search = new QProcess(this);
    m_search->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_search, &QProcess::finished, this, &VaultModel::collectContentMatches);
    m_search->start(program, arguments);

    m_searchRunning = true;
    emit searchingChanged();
}

void VaultModel::collectContentMatches() {
    if (!m_search)
        return;

    const QList<QByteArray> lines = m_search->readAll().split('\n');
    m_search->deleteLater();
    m_search = nullptr;
    m_searchRunning = false;
    emit searchingChanged();

    QSet<QString> matches;
    for (const QByteArray &line : lines) {
        const QString path = QString::fromUtf8(line).trimmed();
        if (path.isEmpty())
            continue;
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty())
            matches.insert(canonical);
    }
    if (matches == m_contentMatches)
        return;

    m_contentMatches = matches;
    resetRows();
}

void VaultModel::setSortMode(const QString &sortMode) {
    const QString mode = sortMode == QStringLiteral("modified") ? sortMode
                                                                : QStringLiteral("name");
    if (m_sortMode == mode)
        return;

    m_sortMode = mode;
    QSettings().setValue(sortModeSetting, m_sortMode);
    emit sortModeChanged();
    refresh();
}

void VaultModel::setCurrentPath(const QString &currentPath) {
    // Entries hold canonical paths. A file opened through the portal dialog can
    // arrive by a different route to the same note, so resolve before comparing.
    QString cleaned;
    if (!currentPath.isEmpty()) {
        cleaned = QFileInfo(currentPath).canonicalFilePath();
        if (cleaned.isEmpty())
            cleaned = QDir::cleanPath(currentPath);
    }
    if (m_currentPath == cleaned)
        return;

    m_currentPath = cleaned;
    emit currentPathChanged();

    // A note inside a collapsed folder cannot be highlighted, so open the way
    // down to it before saying anything changed.
    if (!m_canonicalRoot.isEmpty() && cleaned.startsWith(m_canonicalRoot + QLatin1Char('/'))
            && expandAncestorsOf(QFileInfo(QDir(m_canonicalRoot)
                                               .relativeFilePath(cleaned)).path())) {
        resetRows();
        return;
    }

    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), QList<int>{IsCurrentRole});
}

bool VaultModel::expandAncestorsOf(const QString &relativeDir) {
    if (relativeDir.isEmpty() || relativeDir == QStringLiteral("."))
        return false;

    bool changed = false;
    QString path = relativeDir;
    while (!path.isEmpty() && path != QStringLiteral(".")) {
        if (m_collapsedFolders.remove(path))
            changed = true;
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        path = slash < 0 ? QString() : path.left(slash);
    }
    if (changed)
        saveCollapsedFolders();
    return changed;
}

int VaultModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant VaultModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Node &node = m_rows.at(index.row());
    switch (role) {
    case TitleRole:
        return node.title;
    case RelativeDirRole:
        return node.relativeDir;
    case PathRole:
        return node.path;
    case ModifiedRole:
        return node.modified;
    case IsCurrentRole:
        return !node.directory && !node.header && !m_currentPath.isEmpty()
            && node.path == m_currentPath;
    case DepthRole:
        return node.depth;
    case IsDirectoryRole:
        return node.directory;
    case IsExpandedRole:
        return node.directory && !m_collapsedFolders.contains(node.relativePath);
    case HasDraftRole:
        return !node.directory && m_draftPaths.contains(node.path);
    case MatchesContentRole:
        return !node.directory && !m_filter.isEmpty()
            && !node.relativePath.contains(m_filter, Qt::CaseInsensitive)
            && m_contentMatches.contains(node.path);
    case IsDraftRole:
        return node.draft;
    case IsHeaderRole:
        return node.header;
    default:
        return {};
    }
}

QHash<int, QByteArray> VaultModel::roleNames() const {
    return {{TitleRole, "title"},
            {RelativeDirRole, "relativeDir"},
            {PathRole, "path"},
            {ModifiedRole, "modified"},
            {IsCurrentRole, "isCurrent"},
            {DepthRole, "depth"},
            {IsDirectoryRole, "isDirectory"},
            {IsExpandedRole, "isExpanded"},
            {HasDraftRole, "hasDraft"},
            {MatchesContentRole, "matchesContent"},
            {IsDraftRole, "isDraft"},
            {IsHeaderRole, "isHeader"}};
}

QUrl VaultModel::urlAt(int row) const {
    if (isDirectoryAt(row))
        return {};
    const QString path = pathAt(row);
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

QUrl VaultModel::urlForPath(const QString &path) const {
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

void VaultModel::setRootUrl(const QUrl &url) {
    if (url.isLocalFile())
        setRoot(url.toLocalFile());
}

void VaultModel::setDraftPaths(const QStringList &paths) {
    QSet<QString> resolved;
    for (const QString &path : paths) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        resolved.insert(canonical.isEmpty() ? QDir::cleanPath(path) : canonical);
    }
    if (resolved == m_draftPaths)
        return;

    m_draftPaths = resolved;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), QList<int>{HasDraftRole});
}

void VaultModel::setCurrentUrl(const QUrl &url) {
    setCurrentPath(url.isLocalFile() ? url.toLocalFile() : QString());
}

QString VaultModel::pathAt(int row) const {
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).path;
}

bool VaultModel::isDirectoryAt(int row) const {
    return row >= 0 && row < m_rows.size() && m_rows.at(row).directory;
}

// The folder row this row sits under, so Left can walk out of a subtree.
int VaultModel::rowForParentOf(int row) const {
    if (row < 0 || row >= m_rows.size())
        return -1;
    const QString parent = m_rows.at(row).relativeDir;
    if (parent.isEmpty())
        return -1;
    for (int candidate = row - 1; candidate >= 0; --candidate) {
        if (m_rows.at(candidate).directory && m_rows.at(candidate).relativePath == parent)
            return candidate;
    }
    return -1;
}

void VaultModel::toggleExpanded(int row) {
    if (!isDirectoryAt(row))
        return;
    setExpanded(row, m_collapsedFolders.contains(m_rows.at(row).relativePath));
}

void VaultModel::setExpanded(int row, bool expanded) {
    if (!isDirectoryAt(row))
        return;

    const QString folder = m_rows.at(row).relativePath;
    if (expanded == !m_collapsedFolders.contains(folder))
        return;

    if (expanded)
        m_collapsedFolders.remove(folder);
    else
        m_collapsedFolders.insert(folder);

    saveCollapsedFolders();
    resetRows();
}

int VaultModel::rowForPath(const QString &path) const {
    if (path.isEmpty())
        return -1;
    const QString cleaned = QDir::cleanPath(path);
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (!m_rows.at(row).directory && m_rows.at(row).path == cleaned)
            return int(row);
    }
    return -1;
}

void VaultModel::refresh() {
    m_rescanTimer.stop();
    scan();
    applyRows();
    rewatch();
}

void VaultModel::resetRows() {
    applyRows();
}

// Opening one folder used to reset the model, which throws every delegate away
// and redraws every folder icon in the list. Work out what actually moved and
// say only that: expanding or collapsing is one run of rows appearing or
// disappearing directly under the row you clicked.
void VaultModel::applyRows() {
    const QList<Node> previous = m_rows;
    rebuildRows();
    QList<Node> updated = m_rows;

    const auto same = [](const Node &a, const Node &b) {
        return a.directory == b.directory && a.header == b.header
            && a.draft == b.draft && a.depth == b.depth
            && a.relativePath == b.relativePath && a.title == b.title;
    };

    int prefix = 0;
    while (prefix < previous.size() && prefix < updated.size()
           && same(previous.at(prefix), updated.at(prefix)))
        ++prefix;

    int suffix = 0;
    while (suffix < previous.size() - prefix && suffix < updated.size() - prefix
           && same(previous.at(previous.size() - 1 - suffix),
                   updated.at(updated.size() - 1 - suffix)))
        ++suffix;

    const int removed = previous.size() - prefix - suffix;
    const int inserted = updated.size() - prefix - suffix;

    if (removed > 0 && inserted > 0) {
        // Rows changed rather than only appearing or disappearing, which a
        // rescan can do. Nothing to be gained from a diff there.
        beginResetModel();
        endResetModel();
    } else if (inserted > 0) {
        m_rows = previous;
        beginInsertRows(QModelIndex(), prefix, prefix + inserted - 1);
        m_rows = updated;
        endInsertRows();
    } else if (removed > 0) {
        m_rows = previous;
        beginRemoveRows(QModelIndex(), prefix, prefix + removed - 1);
        m_rows = updated;
        endRemoveRows();
    }

    // A folder that stayed put still opened or closed, and a note that stayed
    // put may have gained or lost its dot.
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0), index(int(m_rows.size()) - 1),
                         QList<int>{IsExpandedRole, IsCurrentRole, HasDraftRole,
                                    TitleRole, MatchesContentRole});
    }
    emit countChanged();
}

namespace {
constexpr auto draftsFolderName = "drafts";
constexpr auto stateFolderName = ".omanotes";
}

QString VaultModel::draftsRelativePath(const QString &relativeDir) {
    const QString tail = QStringLiteral("%1/%2").arg(QLatin1String(stateFolderName),
                                                     QLatin1String(draftsFolderName));
    return relativeDir.isEmpty() ? tail : relativeDir + QLatin1Char('/') + tail;
}

QString VaultModel::draftsDirectoryFor(const QString &folder) {
    return QDir(folder).filePath(QStringLiteral("%1/%2")
                                     .arg(QLatin1String(stateFolderName),
                                          QLatin1String(draftsFolderName)));
}

bool VaultModel::isDraftPath(const QString &path) {
    return !folderForDraft(path).isEmpty();
}

QString VaultModel::folderForDraft(const QString &path) {
    const QDir drafts = QFileInfo(path).dir();
    if (drafts.dirName() != QLatin1String(draftsFolderName))
        return {};
    QDir state = drafts;
    if (!state.cdUp() || state.dirName() != QLatin1String(stateFolderName))
        return {};
    QDir owner = state;
    if (!owner.cdUp())
        return {};
    return owner.absolutePath();
}

QString VaultModel::firstLineOf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // A title comes off the first line with anything on it, so the head of the
    // file is all this needs to see.
    const QString head = QString::fromUtf8(file.read(4096));
    for (const QStringView line : QStringView(head).split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            return line.toString();
    }
    return {};
}

// Drafts sit in a dot-directory, which the walk prunes, so they are collected
// on their own. There are only ever a handful, and reading a title costs one
// short read each.
void VaultModel::scanDrafts(const QString &directory) {
    const QString draftsPath = draftsDirectoryFor(directory);
    QDir drafts(draftsPath);
    if (!drafts.exists())
        return;

    const QDir rootDir(m_canonicalRoot);
    const QFileInfoList found =
        drafts.entryInfoList(QStringList{QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : found) {
        Entry entry;
        entry.path = info.absoluteFilePath();
        entry.draft = true;
        entry.title = Backend::titleFromText(firstLineOf(entry.path));
        if (entry.title.isEmpty())
            entry.title = QStringLiteral("Draft");
        entry.relativePath = rootDir.relativeFilePath(entry.path);
        entry.relativeDir = rootDir.relativeFilePath(directory);
        if (entry.relativeDir == QStringLiteral("."))
            entry.relativeDir.clear();
        entry.modified = info.lastModified();
        m_entries.append(entry);
    }
}

void VaultModel::scan() {
    m_entries.clear();
    m_scannedDirectories.clear();
    m_canonicalRoot.clear();
    m_truncated = false;

    if (m_root.isEmpty())
        return;

    const QString canonicalRoot = QDir(m_root).canonicalPath();
    if (canonicalRoot.isEmpty())
        return;
    m_canonicalRoot = canonicalRoot;

    const QDir rootDir(canonicalRoot);
    QStringList queue{canonicalRoot};
    QSet<QString> visited{canonicalRoot};

    while (!queue.isEmpty()) {
        const QString directory = queue.takeFirst();
        m_scannedDirectories.append(directory);
        scanDrafts(directory);

        // QDirIterator::Subdirectories cannot prune a subtree, and a vault is
        // full of subtrees worth pruning: .git alone would swallow the file
        // cap. Walk one directory at a time and decide before descending.
        QDirIterator it(directory, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.fileName().startsWith(QLatin1Char('.')))
                continue;

            // Resolves symlinks and returns empty for a broken one. Anything
            // that lands outside the root is not part of this vault.
            const QString canonical = info.canonicalFilePath();
            if (canonical.isEmpty() || !isInside(canonical, canonicalRoot))
                continue;

            if (info.isDir()) {
                if (!visited.contains(canonical)) {
                    visited.insert(canonical);
                    queue.append(canonical);
                }
                continue;
            }

            if (!isMarkdown(info.fileName()))
                continue;

            if (m_entries.size() >= maximumFiles) {
                m_truncated = true;
                queue.clear();
                break;
            }

            Entry entry;
            entry.path = canonical;
            entry.title = info.completeBaseName();
            entry.relativePath = rootDir.relativeFilePath(canonical);
            entry.relativeDir = QFileInfo(entry.relativePath).path();
            if (entry.relativeDir == QStringLiteral("."))
                entry.relativeDir.clear();
            entry.modified = info.lastModified();
            m_entries.append(entry);
        }
    }

    if (m_truncated) {
        qWarning().noquote() << QStringLiteral(
            "Omanotes: %1 holds more than %2 Markdown files; the list stops there.")
            .arg(m_root).arg(maximumFiles);
    }

    if (m_sortMode == QStringLiteral("modified")) {
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
            if (a.modified != b.modified)
                return a.modified > b.modified;
            return a.relativePath.compare(b.relativePath, Qt::CaseInsensitive) < 0;
        });
    } else {
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
            const int byTitle = a.title.compare(b.title, Qt::CaseInsensitive);
            if (byTitle != 0)
                return byTitle < 0;
            return a.relativePath.compare(b.relativePath, Qt::CaseInsensitive) < 0;
        });
    }
}

void VaultModel::rebuildRows() {
    m_rows.clear();
    m_visibleNotes = 0;

    const auto appendNote = [this](const Entry &entry, int depth) {
        m_rows.append(Node{entry.title, entry.relativeDir, entry.relativePath,
                           entry.path, entry.modified, depth, false, entry.draft, false});
        ++m_visibleNotes;
    };
    const auto appendHeader = [this](const QString &label) {
        Node node;
        node.title = label;
        node.header = true;
        m_rows.append(node);
    };

    // A filter flattens the tree. What matters while typing is finding the
    // note, not where in the folders it lives, so matches are listed with their
    // parent directory beside them instead of nested under it.
    if (!m_filter.isEmpty()) {
        // Name matches first, then what ripgrep found inside the notes. A name
        // is the stronger answer, and mixing the two buries it.
        QList<const Entry *> byName;
        QList<const Entry *> byContent;
        for (const Entry &entry : m_entries) {
            if (entry.relativePath.contains(m_filter, Qt::CaseInsensitive)
                    || (entry.draft && entry.title.contains(m_filter, Qt::CaseInsensitive)))
                byName.append(&entry);
            else if (m_contentMatches.contains(entry.path))
                byContent.append(&entry);
        }
        for (const Entry *entry : std::as_const(byName))
            appendNote(*entry, 0);
        if (!byContent.isEmpty()) {
            appendHeader(QStringLiteral("Found in text"));
            for (const Entry *entry : std::as_const(byContent))
                appendNote(*entry, 0);
        }
        return;
    }


    // Group the scanned files by their directory, and record every directory
    // under its own parent. Only folders that hold a note somewhere below them
    // ever appear.
    QHash<QString, QList<int>> files;
    QHash<QString, QStringList> subdirectories;
    QSet<QString> known;
    m_draftsByDirectory.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries.at(i);
        const QString directory = entry.relativeDir;
        if (entry.draft)
            m_draftsByDirectory[directory].append(i);
        else
            files[directory].append(i);
        // A folder holding nothing but drafts is still a folder worth showing,
        // or the draft you just made would have nowhere to appear.
        QString path = directory;
        while (!path.isEmpty() && !known.contains(path)) {
            known.insert(path);
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            const QString parent = slash < 0 ? QString() : path.left(slash);
            subdirectories[parent].append(path);
            path = parent;
        }
    }

    for (auto it = subdirectories.begin(); it != subdirectories.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const QString &a, const QString &b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
    }

    appendDirectory(QString(), 0, files, subdirectories);
}

// Folders first, then the notes that sit directly in this one, both already in
// the order the sort mode asked for.
void VaultModel::appendDirectory(const QString &relativeDir, int depth,
                                 const QHash<QString, QList<int>> &files,
                                 const QHash<QString, QStringList> &subdirectories) {
    // Drafts belong to the folder they were made in, above its folders and
    // notes. The group opens and closes like any other folder, and its key is
    // the real path of the directory holding them, so a closed one stays closed.
    const QList<int> drafts = m_draftsByDirectory.value(relativeDir);
    if (!drafts.isEmpty()) {
        const QString key = draftsRelativePath(relativeDir);
        Node group;
        group.title = QStringLiteral("Drafts");
        group.relativeDir = relativeDir;
        group.relativePath = key;
        group.path = QDir(m_canonicalRoot).filePath(key);
        group.depth = depth;
        group.directory = true;
        group.draftsGroup = true;
        m_rows.append(group);

        if (!m_collapsedFolders.contains(key)) {
            for (int index : drafts) {
                const Entry &entry = m_entries.at(index);
                Node node{entry.title, entry.relativeDir, entry.relativePath,
                          entry.path, entry.modified, depth + 1, false, true, false};
                m_rows.append(node);
                ++m_visibleNotes;
            }
        }
    }

    const QStringList children = subdirectories.value(relativeDir);
    for (const QString &child : children) {
        Node node;
        node.title = child.mid(child.lastIndexOf(QLatin1Char('/')) + 1);
        node.relativeDir = relativeDir;
        node.relativePath = child;
        node.path = QDir(m_canonicalRoot).filePath(child);
        node.depth = depth;
        node.directory = true;
        m_rows.append(node);
        if (!m_collapsedFolders.contains(child))
            appendDirectory(child, depth + 1, files, subdirectories);
    }

    for (int index : files.value(relativeDir)) {
        const Entry &entry = m_entries.at(index);
        m_rows.append(Node{entry.title, entry.relativeDir, entry.relativePath,
                           entry.path, entry.modified, depth, false, false, false});
        ++m_visibleNotes;
    }
}

void VaultModel::rewatch() {
    // Directories only. They report creation, deletion and rename of their
    // children, which is everything the list needs, at one descriptor per
    // directory instead of one per note. Deleted directories drop out of the
    // watcher on their own, so the set is rebuilt after every scan.
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (!m_scannedDirectories.isEmpty())
        m_watcher.addPaths(m_scannedDirectories);
}

QString VaultModel::createNote(const QString &relativeDir) {
    if (m_root.isEmpty())
        return {};

    const QString folder = relativeDir.isEmpty() ? m_root
                                                 : QDir(m_root).filePath(relativeDir);
    const QString draftsPath = draftsDirectoryFor(folder);
    if (!QDir().mkpath(draftsPath))
        return {};

    QDir drafts(draftsPath);
    QString name = QStringLiteral("draft.md");
    for (int suffix = 2; drafts.exists(name); ++suffix)
        name = QStringLiteral("draft-%1.md").arg(suffix);

    const QString path = drafts.filePath(name);
    QFile file(path);
    // NewOnly fails rather than truncating, so a note created between the
    // check above and here survives.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return {};
    file.close();

    refresh();
    return QDir::cleanPath(path);
}

QStringList VaultModel::folders() const {
    QSet<QString> seen;
    for (const Entry &entry : m_entries) {
        if (entry.draft)
            continue;
        QString path = entry.relativeDir;
        while (!path.isEmpty() && !seen.contains(path)) {
            seen.insert(path);
            const int slash = path.lastIndexOf(QLatin1Char('/'));
            path = slash < 0 ? QString() : path.left(slash);
        }
    }
    QStringList all(seen.cbegin(), seen.cend());
    std::sort(all.begin(), all.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return all;
}

QString VaultModel::relativeDirAt(int row) const {
    if (row < 0 || row >= m_rows.size())
        return {};
    const Node &node = m_rows.at(row);
    if (node.draftsGroup)
        return node.relativeDir;
    return node.directory ? node.relativePath : node.relativeDir;
}

bool VaultModel::isHeaderAt(int row) const {
    return row >= 0 && row < m_rows.size() && m_rows.at(row).header;
}

bool VaultModel::hasDraftAt(int row) const {
    return row >= 0 && row < m_rows.size() && !m_rows.at(row).directory
        && m_draftPaths.contains(m_rows.at(row).path);
}

QString VaultModel::titleAt(int row) const {
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).title : QString();
}

bool VaultModel::canDragAt(int row) const {
    if (row < 0 || row >= m_rows.size())
        return false;
    const Node &node = m_rows.at(row);
    // A draft has no name of its own yet, so filing it in the vault would put
    // an unnamed note among the named ones. Saving it is what files it.
    return !node.directory && !node.header && !node.draft;
}

QString VaultModel::dropFolderForRow(int targetRow) const {
    if (targetRow < 0)
        return {};
    if (targetRow >= m_rows.size())
        return {};
    const Node &node = m_rows.at(targetRow);
    return node.directory ? node.relativePath : node.relativeDir;
}

bool VaultModel::canDropOnRow(int sourceRow, int targetRow) const {
    if (!canDragAt(sourceRow))
        return false;
    // Below the last row is the vault root.
    if (targetRow >= m_rows.size())
        return false;
    if (targetRow >= 0) {
        const Node &target = m_rows.at(targetRow);
        // A Drafts row stands for a hidden directory, not a folder you can
        // file a note into, and a label is not a target at all.
        if (target.header || target.draftsGroup)
            return false;
    }
    // Where it already is is not a move.
    return dropFolderForRow(targetRow) != m_rows.at(sourceRow).relativeDir;
}

QString VaultModel::relativeDirForUrl(const QUrl &url) const {
    if (m_canonicalRoot.isEmpty() || !url.isLocalFile())
        return {};

    const QString path = QFileInfo(url.toLocalFile()).canonicalFilePath();
    if (path.isEmpty() || !isInside(path, m_canonicalRoot))
        return {};

    const QString owner = folderForDraft(path);
    const QString folder = owner.isEmpty() ? QFileInfo(path).dir().canonicalPath() : owner;
    const QString relative = QDir(m_canonicalRoot).relativeFilePath(folder);
    return relative == QStringLiteral(".") ? QString() : relative;
}

QString VaultModel::moveNote(const QString &path, const QString &relativeDir) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (m_canonicalRoot.isEmpty() || canonical.isEmpty()
            || !isInside(canonical, m_canonicalRoot))
        return {};

    const QString folder = relativeDir.isEmpty()
        ? m_canonicalRoot : QDir(m_canonicalRoot).filePath(relativeDir);
    if (!QDir().mkpath(folder))
        return {};

    const QFileInfo info(canonical);
    if (QDir(folder).canonicalPath() == info.dir().canonicalPath())
        return canonical;

    QDir target(folder);
    const QString stem = info.completeBaseName();
    QString name = info.fileName();
    for (int suffix = 2; target.exists(name); ++suffix)
        name = QStringLiteral("%1-%2.md").arg(stem).arg(suffix);

    const QString destination = target.filePath(name);
    if (!QFile::rename(canonical, destination))
        return {};

    refresh();
    return QDir::cleanPath(destination);
}

bool VaultModel::deleteNote(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (m_canonicalRoot.isEmpty() || canonical.isEmpty()
            || !isInside(canonical, m_canonicalRoot))
        return false;

    QFile file(canonical);
    // The desktop trash where there is one, so a note deleted by mistake can
    // be put back. remove() only when there is no trash to move it to.
    const bool gone = file.moveToTrash() || file.remove();
    if (gone)
        refresh();
    return gone;
}
