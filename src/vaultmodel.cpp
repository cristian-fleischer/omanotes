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
    setSortMode(settings.value(sortModeSetting, QStringLiteral("name")).toString());
    setRoot(settings.value(rootSetting, defaultRoot()).toString());
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
    beginResetModel();
    applyFilter();
    endResetModel();
    emit countChanged();
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
    const QString cleaned = currentPath.isEmpty() ? currentPath
                                                  : QDir::cleanPath(currentPath);
    if (m_currentPath == cleaned)
        return;

    m_currentPath = cleaned;
    emit currentPathChanged();
    if (!m_filtered.isEmpty()) {
        emit dataChanged(index(0), index(int(m_filtered.size()) - 1),
                         QList<int>{IsCurrentRole});
    }
}

int VaultModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_filtered.size());
}

QVariant VaultModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filtered.size())
        return {};

    const Entry &entry = m_entries.at(m_filtered.at(index.row()));
    switch (role) {
    case TitleRole:
        return entry.title;
    case RelativeDirRole:
        return entry.relativeDir;
    case PathRole:
        return entry.path;
    case ModifiedRole:
        return entry.modified;
    case IsCurrentRole:
        return !m_currentPath.isEmpty() && entry.path == m_currentPath;
    default:
        return {};
    }
}

QHash<int, QByteArray> VaultModel::roleNames() const {
    return {{TitleRole, "title"},
            {RelativeDirRole, "relativeDir"},
            {PathRole, "path"},
            {ModifiedRole, "modified"},
            {IsCurrentRole, "isCurrent"}};
}

QUrl VaultModel::urlAt(int row) const {
    const QString path = pathAt(row);
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

QString VaultModel::pathAt(int row) const {
    if (row < 0 || row >= m_filtered.size())
        return {};
    return m_entries.at(m_filtered.at(row)).path;
}

int VaultModel::rowForPath(const QString &path) const {
    if (path.isEmpty())
        return -1;
    const QString cleaned = QDir::cleanPath(path);
    for (qsizetype row = 0; row < m_filtered.size(); ++row) {
        if (m_entries.at(m_filtered.at(row)).path == cleaned)
            return int(row);
    }
    return -1;
}

void VaultModel::refresh() {
    m_rescanTimer.stop();
    beginResetModel();
    scan();
    applyFilter();
    endResetModel();
    rewatch();
    emit countChanged();
}

void VaultModel::scan() {
    m_entries.clear();
    m_scannedDirectories.clear();
    m_truncated = false;

    if (m_root.isEmpty())
        return;

    const QString canonicalRoot = QDir(m_root).canonicalPath();
    if (canonicalRoot.isEmpty())
        return;

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

void VaultModel::applyFilter() {
    m_filtered.clear();
    m_filtered.reserve(m_entries.size());
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_filter.isEmpty()
                || m_entries.at(i).relativePath.contains(m_filter, Qt::CaseInsensitive))
            m_filtered.append(int(i));
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
