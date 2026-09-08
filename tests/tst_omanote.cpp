#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "markdownhighlighter.h"
#include "vaultmodel.h"

class OmanoteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        // Keeps recovery snapshots out of the real ~/.local/share/omanote.
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void vaultModelLists() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        QVERIFY(writeNote(vault.path(), QStringLiteral("Alpha.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("zeta.markdown")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/Beta.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/deep/Gamma.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("notes.txt")));
        QVERIFY(writeNote(vault.path(), QStringLiteral(".hidden.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral(".git/objects/pack.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral(".obsidian/workspace.md")));

        VaultModel model;
        model.setRoot(vault.path());

        QCOMPARE(model.count(), 4);
        QVERIFY(!model.truncated());
        QCOMPARE(titlesOf(model),
                 (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta"),
                              QStringLiteral("Gamma"), QStringLiteral("zeta")}));

        // Nesting shows as dim secondary text; a top-level note has none.
        QCOMPARE(roleOf(model, 0, VaultModel::RelativeDirRole).toString(), QString());
        QCOMPARE(roleOf(model, 1, VaultModel::RelativeDirRole).toString(),
                 QStringLiteral("projects"));
        QCOMPARE(roleOf(model, 2, VaultModel::RelativeDirRole).toString(),
                 QStringLiteral("projects/deep"));

        // A symlink out of the vault is not part of the vault.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        QVERIFY(writeNote(outside.path(), QStringLiteral("Elsewhere.md")));
        QVERIFY(QFile::link(outside.filePath(QStringLiteral("Elsewhere.md")),
                            QDir(vault.path()).filePath(QStringLiteral("Elsewhere.md"))));
        model.refresh();
        QCOMPARE(model.count(), 4);

        // The current file is the one the editor has open.
        model.setCurrentPath(roleOf(model, 1, VaultModel::PathRole).toString());
        QCOMPARE(roleOf(model, 0, VaultModel::IsCurrentRole).toBool(), false);
        QCOMPARE(roleOf(model, 1, VaultModel::IsCurrentRole).toBool(), true);
        QCOMPARE(model.rowForPath(model.pathAt(1)), 1);

        const QString created = model.createNote();
        QVERIFY(!created.isEmpty());
        QCOMPARE(QFileInfo(created).fileName(), QStringLiteral("untitled.md"));
        QCOMPARE(QFileInfo(model.createNote()).fileName(), QStringLiteral("untitled-2.md"));
        QCOMPARE(model.count(), 6);
    }

    void vaultModelHonoursTheFileCap() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        for (int i = 0; i < VaultModel::maximumFiles + 1; ++i)
            QVERIFY(writeNote(vault.path(), QStringLiteral("note-%1.md").arg(i, 5, 10, QLatin1Char('0'))));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.totalCount(), VaultModel::maximumFiles);
        QVERIFY(model.truncated());
    }

    void vaultModelFilter() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        QVERIFY(writeNote(vault.path(), QStringLiteral("Fusion reactor.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("Groceries.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("archive/2024/confusion.md")));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.count(), 3);

        // Case-insensitive substring, matched against the relative path, so a
        // directory name is as good a handle as a title.
        model.setFilter(QStringLiteral("fusion"));
        QCOMPARE(model.count(), 2);
        model.setFilter(QStringLiteral("FUSION R"));
        QCOMPARE(model.count(), 1);
        QCOMPARE(roleOf(model, 0, VaultModel::TitleRole).toString(),
                 QStringLiteral("Fusion reactor"));
        model.setFilter(QStringLiteral("archive/2024"));
        QCOMPARE(model.count(), 1);
        model.setFilter(QString());
        QCOMPARE(model.count(), 3);
    }

    void vaultModelWatch() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        QVERIFY(writeNote(vault.path(), QStringLiteral("first.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("nested/second.md")));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.count(), 2);

        QVERIFY(writeNote(vault.path(), QStringLiteral("nested/third.md")));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 3000);

        QVERIFY(QFile::remove(QDir(vault.path()).filePath(QStringLiteral("first.md"))));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 2, 3000);

        // A directory created after the first scan is watched too.
        QVERIFY(writeNote(vault.path(), QStringLiteral("later/fourth.md")));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 3, 3000);
        QVERIFY(writeNote(vault.path(), QStringLiteral("later/fifth.md")));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 3000);
    }

    void vaultSidebarOpensNotes() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        QVERIFY(writeNote(vaultDirectory.path(), QStringLiteral("Fusion reactor.md")));
        QVERIFY(writeNote(vaultDirectory.path(), QStringLiteral("Groceries.md")));
        QVERIFY(writeNote(vaultDirectory.path(), QStringLiteral("projects/Roadmap.md")));

        Backend backend;
        VaultModel vault;
        vault.setRoot(vaultDirectory.path());
        QCOMPARE(vault.count(), 3);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *sidebar = window->findChild<QObject *>(QStringLiteral("vaultSidebar"));
        QObject *list = window->findChild<QObject *>(QStringLiteral("vaultList"));
        QObject *filterField = window->findChild<QObject *>(QStringLiteral("vaultFilter"));
        QVERIFY(sidebar);
        QVERIFY(list);
        QVERIFY(filterField);
        QCOMPARE(list->property("count").toInt(), 3);

        // The filter field drives the model. QML keeps no second copy of the list.
        filterField->setProperty("text", QStringLiteral("fusion"));
        QCOMPARE(vault.filter(), QStringLiteral("fusion"));
        QCOMPARE(list->property("count").toInt(), 1);

        // Enter opens the selection, and it goes through Backend::open.
        QVERIFY(QMetaObject::invokeMethod(sidebar, "activateSelection"));
        QCOMPARE(backend.fileUrl(), vault.urlAt(0));
        QCOMPARE(QFileInfo(backend.fileUrl().toLocalFile()).fileName(),
                 QStringLiteral("Fusion reactor.md"));
        QCOMPARE(vault.currentPath(), backend.fileUrl().toLocalFile());

        filterField->setProperty("text", QString());
        QCOMPARE(list->property("count").toInt(), 3);

        // Closed until asked for, from the footer icon or Ctrl+L.
        QCOMPARE(window->property("sidebarVisible").toBool(), false);
        QVERIFY(window->findChild<QObject *>(QStringLiteral("sidebarButton")));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QCOMPARE(window->property("sidebarVisible").toBool(), true);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QCOMPARE(window->property("sidebarVisible").toBool(), false);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "createNote"));
        QCOMPARE(QFileInfo(backend.fileUrl().toLocalFile()).fileName(),
                 QStringLiteral("untitled.md"));
        QCOMPARE(list->property("count").toInt(), 4);

        // Sidebar width and visibility outlive the window.
        backend.saveSidebarState(false, 320);
        const QVariantMap state = backend.sidebarState();
        QCOMPARE(state.value(QStringLiteral("visible")).toBool(), false);
        QCOMPARE(state.value(QStringLiteral("width")).toInt(), 320);
    }

    void preservesLineEndingsAndByteOrderMark() {
        Backend::LineEnding ending = Backend::LineEnding::Lf;
        bool byteOrderMark = false;

        const QByteArray crlf("# Title\r\n\r\nBody\r\n");
        const QString decoded = Backend::decodeFileContents(crlf, &ending, &byteOrderMark);
        QCOMPARE(decoded, QStringLiteral("# Title\n\nBody\n"));
        QVERIFY(ending == Backend::LineEnding::CrLf);
        QVERIFY(!byteOrderMark);
        QCOMPARE(Backend::encodeFileContents(decoded, ending, byteOrderMark), crlf);

        const QByteArray withMark = QByteArray("\xef\xbb\xbf", 3) + QByteArray("plain\n");
        QCOMPARE(Backend::decodeFileContents(withMark, &ending, &byteOrderMark),
                 QStringLiteral("plain\n"));
        QVERIFY(ending == Backend::LineEnding::Lf);
        QVERIFY(byteOrderMark);
        QCOMPARE(Backend::encodeFileContents(QStringLiteral("plain\n"), ending, byteOrderMark),
                 withMark);

        // Mixed endings cannot survive a QTextDocument, which holds no carriage
        // returns at all. They collapse to LF, and stay LF on save.
        const QByteArray mixed("one\r\ntwo\nthree\n");
        QCOMPARE(Backend::decodeFileContents(mixed, &ending, &byteOrderMark),
                 QStringLiteral("one\ntwo\nthree\n"));
        QVERIFY(ending == Backend::LineEnding::Lf);
    }

    void byteFidelity_data() {
        QTest::addColumn<QString>("sourcePath");

        const QString corpus = corpusDirectory();
        QVERIFY(!corpus.isEmpty());
        const QFileInfoList files = QDir(corpus).entryInfoList(
            QStringList{QStringLiteral("*.md")}, QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());
        for (const QFileInfo &file : files)
            QTest::newRow(qPrintable(file.fileName())) << file.absoluteFilePath();
    }

    // The requirement that killed every alternative: opening a note, changing
    // your mind, and saving must leave the file byte for byte as it was.
    void byteFidelity() {
        QFETCH(QString, sourcePath);

        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray original = source.readAll();
        source.close();

        QTemporaryDir workingDirectory;
        QVERIFY(workingDirectory.isValid());
        const QString path = workingDirectory.filePath(QFileInfo(sourcePath).fileName());
        QVERIFY(QFile::copy(sourcePath, path));

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);

        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(path));

        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("draft "))));
        backend.editorTextChanged();
        QVERIFY(backend.modified());

        QVERIFY(QMetaObject::invokeMethod(editor.data(), "undo"));
        backend.editorTextChanged();

        backend.save();

        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray written = saved.readAll();
        QCOMPARE(written.size(), original.size());
        QCOMPARE(written, original);
    }

    void noMarkdownConversionApi() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());
        const QDir sourceDirectory = QFileInfo(mainQmlPath).absoluteDir();

        static const QRegularExpression conversionRe(QStringLiteral(
            "toMarkdown|setMarkdown|MarkdownText|TextEdit\\.RichText"));

        // Printing renders a throwaway QTextDocument through Qt's Markdown
        // writer. That is the only permitted call, and it never sees the
        // editing buffer.
        const QString printingException =
            QStringLiteral("rendered.setMarkdown(currentDocumentText());");

        const QFileInfoList sources = sourceDirectory.entryInfoList(QDir::Files, QDir::Name);
        QVERIFY(!sources.isEmpty());

        QStringList offenders;
        for (const QFileInfo &sourceFile : sources) {
            QFile file(sourceFile.absoluteFilePath());
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            int lineNumber = 0;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine());
                ++lineNumber;
                if (!conversionRe.match(line).hasMatch())
                    continue;
                if (sourceFile.fileName() == QStringLiteral("backend.cpp")
                        && line.trimmed() == printingException)
                    continue;
                offenders.append(QStringLiteral("%1:%2 %3")
                                     .arg(sourceFile.fileName())
                                     .arg(lineNumber)
                                     .arg(line.trimmed()));
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));

        QFile mainQml(mainQmlPath);
        QVERIFY(mainQml.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(mainQml.readAll())
                    .contains(QStringLiteral("textFormat: TextEdit.PlainText")));
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        VaultModel vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        VaultModel vault;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    // A bare plain-text TextEdit, so a Backend can be attached without a window.
    static QObject *createEditor(QQmlEngine *engine) {
        auto *component = new QQmlComponent(engine, engine);
        component->setData(R"QML(
            import QtQuick
            TextEdit { textFormat: TextEdit.PlainText }
        )QML", QUrl());
        if (!component->isReady()) {
            qWarning().noquote() << component->errorString();
            return nullptr;
        }
        return component->create();
    }

    static bool writeNote(const QString &root, const QString &relativePath) {
        const QString path = QDir(root).filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write("# ");
        file.write(QFileInfo(path).completeBaseName().toUtf8());
        file.write("\n");
        return true;
    }

    static QVariant roleOf(const VaultModel &model, int row, int role) {
        return model.data(model.index(row), role);
    }

    static QStringList titlesOf(const VaultModel &model) {
        QStringList titles;
        for (int row = 0; row < model.count(); ++row)
            titles.append(roleOf(model, row, VaultModel::TitleRole).toString());
        return titles;
    }

    static QString corpusDirectory() {
        const QString found = QFINDTESTDATA("corpus");
        if (!found.isEmpty())
            return found;
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        if (mainQmlPath.isEmpty())
            return {};
        return QFileInfo(mainQmlPath).absoluteDir().absoluteFilePath(
            QStringLiteral("../tests/corpus"));
    }

    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmanoteTest)
#include "tst_omanote.moc"
