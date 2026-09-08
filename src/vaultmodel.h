#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
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
    int count() const { return int(m_filtered.size()); }
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
    struct Entry {
        QString title;
        QString relativeDir;
        QString relativePath;
        QString path;
        QDateTime modified;
    };

    void scan();
    void applyFilter();
    void rewatch();

    QString m_root;
    QString m_filter;
    QString m_sortMode = QStringLiteral("name");
    QString m_currentPath;
    bool m_truncated = false;
    QList<Entry> m_entries;
    QList<int> m_filtered;
    QStringList m_scannedDirectories;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
};
