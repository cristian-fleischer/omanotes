#include "vaultmodel.h"

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
        return !node.directory && !m_currentPath.isEmpty() && node.path == m_currentPath;
    case DepthRole:
        return node.depth;
    case IsDirectoryRole:
        return node.directory;
    case IsExpandedRole:
        return node.directory && !m_collapsedFolders.contains(node.relativePath);
    case HasDraftRole:
        return !node.directory && m_draftPaths.contains(node.path);
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
            {HasDraftRole, "hasDraft"}};
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
    beginResetModel();
    scan();
    rebuildRows();
    endResetModel();
    rewatch();
    emit countChanged();
}

void VaultModel::resetRows() {
    beginResetModel();
    rebuildRows();
    endResetModel();
    emit countChanged();
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
            "OmaNote: %1 holds more than %2 Markdown files; the list stops there.")
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

    // A filter flattens the tree. What matters while typing is finding the
    // note, not where in the folders it lives, so matches are listed with their
    // parent directory beside them instead of nested under it.
    if (!m_filter.isEmpty()) {
        for (const Entry &entry : m_entries) {
            if (!entry.relativePath.contains(m_filter, Qt::CaseInsensitive))
                continue;
            m_rows.append(Node{entry.title, entry.relativeDir, entry.relativePath,
                               entry.path, entry.modified, 0, false});
            ++m_visibleNotes;
        }
        return;
    }

    // Group the scanned files by their directory, and record every directory
    // under its own parent. Only folders that hold a note somewhere below them
    // ever appear.
    QHash<QString, QList<int>> files;
    QHash<QString, QStringList> subdirectories;
    QSet<QString> known;
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString directory = m_entries.at(i).relativeDir;
        files[directory].append(i);
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
                           entry.path, entry.modified, depth, false});
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

QString VaultModel::createNote() {
    if (m_root.isEmpty())
        return {};

    QDir directory(m_root);
    if (!directory.exists() && !QDir().mkpath(m_root))
        return {};

    QString name = QStringLiteral("untitled.md");
    for (int suffix = 2; directory.exists(name); ++suffix)
        name = QStringLiteral("untitled-%1.md").arg(suffix);

    const QString path = directory.filePath(name);
    QFile file(path);
    // NewOnly fails rather than truncating, so a note created between the
    // check above and here survives.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return {};
    file.close();

    refresh();
    return QDir::cleanPath(path);
}
