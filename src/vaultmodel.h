#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QProcess>
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
    // True while a content search for the current filter is still running.
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

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
        MatchesContentRole,
        IsDraftRole,
        IsHeaderRole,
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
    bool searching() const { return m_searchRunning; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QUrl urlAt(int row) const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int rowForPath(const QString &path) const;
    // Creates a draft in `relativeDir`, or in the vault root when it is empty.
    Q_INVOKABLE QString createNote(const QString &relativeDir = QString());
    // Every folder in the vault, relative to the root, for the move menu.
    Q_INVOKABLE QStringList folders() const;
    // Moves a note into `relativeDir`. Returns its new path, or empty.
    Q_INVOKABLE QString moveNote(const QString &path, const QString &relativeDir);
    // To the desktop trash where there is one, so it can be put back.
    Q_INVOKABLE bool deleteNote(const QString &path);
    Q_INVOKABLE QString relativeDirAt(int row) const;
    Q_INVOKABLE bool isHeaderAt(int row) const;
    Q_INVOKABLE QString titleAt(int row) const;
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

    // A note with no name yet lives in `.omanotes/drafts` inside the folder it
    // belongs to, so the vault proper never holds a file called untitled.
    static QString draftsDirectoryFor(const QString &folder);
    static bool isDraftPath(const QString &path);
    // The folder a draft is promoted into on save: the one holding its
    // `.omanotes`. Empty when the path is not a draft.
    static QString folderForDraft(const QString &path);
    // The first line with anything on it, for labelling a draft. Reads the
    // head of the file, never the whole of it.
    static QString firstLineOf(const QString &path);

signals:
    void rootChanged();
    void filterChanged();
    void sortModeChanged();
    void currentPathChanged();
    void countChanged();
    void searchingChanged();

private:
    // One Markdown file found by the scan.
    struct Entry {
        QString title;
        QString relativeDir;
        QString relativePath;
        QString path;
        QDateTime modified;
        bool draft = false;
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
        bool draft = false;
        // A section label rather than anything you can open.
        bool header = false;
        // The "Drafts" row a folder gets when it holds any. It behaves like a
        // folder: it opens and closes, and it is not a note.
        bool draftsGroup = false;
    };

    void scan();
    void scanDrafts(const QString &directory);
    // Where a folder's drafts group sits, relative to the root. The real path
    // of its directory, so collapsing it persists like any other folder.
    static QString draftsRelativePath(const QString &relativeDir);
    void rebuildRows();
    void appendDirectory(const QString &relativeDir, int depth,
                         const QHash<QString, QList<int>> &files,
                         const QHash<QString, QStringList> &subdirectories);
    void resetRows();
    bool expandAncestorsOf(const QString &relativeDir);
    void saveCollapsedFolders();
    void rewatch();
    void startContentSearch();
    void collectContentMatches();

    QString m_root;
    QString m_filter;
    QString m_sortMode = QStringLiteral("name");
    QString m_currentPath;
    bool m_truncated = false;
    int m_visibleNotes = 0;
    QString m_canonicalRoot;
    QList<Entry> m_entries;
    QList<Node> m_rows;
    // Draft entries by the folder they belong to, for the group rows.
    QHash<QString, QList<int>> m_draftsByDirectory;
    QSet<QString> m_collapsedFolders;
    QSet<QString> m_draftPaths;
    // Notes whose text matches the filter, found by ripgrep or grep. The
    // filter matches a path on its own; this adds what is written inside.
    QSet<QString> m_contentMatches;
    QProcess *m_search = nullptr;
    QTimer m_searchTimer;
    bool m_searchRunning = false;
    QStringList m_scannedDirectories;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
};
