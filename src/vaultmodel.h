#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

// The sidebar's view of one directory of Markdown notes. Full depth, dotfiles
// and dot-directories skipped, filtered in the model rather than in QML.
class VaultModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString root READ root WRITE setRoot NOTIFY rootChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY countChanged)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        RelativeDirRole,
        PathRole,
        ModifiedRole,
        IsCurrentRole,
        DepthRole,
        IsDirectoryRole,
        IsExpandedRole,
        HasDraftRole,
    };
    Q_ENUM(Role)

    // A notes vault is not a filesystem browser. Past this, stop and warn.
    static constexpr int maximumFiles = 5000;

    explicit VaultModel(QObject *parent = nullptr);

    QString root() const { return m_root; }
    void setRoot(const QString &root);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &sortMode);
    QString currentPath() const { return m_currentPath; }
    void setCurrentPath(const QString &currentPath);
    // Notes on screen, which is what the footer counts. Folder rows are
    // structure, not content, so they are not part of it.
    int count() const { return m_visibleNotes; }
    int totalCount() const { return int(m_entries.size()); }
    bool truncated() const { return m_truncated; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QUrl urlAt(int row) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int rowForPath(const QString &path) const;
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE bool isDirectoryAt(int row) const;
    Q_INVOKABLE void toggleExpanded(int row);
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    Q_INVOKABLE int rowForParentOf(int row) const;
    // QML has no QUrl::fromLocalFile, and string-splicing a file:// URL
    // loses every path that contains a space or a percent sign.
    Q_INVOKABLE QUrl urlForPath(const QString &path) const;
    Q_INVOKABLE void setRootUrl(const QUrl &url);
    Q_INVOKABLE void setCurrentUrl(const QUrl &url);
    // Absolute paths of notes holding text that is not on disk.
    Q_INVOKABLE void setDraftPaths(const QStringList &paths);

    // Reads vault/root and vault/sortMode. Called from main(), never from the
    // constructor: a test that builds a VaultModel must not scan the real vault.
    void loadSettings();
    static QString defaultRoot();

signals:
    void rootChanged();
    void filterChanged();
    void sortModeChanged();
    void currentPathChanged();
    void countChanged();

private:
    // One Markdown file found by the scan.
    struct Entry {
        QString title;
        QString relativeDir;
        QString relativePath;
        QString path;
        QDateTime modified;
    };

    // One visible row: a folder or a note, at a depth in the tree.
    struct Node {
        QString title;
        QString relativeDir;
        QString relativePath;
        QString path;
        QDateTime modified;
        int depth = 0;
        bool directory = false;
    };

    void scan();
    void rebuildRows();
    void appendDirectory(const QString &relativeDir, int depth,
                         const QHash<QString, QList<int>> &files,
                         const QHash<QString, QStringList> &subdirectories);
    void resetRows();
    bool expandAncestorsOf(const QString &relativeDir);
    void saveCollapsedFolders();
    void rewatch();

    QString m_root;
    QString m_filter;
    QString m_sortMode = QStringLiteral("name");
    QString m_currentPath;
    bool m_truncated = false;
    int m_visibleNotes = 0;
    QString m_canonicalRoot;
    QList<Entry> m_entries;
    QList<Node> m_rows;
    QSet<QString> m_collapsedFolders;
    QSet<QString> m_draftPaths;
    QStringList m_scannedDirectories;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
};
