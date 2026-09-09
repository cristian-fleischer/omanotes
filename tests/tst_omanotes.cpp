#include <QtTest>
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextLine>
#include <QColor>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QSignalSpy>
#include <QQuickTextDocument>
#include <QWindow>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "lexillacodehighlighter.h"
#include "markdownhighlighter.h"
#include "vaultmodel.h"

class OmanotesTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        // Keeps recovery snapshots out of the real ~/.local/share/omanotes.
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    // Closing the window is not a decision about the text: what was typed comes
    // back in the next window, the way Sublime Text's hot exit works.
    // An INI file holds "false" as text. Read back untyped it becomes a true
    // bool in QML, and the window opens with a sidebar nobody asked for.
    void readsSettingsBackAsTypes() {
        QSettings settings;
        settings.setValue(QStringLiteral("vault/sidebarVisible"), QStringLiteral("false"));
        settings.setValue(QStringLiteral("vault/sidebarWidth"), QStringLiteral("312"));
        settings.setValue(QStringLiteral("view/fullWidth"), QStringLiteral("false"));
        settings.setValue(QStringLiteral("view/zoom"), QStringLiteral("1.4"));
        settings.setValue(QStringLiteral("window/maximized"), QStringLiteral("false"));

        Backend backend;
        const QVariantMap sidebar = backend.sidebarState();
        QCOMPARE(sidebar.value(QStringLiteral("visible")).typeId(), QMetaType::Bool);
        QCOMPARE(sidebar.value(QStringLiteral("visible")).toBool(), false);
        QCOMPARE(sidebar.value(QStringLiteral("width")).toInt(), 312);

        const QVariantMap view = backend.viewState();
        QCOMPARE(view.value(QStringLiteral("fullWidth")).typeId(), QMetaType::Bool);
        QCOMPARE(view.value(QStringLiteral("fullWidth")).toBool(), false);
        QCOMPARE(view.value(QStringLiteral("zoom")).toDouble(), 1.4);

        QCOMPARE(backend.windowGeometry().value(QStringLiteral("maximized")).typeId(),
                 QMetaType::Bool);

        settings.remove(QStringLiteral("vault"));
        settings.remove(QStringLiteral("view"));
        settings.remove(QStringLiteral("window"));
    }

    // Leaving a note with unsaved text used to stop and ask. It keeps the text
    // under that note instead, and the sidebar marks it.
    // Fence rows and thematic breaks are punctuation, not content: they
    // collapse until the caret is in the block they belong to.
    void alignsATableThatWasTypedIn() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("table.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("| Month | Savings |\n"
                   "|---|---|\n"
                   "| January | $250 |\n"
                   "| February | $80 |\n"
                   "\n"
                   "after\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);

        // Opening a file changes nothing, whatever its tables look like.
        QVERIFY(!backend.modified());
        QCOMPARE(document->findBlockByNumber(2).text(), QStringLiteral("| January | $250 |"));

        // Ragged source gets no column rules: half a grid reads worse than none.
        QVERIFY(backend.tableRegions().constFirst().toMap()
                    .value(QStringLiteral("columns")).toList().isEmpty());

        // Typing in it and moving away tidies it.
        backend.setCursorPosition(document->findBlockByNumber(2).position());
        QVERIFY(QMetaObject::invokeMethod(
            editor.data(), "insert",
            Q_ARG(int, document->findBlockByNumber(2).position() + 2),
            Q_ARG(QString, QStringLiteral("x"))));
        backend.editorTextChanged();
        backend.setCursorPosition(document->findBlockByNumber(5).position());

        const QString header = document->findBlockByNumber(0).text();
        for (int line = 1; line <= 3; ++line)
            QCOMPARE(document->findBlockByNumber(line).text().size(), header.size());
        QVERIFY(header.startsWith(QStringLiteral("| Month ")));

        // Now every row has its pipes in the same columns, so the grid is drawn.
        backend.updateTableGridsForTest();
        const QVariantList columns = backend.tableRegions().constFirst().toMap()
                                         .value(QStringLiteral("columns")).toList();
        QCOMPARE(columns.size(), 3);

        // Put the caret back in the table and it goes back to plain source:
        // nothing drawn over it, wherever in the table the caret sits.
        backend.setCursorPosition(document->findBlockByNumber(3).position());
        const QVariantMap editing = backend.tableRegions().constFirst().toMap();
        QCOMPARE(editing.value(QStringLiteral("editing")).toBool(), true);
        QVERIFY(editing.value(QStringLiteral("columns")).toList().isEmpty());
        QCOMPARE(editing.value(QStringLiteral("separator")).toInt(), -1);

        // The pipes come back with it, on every row and not just the caret's.
        document->setTextWidth(600);
        (void)document->size();
        QVERIFY(!qFuzzyCompare(formatAt(*document, 0, 0).fontPointSize(), 1.0));
        QVERIFY(!qFuzzyCompare(formatAt(*document, 2, 0).fontPointSize(), 1.0));

        // Leaving it again restores the grid.
        backend.setCursorPosition(document->findBlockByNumber(5).position());
        QCOMPARE(backend.tableRegions().constFirst().toMap()
                     .value(QStringLiteral("editing")).toBool(), false);

        // One undo puts the table back exactly as it was written.
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "undo"));
        QCOMPARE(document->findBlockByNumber(1).text(), QStringLiteral("|---|---|"));
    }

    // Putting the caret in a table is what counts as working on it, so leaving
    // one tidies it whether or not you typed in it. Visiting one that already
    // lines up has to change nothing, or every click would dirty the note.
    void alignsATableTheCaretVisited() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("visited.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("| Day | Cris | Rux |\n"
                   "|---|---|---|\n"
                   "| Mon | O 100% | H 50% |\n"
                   "| Tue | H 50% | O 100% |\n"
                   "\n"
                   "after\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);
        const auto line = [document](int number) {
            return document->findBlockByNumber(number).text();
        };
        const int outside = document->findBlockByNumber(5).position();

        // The view puts the caret at the top of a note it has just loaded.
        // That is not the reader visiting anything.
        backend.setCursorPosition(0);
        backend.setCursorPosition(outside);
        QVERIFY(!backend.editorTextChanged());
        QVERIFY(!backend.modified());
        QCOMPARE(line(1), QStringLiteral("|---|---|---|"));

        // Caret in, caret out: tidied, without a character typed. The editor
        // reports the change the same way it reports typing, which is what
        // marks the note unsaved.
        backend.setCursorPosition(document->findBlockByNumber(2).position());
        backend.setCursorPosition(outside);
        QVERIFY(backend.editorTextChanged());
        QVERIFY(backend.modified());
        const QString header = line(0);
        for (int row = 1; row <= 3; ++row)
            QCOMPARE(line(row).size(), header.size());
        QVERIFY(header.startsWith(QStringLiteral("| Day ")));

        // Now the columns line up, so the grid is drawn over it.
        QCOMPARE(backend.tableRegions().constFirst().toMap()
                     .value(QStringLiteral("columns")).toList().size(), 4);

        // Visiting it again is free: nothing to tidy, so nothing changes and
        // the note does not go dirty behind you.
        backend.save();
        QVERIFY(!backend.modified());
        const QString settled = line(0);
        backend.setCursorPosition(document->findBlockByNumber(3).position());
        backend.setCursorPosition(outside);
        QVERIFY(!backend.editorTextChanged());
        QVERIFY(!backend.modified());
        QCOMPARE(line(0), settled);
    }

    // Setting the text moves the caret to the end of it, which is inside a
    // table when the note ends with one. That is not the reader revealing
    // anything, and the grid must not be worked out around it: the note came
    // up with its last table drawn as rules and written out in pipes at once.
    void aTableAtTheEndOfANoteIsStillGridded() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        const QString path = vaultDirectory.filePath(QStringLiteral("ends-in-a-table.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("# Schedule\n"
                   "\n"
                   "| Day | Cris   |\n"
                   "|-----|--------|\n"
                   "| Mon | O 100% |\n"
                   "\n"
                   "| one  | two  |\n"
                   "|------|------|\n"
                   "| four | five |\n"
                   "\n"
                   "| a | b |\n"
                   "|---|---|\n"
                   "| ragged | c |");
        seed.close();

        Backend backend;
        VaultModel vault;
        vault.setRoot(vaultDirectory.path());

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        engine.rootContext()->setContextProperty(QStringLiteral("vault"), &vault);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        backend.open(QUrl::fromLocalFile(path));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);
        document->setTextWidth(600);
        (void)document->size();

        // Opening it changed nothing, whatever the caret did on the way in.
        // The ragged table at the very end is where that shows: the caret
        // lands in it, and tidying on the way out would rewrite it.
        QVERIFY(!backend.modified());
        QCOMPARE(document->findBlockByNumber(11).text(), QStringLiteral("|---|---|"));

        const QVariantList regions = backend.tableRegions();
        QCOMPARE(regions.size(), 3);
        for (int table = 0; table < 2; ++table) {
            const QVariantMap region = regions.at(table).toMap();
            QVERIFY(!region.value(QStringLiteral("editing")).toBool());
            QVERIFY(!region.value(QStringLiteral("columns")).toList().isEmpty());
        }

        // Both aligned tables fold their pipes, the second one included: it is
        // the one the caret passed through while the text was being set.
        for (int block : {2, 3, 4, 6, 7, 8})
            QCOMPARE(formatAt(*document, block, 0).fontPointSize(), 1.0);

        // The ragged one keeps everything it was written with.
        QVERIFY(regions.at(2).toMap().value(QStringLiteral("columns")).toList().isEmpty());
        for (int block : {10, 11, 12})
            QVERIFY(!qFuzzyCompare(formatAt(*document, block, 0).fontPointSize(), 1.0));
    }

    void tableRegionsShareOnlyAlignedColumns() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("tables.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("| a  | b  |\n"
                   "|----|----|\n"
                   "| 1  | 2  |\n"
                   "\n"
                   "| a | b |\n"
                   "|---|---|\n"
                   "| much longer | c |\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        const QVariantList regions = backend.tableRegions();
        QCOMPARE(regions.size(), 2);

        // Aligned source: every row has a pipe in the same three columns, so
        // all three can be drawn as one continuous rule.
        const QVariantMap aligned = regions.constFirst().toMap();
        QCOMPARE(aligned.value(QStringLiteral("columns")).toList().size(), 3);
        QVERIFY(aligned.value(QStringLiteral("separator")).toInt() >= 0);

        // Ragged source shares only the columns that happen to line up, so
        // nothing is drawn through a pipe that is not there, and with no
        // column rules the separator rule goes too rather than hanging across
        // the block on its own.
        const QVariantMap ragged = regions.at(1).toMap();
        QVERIFY(ragged.value(QStringLiteral("columns")).toList().isEmpty());
        QCOMPARE(ragged.value(QStringLiteral("separator")).toInt(), -1);

        // With no rule drawn for it, the separator row stays visible: folding
        // it away would leave the header and the body with nothing between
        // them at all.
        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);
        document->setTextWidth(600);
        (void)document->size();
        QVERIFY(!qFuzzyCompare(formatAt(*document, 5, 0).fontPointSize(), 1.0));
        // The aligned table's separator does fold, because a rule replaces it.
        QCOMPARE(formatAt(*document, 1, 0).fontPointSize(), 1.0);
    }

    void markersRevealWhereTheCaretIs() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("reveal.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("prose\n"
                   "\n"
                   "---\n"
                   "\n"
                   "```sh\n"
                   "ls\n"
                   "```\n"
                   "after\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);
        const auto blockAt = [document](int line) { return document->findBlockByNumber(line); };
        const auto layOut = [document]() { document->setTextWidth(600); (void)document->size(); };
        const auto hidden = [&](int line) {
            layOut();
            return qFuzzyCompare(formatAt(*document, line, 0).fontPointSize(), 1.0);
        };

        // Caret on the first line: everything else is collapsed.
        backend.setCursorPosition(0);
        QVERIFY(hidden(2));   // the rule
        QVERIFY(hidden(4));   // the opening fence
        QVERIFY(hidden(6));   // the closing fence
        QCOMPARE(backend.thematicBreakPositions(), QList<int>{blockAt(2).position()});

        // Caret on the rule: its dashes come back, the fence stays collapsed.
        backend.setCursorPosition(blockAt(2).position());
        QVERIFY(!hidden(2));
        QVERIFY(hidden(4));

        // Caret inside the fenced block: both of its rows come back.
        backend.setCursorPosition(blockAt(5).position());
        QVERIFY(!hidden(4));
        QVERIFY(!hidden(6));
        QVERIFY(hidden(2));

        // And leaving it collapses them again.
        backend.setCursorPosition(blockAt(7).position());
        QVERIFY(hidden(4));
        QVERIFY(hidden(6));
    }

    void findsLinkTargetsAndTaskMarkers() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeNote(directory.path(), QStringLiteral("note.md")));
        QVERIFY(writeNote(directory.path(), QStringLiteral("sibling.md")));

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(directory.filePath(QStringLiteral("note.md"))));

        const QString body = QStringLiteral(
            "See [the site](https://example.com/a) and https://plain.example/b too.\n"
            "- [ ] open task\n"
            "- [x] done task\n"
            "Read sibling.md for more.\n"
            "Nothing here.");
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, body)));

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        const auto at = [document](int line, int column) {
            return document->findBlockByNumber(line).position() + column;
        };

        // Clicking the label of a Markdown link opens its destination.
        QCOMPARE(backend.linkTargetAt(at(0, 7)), QStringLiteral("https://example.com/a"));
        // A bare URL works, and the full stop after it is not part of it.
        QCOMPARE(backend.linkTargetAt(at(0, 48)), QStringLiteral("https://plain.example/b"));
        // A file beside the note resolves; a word does not.
        QCOMPARE(backend.linkTargetAt(at(3, 7)), QStringLiteral("sibling.md"));
        QCOMPARE(backend.linkTargetAt(at(4, 3)), QString());

        // The whole `[ ]` is clickable, not just the character between them.
        QCOMPARE(backend.taskMarkerAt(at(1, 2)), at(1, 3));
        QCOMPARE(backend.taskMarkerAt(at(1, 3)), at(1, 3));
        QCOMPARE(backend.taskMarkerAt(at(1, 4)), at(1, 3));
        QCOMPARE(backend.taskMarkerAt(at(2, 3)), at(2, 3));
        // Not on the text, and not on a line that is not a task.
        QCOMPARE(backend.taskMarkerAt(at(1, 8)), -1);
        QCOMPARE(backend.taskMarkerAt(at(4, 2)), -1);
    }

    void keepsADraftPerNote() {
        clearRecoverySnapshots();

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeNote(directory.path(), QStringLiteral("first.md")));
        QVERIFY(writeNote(directory.path(), QStringLiteral("second.md")));
        const QUrl first = QUrl::fromLocalFile(directory.filePath(QStringLiteral("first.md")));
        const QUrl second = QUrl::fromLocalFile(directory.filePath(QStringLiteral("second.md")));

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());

        backend.open(first);
        QVERIFY(!backend.modified());
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("draft "))));
        backend.editorTextChanged();
        QVERIFY(backend.modified());
        QCOMPARE(backend.draftPaths(), QStringList{first.toLocalFile()});

        // Switching away keeps it, and the second note opens clean.
        backend.open(second);
        QVERIFY(!backend.modified());
        QVERIFY(!editor->property("text").toString().startsWith(QStringLiteral("draft ")));
        QCOMPARE(backend.draftPaths(), QStringList{first.toLocalFile()});

        // Coming back brings it, still unsaved, with the file untouched.
        backend.open(first);
        QVERIFY(backend.modified());
        QVERIFY(editor->property("text").toString().startsWith(QStringLiteral("draft ")));
        QFile onDisk(first.toLocalFile());
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QVERIFY(!onDisk.readAll().startsWith("draft "));
        onDisk.close();

        // Saving settles it and the mark goes.
        backend.save();
        QVERIFY(!backend.modified());
        QVERIFY(backend.draftPaths().isEmpty());

        clearRecoverySnapshots();
    }

    void keepsUnsavedDraftAcrossRestart() {
        clearRecoverySnapshots();

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("draft.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        QCOMPARE(seed.write("original\n"), qint64(9));
        seed.close();
        const QUrl fileUrl = QUrl::fromLocalFile(path);

        {
            QQmlEngine engine;
            QScopedPointer<QObject> editor(createEditor(&engine));
            QVERIFY(editor);
            Backend backend;
            backend.attachDocument(editor->property("textDocument").value<QObject *>());
            backend.open(fileUrl);

            QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                              Q_ARG(QString, QStringLiteral("draft "))));
            backend.editorTextChanged();
            QVERIFY(backend.modified());
            backend.persistDraft();
        }

        // The file on disk is exactly as it was left.
        QFile onDisk(path);
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), QByteArray("original\n"));
        onDisk.close();

        {
            QQmlEngine engine;
            QScopedPointer<QObject> editor(createEditor(&engine));
            QVERIFY(editor);
            Backend backend;
            backend.attachDocument(editor->property("textDocument").value<QObject *>());

            QCOMPARE(editor->property("text").toString(),
                     QStringLiteral("draft original\n"));
            QVERIFY(backend.modified());
            QCOMPARE(backend.fileUrl(), fileUrl);
            backend.discardRecovery();
        }

        clearRecoverySnapshots();
    }

    void zoomsAndWidensTheEditor() {
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
        QVERIFY(window->findChild<QObject *>(QStringLiteral("widthButton")));
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 20);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "setZoom",
                                          Q_ARG(QVariant, QVariant(1.5))));
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 30);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 30);

        // Clamped at both ends, so a stuck key cannot make the text unusable.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setZoom",
                                          Q_ARG(QVariant, QVariant(9.0))));
        QCOMPARE(window->property("editorZoom").toDouble(), 2.5);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setZoom",
                                          Q_ARG(QVariant, QVariant(0.0))));
        QCOMPARE(window->property("editorZoom").toDouble(), 0.6);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "setZoom",
                                          Q_ARG(QVariant, QVariant(1.0))));
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 20);

        // The column is a measured 65 characters until it is switched off.
        QVERIFY(window->property("editorWidth").toInt()
                < window->property("editorAreaWidth").toInt());
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleFullWidth"));
        QCOMPARE(window->property("fullWidth").toBool(), true);
        QCOMPARE(window->property("editorWidth").toInt(),
                 window->property("editorAreaWidth").toInt());

        // Every view setting outlives the window.
        QVariantMap view = backend.viewState();
        QCOMPARE(view.value(QStringLiteral("zoom")).toDouble(), 1.0);
        QCOMPARE(view.value(QStringLiteral("fullWidth")).toBool(), true);
        QCOMPARE(view.value(QStringLiteral("contentColumns")).toInt(), 65);
        QCOMPARE(window->property("contentColumns").toInt(), 65);

        // The bundled family is stored as empty, so replacing the bundled font
        // in a later release changes the default for anyone who never picked.
        QCOMPARE(window->property("editorFontFamily").toString(),
                 QStringLiteral("iA Writer Mono S"));
        QCOMPARE(view.value(QStringLiteral("fontFamily")).toString(), QString());
        QVERIFY(window->findChild<QObject *>(QStringLiteral("fontDialog")));

        const QStringList families = QFontDatabase::families();
        QVERIFY(!families.isEmpty());
        const QString installed = families.constFirst();

        QVERIFY(QMetaObject::invokeMethod(window.data(), "setEditorFont",
                                          Q_ARG(QVariant, QVariant(installed))));
        QCOMPARE(editor->property("font").value<QFont>().family(), installed);
        QCOMPARE(backend.viewState().value(QStringLiteral("fontFamily")).toString(),
                 installed);

        // The portal chooser lists styles as if they were families.
        QCOMPARE(Backend::resolveFontFamily(installed + QStringLiteral(" Bold")), installed);
        QCOMPARE(Backend::resolveFontFamily(QStringLiteral("No Such Family")), QString());
        QCOMPARE(Backend::resolveFontFamily(QString()), QString());

        // Anything that resolves to nothing falls back to the bundled font.
        QVERIFY(QMetaObject::invokeMethod(window.data(), "setEditorFont",
                                          Q_ARG(QVariant, QVariant(QStringLiteral("No Such Family")))));
        QCOMPARE(window->property("editorFontFamily").toString(),
                 QStringLiteral("iA Writer Mono S"));

        // The font dialog's own path: whatever it hands back has to reach the
        // editor. accept() is a no-op on a closed dialog, so open it first;
        // that covers everything except GTK's own list widget.
        QObject *fontDialog = window->findChild<QObject *>(QStringLiteral("fontDialog"));
        QVERIFY(fontDialog);
        QVERIFY(fontDialog->setProperty("selectedFont", QVariant::fromValue(QFont(installed))));
        QVERIFY(QMetaObject::invokeMethod(fontDialog, "open"));
        QVERIFY(fontDialog->setProperty("selectedFont", QVariant::fromValue(QFont(installed))));
        QVERIFY(QMetaObject::invokeMethod(fontDialog, "accept"));
        QCOMPARE(window->property("editorFontFamily").toString(), installed);
        QCOMPARE(editor->property("font").value<QFont>().family(), installed);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleFullWidth"));
        backend.saveViewState(1.0, false, QString(), 65, QString());
    }

    // visibility reads Hidden by the time the window is torn down, so the state
    // to restore is the one recorded while it was still on screen.
    // Line height is a block property, so it is the one part of the styling
    // QSyntaxHighlighter cannot set. Backend reads the block's kind instead.
    // The slab is a step away from the page, not a fixed grey, so it keeps the
    // palette's hue when the wallpaper changes it.
    // Box-drawing characters join only when the font draws them at least as
    // tall as its own line. Having the glyphs is not enough: Noto Sans Mono has
    // them at 0.92 of its line spacing, so every diagram comes out dashed.
    void boldAndItalicTogether() {
        // `**` would claim the outer pair of `***both***` and leave the third
        // asterisk in the text, so the three-marker form is matched first and
        // its span is off limits to the other two.
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("a ***both*** b"));
        QCOMPARE(markup.size(), 1);
        QVERIFY(markup.constFirst().kind == MarkdownHighlighter::InlineKind::BoldItalic);
        QCOMPARE(markup.constFirst().content.start, 5);
        QCOMPARE(markup.constFirst().content.length, 4);
        QCOMPARE(markup.constFirst().markers[0].length, 3);
        QCOMPARE(markup.constFirst().markers[1].length, 3);

        QCOMPARE(MarkdownHighlighter::inlineMarkup(QStringLiteral("___both___")).size(), 1);

        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral(
            "***both*** and **bold with *nested* inside**"));

        const QTextCharFormat together = formatAt(document, 0, 4);
        QCOMPARE(together.fontWeight(), int(QFont::Bold));
        QVERIFY(together.fontItalic());

        // Nesting the other way round has always worked, and still does.
        const QTextCharFormat nested = formatAt(document, 0, 33);
        QCOMPARE(nested.fontWeight(), int(QFont::Bold));
        QVERIFY(nested.fontItalic());

        // The plain bold around it is bold and not italic.
        QCOMPARE(formatAt(document, 0, 18).fontWeight(), int(QFont::Bold));
        QVERIFY(!formatAt(document, 0, 18).fontItalic());
    }

    void fencedCodeIsSyntaxHighlighted() {
        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral(
            "```php\n"
            "// a comment\n"
            "$name = 'value';\n"
            "```\n"
            "\n"
            "```text\n"
            "// not code\n"
            "```"));

        const QBrush comment = formatAt(document, 1, 0).foreground();
        const QBrush code = formatAt(document, 2, 0).foreground();
        QVERIFY(comment.style() != Qt::NoBrush);
        // A comment is not coloured like the code beside it.
        QVERIFY(comment != code);
        QVERIFY(formatAt(document, 1, 0).fontItalic());

        // The string in the second line is coloured too, differently again.
        const QBrush string = formatAt(document, 2, 9).foreground();
        QVERIFY(string != code);
        QVERIFY(string != comment);

        // `text` is not a language, so the same line is left alone there.
        QCOMPARE(formatAt(document, 6, 0).foreground(),
                 formatAt(document, 6, 5).foreground());
        QVERIFY(!formatAt(document, 6, 0).fontItalic());

        // The language rides along in the block state.
        QVERIFY(MarkdownHighlighter::isFencedState(
            document.findBlockByNumber(1).userState()));
        QCOMPARE((document.findBlockByNumber(1).userState() >> 8) & 0xff, 1);
        QCOMPARE((document.findBlockByNumber(6).userState() >> 8) & 0xff, 0);
    }

    void lexillaTokenizesCode() {
        LexillaCodeHighlighter highlighter;
        QVERIFY(highlighter.supports(QStringLiteral("php")));
        QVERIFY(highlighter.supports(QStringLiteral("PHP")));
        QVERIFY(highlighter.supports(QStringLiteral("bash")));
        QVERIFY(!highlighter.supports(QStringLiteral("text")));
        QVERIFY(!highlighter.supports(QString()));

        const auto kinds = [&](const QString &language, const QString &line) {
            int state = 0;
            QList<CodeSyntaxHighlighter::Token> tokens;
            for (const CodeSyntaxHighlighter::Span &span :
                     highlighter.tokenize(language, line, state))
                tokens.append(span.token);
            return tokens;
        };

        QVERIFY(kinds(QStringLiteral("php"),
                      QStringLiteral("$x = 'text'; // note"))
                    .contains(CodeSyntaxHighlighter::Token::Comment));
        // LexHTML keeps PHP keywords in word list 4, not 0. Getting that wrong
        // colours strings and comments but leaves every keyword plain.
        QVERIFY(kinds(QStringLiteral("php"), QStringLiteral("function f() { return 1; }"))
                    .contains(CodeSyntaxHighlighter::Token::Keyword));
        QVERIFY(kinds(QStringLiteral("bash"), QStringLiteral("# a comment"))
                    .contains(CodeSyntaxHighlighter::Token::Comment));
        QVERIFY(kinds(QStringLiteral("python"), QStringLiteral("def f(): return 42"))
                    .contains(CodeSyntaxHighlighter::Token::Keyword));
        // A JSON key is a property name, not a string; the value is the string.
        QVERIFY(kinds(QStringLiteral("json"), QStringLiteral("{\"a\": \"b\"}"))
                    .contains(CodeSyntaxHighlighter::Token::String));
        QVERIFY(kinds(QStringLiteral("json"), QStringLiteral("{\"a\": 1}"))
                    .contains(CodeSyntaxHighlighter::Token::Number));

        // Spans stay inside the line and never overlap.
        int state = 0;
        const QString line = QStringLiteral("function greet($who) { echo \"hi $who\"; }");
        const QList<CodeSyntaxHighlighter::Span> spans =
            highlighter.tokenize(QStringLiteral("php"), line, state);
        QVERIFY(!spans.isEmpty());
        int previousEnd = 0;
        for (const CodeSyntaxHighlighter::Span &span : spans) {
            QVERIFY(span.start >= previousEnd);
            QVERIFY(span.length > 0);
            QVERIFY(span.start + span.length <= line.length());
            previousEnd = span.start + span.length;
        }

        // State carries a block comment across lines.
        int blockState = 0;
        highlighter.tokenize(QStringLiteral("javascript"),
                             QStringLiteral("/* opened here"), blockState);
        const QList<CodeSyntaxHighlighter::Span> continued =
            highlighter.tokenize(QStringLiteral("javascript"),
                                 QStringLiteral("still inside"), blockState);
        QVERIFY(!continued.isEmpty());
        QCOMPARE(continued.constFirst().token, CodeSyntaxHighlighter::Token::Comment);

        // An unknown language yields nothing rather than misleading colour.
        int unknown = 0;
        QVERIFY(highlighter.tokenize(QStringLiteral("brainfuck"),
                                     QStringLiteral("+++."), unknown).isEmpty());
    }

    void codeFontDrawsContinuousBoxes() {
        QVERIFY(!MarkdownHighlighter::drawsContinuousBoxes(QString()));
        QVERIFY(!MarkdownHighlighter::drawsContinuousBoxes(
            QStringLiteral("No Such Family At All")));

        QTextDocument document;
        QFont base(QStringLiteral("Noto Sans Mono"));
        base.setPixelSize(20);
        document.setDefaultFont(base);
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("```\n\u250c\u2500\u2510\n```"));

        // Whatever it settles on, it has to be a family that can draw the boxes.
        const QStringList families = highlighter.codeFamilies();
        QVERIFY(!families.isEmpty());
        QVERIFY2(MarkdownHighlighter::drawsContinuousBoxes(families.constFirst()),
                 qPrintable(families.constFirst()));
        // The document font cannot, so it must have chosen something else.
        QVERIFY(families.constFirst() != QStringLiteral("Noto Sans Mono"));

        // A pinned family wins outright.
        const QString pinned = QFontDatabase::families().constFirst();
        highlighter.setCodeFontFamily(pinned);
        QCOMPARE(highlighter.codeFamilies().constFirst(), pinned);
        highlighter.setCodeFontFamily(QString());
        QVERIFY(MarkdownHighlighter::drawsContinuousBoxes(
            highlighter.codeFamilies().constFirst()));
    }

    void codeBackgroundFollowsThePage() {
        const auto lightnessOf = [](const QColor &color) { return color.toHsl().lightnessF(); };

        const QColor page(QStringLiteral("#11131c"));
        const QColor slab = MarkdownHighlighter::codeBackgroundFor(page.name(), true);
        QVERIFY(slab.isValid());
        QVERIFY(lightnessOf(slab) < lightnessOf(page));
        // Same hue, so it reads as the page rather than as grey pasted on top.
        QCOMPARE(slab.toHsl().hue(), page.toHsl().hue());

        const QColor lightPage(QStringLiteral("#f8f9ff"));
        const QColor lightSlab = MarkdownHighlighter::codeBackgroundFor(lightPage.name(), false);
        QVERIFY(lightnessOf(lightSlab) < lightnessOf(lightPage));

        // Nothing darker is left on a page that is already black.
        const QColor blackSlab = MarkdownHighlighter::codeBackgroundFor(
            QStringLiteral("#000000"), true);
        QVERIFY(lightnessOf(blackSlab) > 0.0);

        // An unset palette still yields a usable colour.
        QVERIFY(MarkdownHighlighter::codeBackgroundFor(QString(), true).isValid());
        QVERIFY(MarkdownHighlighter::codeBackgroundFor(QStringLiteral("nonsense"), false).isValid());
    }

    void lineHeightPerBlockKind() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("kinds.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("prose line\n"
                   "\n"
                   "| a | b |\n"
                   "|---|---|\n"
                   "\n"
                   "```sh\n"
                   "ls *.md\n"
                   "```\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);

        const auto lineHeight = [document](int blockNumber) {
            return document->findBlockByNumber(blockNumber).blockFormat().lineHeight();
        };

        QCOMPARE(lineHeight(0), 140.0);  // prose
        QCOMPARE(lineHeight(2), 120.0);  // table row
        QCOMPARE(lineHeight(3), 120.0);  // separator row
        // The font's natural spacing, so box-drawing characters tile.
        QCOMPARE(lineHeight(5), 100.0);  // opening fence
        QCOMPARE(lineHeight(6), 100.0);  // code
        QCOMPARE(lineHeight(7), 100.0);  // closing fence

        // Code and tables are drawn on a slab, so their text is inset from the
        // column the prose uses. The slab is bled out by the same amount in
        // QML, which is what puts space on all four sides of the block.
        const auto margins = [document](int blockNumber) {
            const QTextBlockFormat format =
                document->findBlockByNumber(blockNumber).blockFormat();
            return QPair<qreal, qreal>{format.leftMargin(), format.rightMargin()};
        };
        const qreal padding = backend.blockPadding();
        QVERIFY(padding > 0);
        QCOMPARE(margins(0), (QPair<qreal, qreal>{0.0, 0.0}));            // prose
        QCOMPARE(margins(2), (QPair<qreal, qreal>{padding, padding}));    // table row
        QCOMPARE(margins(3), (QPair<qreal, qreal>{padding, padding}));    // separator
        QCOMPARE(margins(6), (QPair<qreal, qreal>{padding, padding}));    // code

        // Every run of fenced lines is reported once, fences included, so QML
        // can paint one slab behind each. Qt Quick's text node paints character
        // backgrounds only, which came out ragged where lines are short and
        // striped through the leading.
        const QVariantList regions = backend.fencedCodeRegions();
        QCOMPARE(regions.size(), 1);
        const QVariantMap region = regions.constFirst().toMap();
        QCOMPARE(region.value(QStringLiteral("start")).toInt(),
                 document->findBlockByNumber(5).position());
        QCOMPARE(region.value(QStringLiteral("end")).toInt(),
                 document->findBlockByNumber(7).position());

        // Opening a fence at the top restates every line under it. The changed
        // range covers one block; the rest come from the highlighter.
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("```\n"))));
        backend.editorTextChanged();
        QCOMPARE(lineHeight(1), 100.0);  // "prose line" is inside the fence now
        QCOMPARE(lineHeight(3), 100.0);  // and so is the table
        QCOMPARE(margins(1), (QPair<qreal, qreal>{padding, padding}));

        // Undoing puts both the text and the line heights back.
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "undo"));
        backend.editorTextChanged();
        QCOMPARE(lineHeight(0), 140.0);
        QCOMPARE(lineHeight(2), 120.0);
        QCOMPARE(margins(0), (QPair<qreal, qreal>{0.0, 0.0}));
        QCOMPARE(backend.fencedCodeRegions().size(), 1);
    }

    // Typography is a block property, so every block needs one. Setting them
    // one at a time laid the document out once per block: 660 ms for this
    // note, and a second of frozen window before it appeared.
    void opensALargeNoteQuickly() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("big.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        QByteArray text;
        for (int i = 0; i < 1500; ++i) {
            if (i % 25 == 0)
                text += "\n```sh\nls -la ~/Notes\n```\n";
            else
                text += "A line of prose with some **bold** and `code` in it.\n";
        }
        seed.write(text);
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());

        QElapsedTimer timer;
        timer.start();
        backend.open(QUrl::fromLocalFile(path));
        const qint64 elapsed = timer.elapsed();

        QTextDocument *document =
            qobject_cast<QQuickTextDocument *>(
                editor->property("textDocument").value<QObject *>())->textDocument();
        QVERIFY(document);
        QVERIFY(document->blockCount() > 1500);
        // Well clear of what it costs, so this only fails if the per-block
        // edit comes back rather than when the machine is busy.
        QVERIFY2(elapsed < 300,
                 qPrintable(QStringLiteral("opening took %1 ms").arg(elapsed)));
    }

    // The chrome's family is a config knob: main() reads it before any window
    // exists, and the view state carries it back so it stays in the file.
    void interfaceFontComesFromTheConfigFile() {
        QSettings().remove(QStringLiteral("view/interfaceFontFamily"));
        QCOMPARE(Backend::interfaceFontFamily(), QString());

        const QStringList families = QFontDatabase::families();
        QVERIFY(!families.isEmpty());
        const QString installed = families.constFirst();

        Backend backend;
        backend.saveViewState(1.0, false, QString(), 65, installed);
        QCOMPARE(backend.viewState().value(QStringLiteral("interfaceFontFamily")).toString(),
                 installed);
        QCOMPARE(Backend::interfaceFontFamily(), installed);

        // A family nothing on the system provides falls back to the bundled one.
        QSettings().setValue(QStringLiteral("view/interfaceFontFamily"),
                             QStringLiteral("No Such Family"));
        QCOMPARE(Backend::interfaceFontFamily(), QString());
        QSettings().remove(QStringLiteral("view/interfaceFontFamily"));
    }

    void keepsWindowStateAcrossRuns() {
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

        window->setProperty("lastWindowedGeometry", QRectF(120, 80, 900, 600));

        // Qt reports a tiled window as maximised, so this is the ordinary case
        // on a tiling compositor, not an unusual one.
        window->setProperty("lastVisibility", int(QWindow::Maximized));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "saveWindowState"));
        QVariantMap geometry = backend.windowGeometry();
        QCOMPARE(geometry.value(QStringLiteral("maximized")).toBool(), true);
        QCOMPARE(geometry.value(QStringLiteral("fullScreen")).toBool(), false);
        QCOMPARE(geometry.value(QStringLiteral("width")).toInt(), 900);
        QCOMPARE(geometry.value(QStringLiteral("x")).toInt(), 120);

        window->setProperty("lastVisibility", int(QWindow::FullScreen));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "saveWindowState"));
        geometry = backend.windowGeometry();
        QCOMPARE(geometry.value(QStringLiteral("fullScreen")).toBool(), true);
        QCOMPARE(geometry.value(QStringLiteral("maximized")).toBool(), false);
        // Still the windowed size, not the screen's.
        QCOMPARE(geometry.value(QStringLiteral("width")).toInt(), 900);
        QCOMPARE(geometry.value(QStringLiteral("height")).toInt(), 600);

        window->setProperty("lastVisibility", int(QWindow::Windowed));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "saveWindowState"));
        geometry = backend.windowGeometry();
        QCOMPARE(geometry.value(QStringLiteral("maximized")).toBool(), false);
        QCOMPARE(geometry.value(QStringLiteral("fullScreen")).toBool(), false);

        QSettings().remove(QStringLiteral("window"));
    }

    void headingFormats() {
        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("## Heading\nbody"));

        const QTextCharFormat content = formatAt(document, 0, 3);
        QCOMPARE(content.fontPointSize(), 12.0 * 1.6);
        QCOMPARE(content.fontWeight(), int(QFont::Bold));

        // The `##` and the space after it collapse to nothing, or the heading
        // would sit indented by its own syntax.
        QCOMPARE(formatAt(document, 0, 0).fontPointSize(), 1.0);
        QVERIFY(formatAt(document, 0, 0).fontLetterSpacing() < 0);
        QCOMPARE(formatAt(document, 0, 2).fontPointSize(), 1.0);

        QCOMPARE(formatAt(document, 1, 0).fontPointSize(), 0.0);

        // And the caret is told about them, or it gets stuck at column zero.
        const auto markup = MarkdownHighlighter::inlineMarkup(QStringLiteral("## Heading"));
        QCOMPARE(markup.size(), 1);
        QVERIFY(markup.at(0).kind == MarkdownHighlighter::InlineKind::Heading);
        QCOMPARE(markup.at(0).level, 2);
        QCOMPARE(markup.at(0).markers[0].start, 0);
        QCOMPARE(markup.at(0).markers[0].length, 3);

        setDocumentText(document, QStringLiteral("# One\n###### Six"));

        // Each level is a clear step from the one above, and the deep ones fade
        // as well, so H2 and H4 are not a guess.
        double previous = 0;
        for (int level = 1; level <= 6; ++level) {
            QTextDocument levels;
            levels.setDefaultFont(bodyFont());
            MarkdownHighlighter each(&levels);
            setDocumentText(levels, QString(level, QLatin1Char('#'))
                                        + QStringLiteral(" Heading"));
            const double size = formatAt(levels, 0, level + 1).fontPointSize();
            if (previous > 0)
                QVERIFY2(previous - size >= 1.0, qPrintable(QString::number(size)));
            previous = size;
        }
        QCOMPARE(formatAt(document, 0, 2).fontPointSize(), 12.0 * 1.9);
        QCOMPARE(formatAt(document, 1, 7).fontPointSize(), 12.0);
    }

    void fencedCodeState() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral(
            "```sh\nls *.md\nnot *italic* here\n```\nafter *italic*"));

        // A star inside a fence is a glob, not emphasis.
        QVERIFY(!formatAt(document, 1, 4).fontItalic());
        QVERIFY(!formatAt(document, 2, 5).fontItalic());

        // The fence covers every line between its markers. The slab behind them
        // is drawn in QML from Backend::fencedCodeRegions, not as a character
        // background, so what the highlighter leaves here is the monospace.
        QVERIFY(!formatAt(document, 1, 0).fontFamilies().toStringList().isEmpty());
        QVERIFY(!formatAt(document, 2, 0).fontFamilies().toStringList().isEmpty());
        // The state also carries the language and the lexer's own position, so
        // it is read through the helper rather than compared to the flag.
        QVERIFY(MarkdownHighlighter::isFencedState(
            document.findBlockByNumber(1).userState()));
        QVERIFY(!MarkdownHighlighter::isFencedState(
            document.findBlockByNumber(3).userState()));

        // Past the closing fence, emphasis works again.
        QVERIFY(formatAt(document, 4, 7).fontItalic());
    }

    void strikethroughCaret() {
        const auto markup = MarkdownHighlighter::inlineMarkup(QStringLiteral("a ~~gone~~ b"));
        QCOMPARE(markup.size(), 1);
        QVERIFY(markup.at(0).kind == MarkdownHighlighter::InlineKind::Strikethrough);
        QCOMPARE(markup.at(0).content.start, 4);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(0).markers[0].start, 2);
        QCOMPARE(markup.at(0).markers[0].length, 2);
        QCOMPARE(markup.at(0).markers[1].start, 8);
        QCOMPARE(markup.at(0).markers[1].length, 2);

        // Inside a fence the tildes are visible text, so no range is reported
        // and the caret walks over them one character at a time.
        QVERIFY(MarkdownHighlighter::inlineMarkup(QStringLiteral("a ~~gone~~ b"), true).isEmpty());

        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("a ~~gone~~ b"));
        QVERIFY(formatAt(document, 0, 4).fontStrikeOut());
        QCOMPARE(formatAt(document, 0, 2).fontPointSize(), 1.0);
    }

    void tableMonospace() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral(
            "| a | **b** |\n|---|-------|\n| 1 | 2     |\nplain **b**"));

        const QString row = document.findBlockByNumber(0).text();
        const QVariant families = formatAt(document, 0, 0).fontFamilies();
        QVERIFY(!families.toStringList().isEmpty());
        for (int i = 0; i < row.length(); ++i) {
            QCOMPARE(formatAt(document, 0, i).fontFamilies().toStringList(),
                     families.toStringList());
        }

        // The row above the separator is the header, so it is bold as a whole.
        QCOMPARE(formatAt(document, 0, 2).fontWeight(), int(QFont::Bold));

        // Emphasis inside a body cell is left as source, so columns stay
        // aligned, and no marker is hidden there either.
        QVERIFY(formatAt(document, 2, 2).fontWeight() != int(QFont::Bold));
        QVERIFY(MarkdownHighlighter::inlineMarkup(row).isEmpty());
        QCOMPARE(formatAt(document, 3, 8).fontWeight(), int(QFont::Bold));

        // The separator collapses only where a rule is drawn in its place, so
        // a table with no grid keeps the line between its header and its body.
        QVERIFY(MarkdownHighlighter::isTableSeparator(
            document.findBlockByNumber(1).text()));
        QVERIFY(!qFuzzyCompare(formatAt(document, 1, 1).fontPointSize(), 1.0));
        highlighter.setGriddedRows(QSet<int>{0, 1, 2});
        QCOMPARE(formatAt(document, 1, 1).fontPointSize(), 1.0);
        QCOMPARE(document.findBlockByNumber(0).userState(),
                 int(MarkdownHighlighter::TableRow));

        // Pipes are dimmed away from the content.
        QVERIFY(formatAt(document, 0, 0).foreground() != formatAt(document, 0, 2).foreground());
    }

    // A hidden marker cancels its own advance, which is right for `**` but
    // wrong for a list bullet: it dragged the label a cell to the left and the
    // asterisk items no longer lined up with the dash ones.
    void asteriskBulletKeepsItsCell() {
        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("- item\n* item\n+ item"));

        const auto lineWidth = [&document](int line) {
            return document.findBlockByNumber(line).layout()->lineAt(0).naturalTextWidth();
        };
        // Same text, same width: the asterisk still occupies its cell.
        QCOMPARE(lineWidth(1), lineWidth(0));
        QCOMPARE(lineWidth(2), lineWidth(0));

        // It paints nothing, though, so the drawn dot is all you see. The
        // dot is in the layer behind the text, so the asterisk has to be
        // transparent: painted in the page colour it carves itself out of it.
        const QTextCharFormat marker = formatAt(document, 1, 0);
        QCOMPARE(marker.foreground().color().alpha(), 0);
        QVERIFY(!marker.hasProperty(QTextFormat::FontLetterSpacing));
        QCOMPARE(marker.fontPointSize(), 0.0);

        // A dash is left exactly as written.
        QVERIFY(formatAt(document, 0, 0).foreground().color().alpha() > 0);

        QCOMPARE(MarkdownHighlighter::asteriskBulletColumn(QStringLiteral("* item")), 0);
        QCOMPARE(MarkdownHighlighter::asteriskBulletColumn(QStringLiteral("  * item")), 2);
        QCOMPARE(MarkdownHighlighter::asteriskBulletColumn(QStringLiteral("- item")), -1);
    }

    void taskItemFormats() {
        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("- [x] done\n- [ ] todo"));

        const QBrush markerColor = formatAt(document, 0, 0).foreground();
        QVERIFY(markerColor.style() != Qt::NoBrush);
        QCOMPARE(formatAt(document, 0, 2).foreground(), markerColor);
        QCOMPARE(formatAt(document, 0, 4).foreground(), markerColor);
        QCOMPARE(formatAt(document, 0, 3).fontWeight(), int(QFont::Bold));

        QVERIFY(formatAt(document, 1, 3).fontWeight() != int(QFont::Bold));
        QCOMPARE(formatAt(document, 1, 3).foreground(), markerColor);
        QVERIFY(formatAt(document, 0, 6).foreground() != markerColor);
    }

    void imageMarkers() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("see ![alt text](pic.png) here"));
        QCOMPARE(markup.size(), 1);
        QVERIFY(markup.at(0).kind == MarkdownHighlighter::InlineKind::Image);
        QCOMPARE(markup.at(0).content.start, 6);
        QCOMPARE(markup.at(0).content.length, 8);
        QCOMPARE(markup.at(0).markers[0].start, 4);
        QCOMPARE(markup.at(0).markers[0].length, 2);
        QCOMPARE(markup.at(0).markers[1].start, 14);
        QCOMPARE(markup.at(0).markers[1].length, 10);

        QTextDocument document;
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("see ![alt text](pic.png) here"));
        QVERIFY(formatAt(document, 0, 6).fontUnderline());
        QCOMPARE(formatAt(document, 0, 4).fontPointSize(), 1.0);
    }

    void setextHeadings() {
        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("Title\n=====\n\nSub\n---\n\nbody\n\n---"));

        QCOMPARE(formatAt(document, 0, 0).fontPointSize(), 12.0 * 1.9);
        QCOMPARE(formatAt(document, 3, 0).fontPointSize(), 12.0 * 1.6);

        // The underline reads as a marker, the way a thematic break does.
        QVERIFY(formatAt(document, 1, 0).foreground().style() != Qt::NoBrush);
        QVERIFY(formatAt(document, 4, 0).foreground().style() != Qt::NoBrush);

        // A rule with a blank line above it is a rule, not an underline.
        QCOMPARE(formatAt(document, 6, 0).fontPointSize(), 0.0);
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

        // Four notes, laid out as a folder tree: folders before the notes that
        // sit beside them, each level in name order.
        QCOMPARE(model.count(), 4);
        QCOMPARE(model.rowCount(), 6);
        QVERIFY(!model.truncated());
        QCOMPARE(titlesOf(model),
                 (QStringList{QStringLiteral("projects"), QStringLiteral("deep"),
                              QStringLiteral("Gamma"), QStringLiteral("Beta"),
                              QStringLiteral("Alpha"), QStringLiteral("zeta")}));
        QCOMPARE(depthsOf(model), (QList<int>{0, 1, 2, 1, 0, 0}));
        QVERIFY(roleOf(model, 0, VaultModel::IsDirectoryRole).toBool());
        QVERIFY(roleOf(model, 1, VaultModel::IsDirectoryRole).toBool());
        QVERIFY(!roleOf(model, 2, VaultModel::IsDirectoryRole).toBool());
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

        // The current file is the one the editor has open. A folder never is.
        model.setCurrentPath(roleOf(model, 3, VaultModel::PathRole).toString());
        QCOMPARE(roleOf(model, 0, VaultModel::IsCurrentRole).toBool(), false);
        QCOMPARE(roleOf(model, 3, VaultModel::IsCurrentRole).toBool(), true);
        QCOMPARE(model.rowForPath(model.pathAt(3)), 3);

        // A new note has no name yet, so it starts as a draft beside the vault
        // rather than in it, under a Drafts row in the folder it was made in.
        const QString created = model.createNote();
        QVERIFY(!created.isEmpty());
        QCOMPARE(QFileInfo(created).fileName(), QStringLiteral("draft.md"));
        QCOMPARE(QFileInfo(created).dir().absolutePath(),
                 VaultModel::draftsDirectoryFor(vault.path()));
        QVERIFY(VaultModel::isDraftPath(created));
        QCOMPARE(VaultModel::folderForDraft(created), QDir(vault.path()).canonicalPath());
        QCOMPARE(QFileInfo(model.createNote()).fileName(), QStringLiteral("draft-2.md"));
        QCOMPARE(model.count(), 6);
        QCOMPARE(roleOf(model, 0, VaultModel::TitleRole).toString(), QStringLiteral("Drafts"));
        QVERIFY(roleOf(model, 0, VaultModel::IsDirectoryRole).toBool());
        QCOMPARE(roleOf(model, 0, VaultModel::DepthRole).toInt(), 0);
        QCOMPARE(roleOf(model, 1, VaultModel::IsDraftRole).toBool(), true);
        QCOMPARE(roleOf(model, 1, VaultModel::DepthRole).toInt(), 1);
        // "New note here" on the group means the folder it belongs to, not the
        // hidden directory the drafts are kept in.
        QCOMPARE(model.relativeDirAt(0), QString());

        // A draft made in a subfolder appears in that subfolder's own group.
        const QString nested = model.createNote(QStringLiteral("projects"));
        QCOMPARE(VaultModel::folderForDraft(nested),
                 QDir(vault.path()).absoluteFilePath(QStringLiteral("projects")));
        const int projectsRow = titlesOf(model).indexOf(QStringLiteral("projects"));
        QVERIFY(projectsRow >= 0);
        QCOMPARE(roleOf(model, projectsRow + 1, VaultModel::TitleRole).toString(),
                 QStringLiteral("Drafts"));
        QCOMPARE(roleOf(model, projectsRow + 1, VaultModel::DepthRole).toInt(), 1);
        QCOMPARE(model.relativeDirAt(projectsRow + 1), QStringLiteral("projects"));
        QCOMPARE(roleOf(model, projectsRow + 2, VaultModel::IsDraftRole).toBool(), true);
        QCOMPARE(roleOf(model, projectsRow + 2, VaultModel::DepthRole).toInt(), 2);

        // And the group closes like any other folder.
        const int before = model.rowCount();
        model.toggleExpanded(projectsRow + 1);
        QCOMPARE(model.rowCount(), before - 1);
        model.toggleExpanded(projectsRow + 1);
        QCOMPARE(model.rowCount(), before);
    }

    void vaultModelMovesAndDeletes() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        QVERIFY(writeNote(vault.path(), QStringLiteral("Note.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/Beta.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/deep/Gamma.md")));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.folders(),
                 (QStringList{QStringLiteral("projects"), QStringLiteral("projects/deep")}));

        const QString note = QDir(vault.path()).filePath(QStringLiteral("Note.md"));
        const QString moved = model.moveNote(note, QStringLiteral("projects/deep"));
        QVERIFY(!moved.isEmpty());
        QVERIFY(QFile::exists(moved));
        QVERIFY(!QFile::exists(note));
        QCOMPARE(QFileInfo(moved).dir().dirName(), QStringLiteral("deep"));
        QCOMPARE(model.totalCount(), 3);

        // A name already in the target folder is not overwritten.
        QVERIFY(writeNote(vault.path(), QStringLiteral("Beta.md")));
        model.refresh();
        const QString clash = QDir(vault.path()).filePath(QStringLiteral("Beta.md"));
        const QString settled = model.moveNote(clash, QStringLiteral("projects"));
        QCOMPARE(QFileInfo(settled).fileName(), QStringLiteral("Beta-2.md"));
        QVERIFY(QFile::exists(QDir(vault.path()).filePath(QStringLiteral("projects/Beta.md"))));

        // Nothing outside the vault can be moved or deleted through the model.
        QTemporaryDir elsewhere;
        QVERIFY(elsewhere.isValid());
        QVERIFY(writeNote(elsewhere.path(), QStringLiteral("Outside.md")));
        const QString outside = QDir(elsewhere.path()).filePath(QStringLiteral("Outside.md"));
        QCOMPARE(model.moveNote(outside, QString()), QString());
        QVERIFY(!model.deleteNote(outside));
        QVERIFY(QFile::exists(outside));

        QVERIFY(model.deleteNote(moved));
        QVERIFY(!QFile::exists(moved));
        model.refresh();
        QCOMPARE(model.totalCount(), 3);
    }

    void vaultModelCollapsesFolders() {
        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        QVERIFY(writeNote(vault.path(), QStringLiteral("Root.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/Beta.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("projects/deep/Gamma.md")));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(roleOf(model, 0, VaultModel::IsExpandedRole).toBool());

        // Collapsing takes the whole subtree with it, notes and folders alike.
        model.toggleExpanded(0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.count(), 1);
        QVERIFY(!roleOf(model, 0, VaultModel::IsExpandedRole).toBool());
        QCOMPARE(titlesOf(model),
                 (QStringList{QStringLiteral("projects"), QStringLiteral("Root")}));

        // A note is not a folder, so neither call does anything to it.
        model.toggleExpanded(1);
        QCOMPARE(model.rowCount(), 2);

        // Opening a note inside a collapsed folder opens the way down to it.
        const QString buried = QDir(vault.path())
            .filePath(QStringLiteral("projects/deep/Gamma.md"));
        model.setCurrentPath(buried);
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.rowForPath(QFileInfo(buried).canonicalFilePath()), 2);
        QVERIFY(roleOf(model, 2, VaultModel::IsCurrentRole).toBool());

        // A filter flattens the tree, folders and all.
        model.setFilter(QStringLiteral("mm"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(roleOf(model, 0, VaultModel::TitleRole).toString(),
                 QStringLiteral("Gamma"));
        QCOMPARE(roleOf(model, 0, VaultModel::DepthRole).toInt(), 0);
        QVERIFY(!roleOf(model, 0, VaultModel::IsDirectoryRole).toBool());
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

    // The filter matches a path on its own. This adds what is written inside
    // the notes, through ripgrep when it is installed and grep otherwise.
    void vaultModelSearchesContent() {
        if (QStandardPaths::findExecutable(QStringLiteral("rg")).isEmpty()
                && QStandardPaths::findExecutable(QStringLiteral("grep")).isEmpty()) {
            QSKIP("neither ripgrep nor grep is installed");
        }

        QTemporaryDir vault;
        QVERIFY(vault.isValid());
        const QString path = QDir(vault.path()).filePath(QStringLiteral("minutes.md"));
        QFile note(path);
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("# Minutes\n\nThe word ferroalloy appears only in here.\n");
        note.close();
        QVERIFY(writeNote(vault.path(), QStringLiteral("ferroalloy-by-name.md")));
        QVERIFY(writeNote(vault.path(), QStringLiteral("unrelated.md")));

        VaultModel model;
        model.setRoot(vault.path());
        QCOMPARE(model.count(), 3);

        // The name match is immediate; the content match arrives when the
        // search process finishes.
        model.setFilter(QStringLiteral("ferroalloy"));
        QCOMPARE(model.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 2, 5000);

        const auto titles = titlesOf(model);
        QVERIFY(titles.contains(QStringLiteral("minutes")));
        QVERIFY(titles.contains(QStringLiteral("ferroalloy-by-name")));

        // A name match answers the question; a line buried in a file is a
        // weaker answer, so it goes under a heading of its own below.
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(roleOf(model, 0, VaultModel::TitleRole).toString(),
                 QStringLiteral("ferroalloy-by-name"));
        QVERIFY(model.isHeaderAt(1));
        QCOMPARE(model.titleAt(1), QStringLiteral("Found in text"));
        QCOMPARE(roleOf(model, 2, VaultModel::TitleRole).toString(),
                 QStringLiteral("minutes"));

        // A note found only by its text says so, one found by name does not.
        for (int row = 0; row < model.rowCount(); ++row) {
            const bool byContent = roleOf(model, row, VaultModel::MatchesContentRole).toBool();
            QCOMPARE(byContent, roleOf(model, row, VaultModel::TitleRole).toString()
                                    == QStringLiteral("minutes"));
        }

        // Clearing goes back to everything, with no search left running.
        model.setFilter(QString());
        QCOMPARE(model.count(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!model.searching(), 5000);
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

        // The footer actions are icons, drawn by the same button the editor's
        // own footer uses.
        QObject *newNoteButton = window->findChild<QObject *>(QStringLiteral("newNoteButton"));
        QObject *rootButton = window->findChild<QObject *>(QStringLiteral("vaultRootButton"));
        QVERIFY(newNoteButton);
        QVERIFY(rootButton);
        QCOMPARE(newNoteButton->property("iconName").toString(), QStringLiteral("newnote"));
        QCOMPARE(rootButton->property("iconName").toString(), QStringLiteral("open"));
        QVERIFY(rootButton->property("tooltip").toString().contains(vaultDirectory.path()));

        QSignalSpy rootChangeSpy(sidebar, SIGNAL(rootChangeRequested()));
        QVERIFY(QMetaObject::invokeMethod(rootButton, "clicked"));
        QCOMPARE(rootChangeSpy.count(), 1);
        QCOMPARE(vault.rowCount(), 4);
        QCOMPARE(list->property("count").toInt(), 4);

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
        QCOMPARE(list->property("count").toInt(), 4);

        // Closed until asked for, from the footer icon or Ctrl+L.
        QCOMPARE(window->property("sidebarVisible").toBool(), false);
        QVERIFY(window->findChild<QObject *>(QStringLiteral("sidebarButton")));
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QCOMPARE(window->property("sidebarVisible").toBool(), true);
        QVERIFY(QMetaObject::invokeMethod(window.data(), "toggleSidebar"));
        QCOMPARE(window->property("sidebarVisible").toBool(), false);

        QVERIFY(QMetaObject::invokeMethod(window.data(), "createNote",
                                          Q_ARG(QVariant, QVariant(QString()))));
        QCOMPARE(QFileInfo(backend.fileUrl().toLocalFile()).fileName(),
                 QStringLiteral("draft.md"));
        // The note, plus the draft and the header over it.
        QCOMPARE(list->property("count").toInt(), 6);

        // An edited buffer is marked in the list, and a draft with no file
        // behind it gets a row of its own.
        QObject *sidebarItem = window->findChild<QObject *>(QStringLiteral("vaultSidebar"));
        QVERIFY(sidebarItem);
        QCOMPARE(sidebarItem->property("documentModified").toBool(), false);
        QCOMPARE(sidebarItem->property("hasUntitledDraft").toBool(), false);
        QVERIFY(!backend.untitled());

        QObject *editorItem = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editorItem);
        QVERIFY(QMetaObject::invokeMethod(editorItem, "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("edit "))));
        QVERIFY(backend.modified());
        QCOMPARE(sidebarItem->property("documentModified").toBool(), true);
        QCOMPARE(sidebarItem->property("hasUntitledDraft").toBool(), false);

        // Sidebar width and visibility outlive the window.
        backend.saveSidebarState(false, 320);
        const QVariantMap state = backend.sidebarState();
        QCOMPARE(state.value(QStringLiteral("visible")).toBool(), false);
        QCOMPARE(state.value(QStringLiteral("width")).toInt(), 320);

        // Clicking a checkbox flips it, through the same mutation the keyboard
        // uses, so one undo puts it back.
        QObject *taskEditor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(taskEditor);
        taskEditor->setProperty("text", QStringLiteral("- [ ] a task"));
        QVERIFY(QMetaObject::invokeMethod(taskEditor, "toggleTaskAt",
                                          Q_ARG(QVariant, QVariant(3))));
        QCOMPARE(taskEditor->property("text").toString(), QStringLiteral("- [x] a task"));
        QVERIFY(QMetaObject::invokeMethod(taskEditor, "toggleTaskAt",
                                          Q_ARG(QVariant, QVariant(3))));
        QCOMPARE(taskEditor->property("text").toString(), QStringLiteral("- [ ] a task"));
        // A click anywhere else leaves the text alone.
        QVERIFY(QMetaObject::invokeMethod(taskEditor, "toggleTaskAt",
                                          Q_ARG(QVariant, QVariant(9))));
        QCOMPARE(taskEditor->property("text").toString(), QStringLiteral("- [ ] a task"));
        taskEditor->setProperty("text", QString());

        Q_UNUSED(taskEditor)
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

    void readsATitleOffTheFirstLine() {
        const auto title = [](const char *text) {
            return Backend::titleFromText(QString::fromUtf8(text));
        };
        QCOMPARE(title("# Meeting notes\nBody"), QStringLiteral("Meeting notes"));
        QCOMPARE(title("\n\n   ### Deep heading ###\n"), QStringLiteral("Deep heading"));
        QCOMPARE(title("- [ ] buy milk"), QStringLiteral("buy milk"));
        QCOMPARE(title("2. second thing"), QStringLiteral("second thing"));
        QCOMPARE(title("> quoted opener"), QStringLiteral("quoted opener"));
        QCOMPARE(title("**Bold all through**"), QStringLiteral("Bold all through"));
        QCOMPARE(title("a/b\tc   d"), QStringLiteral("a-b c d"));
        QCOMPARE(title("   \n\t\n"), QString());
        QCOMPARE(title(""), QString());
        // Capped, and the cap does not leave a trailing space behind.
        const QString long_ = Backend::titleFromText(QString(80, QLatin1Char('x')));
        QCOMPARE(long_.size(), 64);

        QVERIFY(Backend::isPlaceholderName(QStringLiteral("untitled.md")));
        QVERIFY(Backend::isPlaceholderName(QStringLiteral("untitled-7.md")));
        QVERIFY(!Backend::isPlaceholderName(QStringLiteral("untitled notes.md")));
        QVERIFY(!Backend::isPlaceholderName(QStringLiteral("Meeting notes.md")));
    }

    // A new note is created as untitled.md because there is nothing to call it
    // yet. The first save, while the file is still empty, names it.
    void namesANewNoteOnItsFirstSave() {
        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        VaultModel vault;
        vault.setRoot(vaultDirectory.path());

        const QString created = vault.createNote();
        QCOMPARE(QFileInfo(created).fileName(), QStringLiteral("draft.md"));
        QVERIFY(VaultModel::isDraftPath(created));

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(created));

        // Before it is saved the sidebar still has a label for it.
        QCOMPARE(backend.placeholderTitle(), QString());
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("# Meeting notes\nBody"))));
        backend.editorTextChanged();
        QCOMPARE(backend.placeholderTitle(), QStringLiteral("Meeting notes"));

        backend.save();
        const QString renamed = vaultDirectory.filePath(QStringLiteral("Meeting notes.md"));
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(renamed));
        QVERIFY(QFile::exists(renamed));
        QVERIFY(!QFile::exists(created));
        QVERIFY(!backend.modified());
        QVERIFY(backend.draftPaths().isEmpty());
        // Named, so the sidebar goes back to using the file name.
        QCOMPARE(backend.placeholderTitle(), QString());

        QFile saved(renamed);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("# Meeting notes\nBody"));
        saved.close();

        // A second save keeps the name, whatever the first line says now.
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("# Something else\n"))));
        backend.editorTextChanged();
        backend.save();
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(renamed));
    }

    // The title has to outlive the buffer: leave the draft for another note and
    // the sidebar still knows what to call it.
    void aDraftKeepsItsTitleWhenYouLeaveIt() {
        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        QVERIFY(writeNote(vaultDirectory.path(), QStringLiteral("Other.md")));
        VaultModel vault;
        vault.setRoot(vaultDirectory.path());
        const QString draft = vault.createNote();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(draft));
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("# Kept title\nbody"))));
        backend.editorTextChanged();
        QCOMPARE(backend.placeholderTitle(), QStringLiteral("Kept title"));

        // Autosaving a draft must not look like somebody else editing it: the
        // open file is watched, and the write replaces it.
        QSignalSpy outside(&backend, &Backend::externalChangeDetected);

        // Moving to another note writes the draft's own file on the way out.
        QSignalSpy written(&backend, &Backend::draftWritten);
        backend.open(QUrl::fromLocalFile(
            QDir(vaultDirectory.path()).filePath(QStringLiteral("Other.md"))));
        QCOMPARE(written.count(), 1);
        QCOMPARE(VaultModel::firstLineOf(draft), QStringLiteral("# Kept title"));

        vault.refresh();
        int draftRow = -1;
        for (int row = 0; row < vault.rowCount(); ++row) {
            if (roleOf(vault, row, VaultModel::IsDraftRole).toBool())
                draftRow = row;
        }
        QVERIFY(draftRow >= 0);
        QCOMPARE(roleOf(vault, draftRow, VaultModel::TitleRole).toString(),
                 QStringLiteral("Kept title"));

        // And the buffer's own label is gone, because the draft is not open.
        QCOMPARE(backend.placeholderTitle(), QString());
        QTest::qWait(200);
        QCOMPARE(outside.count(), 0);
    }

    // Save As on a draft offers the name it would get, not "draft".
    // Unsaved text is kept, not forced on you: there has to be a way back to
    // what is on disk.
    void discardsChangesBackToTheFile() {
        clearRecoverySnapshots();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("note.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("# On disk\n\nthe saved text\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));

        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("scribble "))));
        backend.editorTextChanged();
        QVERIFY(backend.modified());
        QCOMPARE(backend.draftPaths(), QStringList{path});

        backend.discardChanges();
        QVERIFY(!backend.modified());
        QCOMPARE(editor->property("text").toString(),
                 QStringLiteral("# On disk\n\nthe saved text\n"));
        // The draft goes with the text, or the note would come back marked
        // and hand the scribble over again on the next visit.
        QVERIFY(backend.draftPaths().isEmpty());
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(path));

        // The file itself was never written, before or after.
        QFile onDisk(path);
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        QCOMPARE(onDisk.readAll(), QByteArray("# On disk\n\nthe saved text\n"));
    }

    void saveAsProposesTheTitle() {
        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        VaultModel vault;
        vault.setRoot(vaultDirectory.path());
        const QString draft = vault.createNote();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(draft));
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("# Quarterly review\n"))));
        backend.editorTextChanged();

        QSignalSpy asked(&backend, &Backend::saveDialogRequested);
        backend.saveAsDialog();
        QCOMPARE(asked.count(), 1);
        const QUrl proposed = asked.first().first().toUrl();
        QCOMPARE(QFileInfo(proposed.toLocalFile()).fileName(),
                 QStringLiteral("Quarterly review.md"));
        // In the folder the draft belongs to, not the drafts folder itself.
        QCOMPARE(QFileInfo(proposed.toLocalFile()).dir().canonicalPath(),
                 QDir(vaultDirectory.path()).canonicalPath());
    }

    void leavesANoteYouNamedUntitledAlone() {
        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        const QString path = vaultDirectory.filePath(QStringLiteral("untitled.md"));
        QFile seed(path);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("# Deliberately untitled\n");
        seed.close();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(path));
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("more "))));
        backend.editorTextChanged();
        backend.save();

        // The file had content of its own, so the name was a choice, not a
        // placeholder waiting to be filled in.
        QCOMPARE(backend.fileUrl(), QUrl::fromLocalFile(path));
        QVERIFY(QFile::exists(path));
    }

    void doesNotOverwriteANoteWithTheSameTitle() {
        QTemporaryDir vaultDirectory;
        QVERIFY(vaultDirectory.isValid());
        const QString taken = vaultDirectory.filePath(QStringLiteral("Standup.md"));
        QFile occupant(taken);
        QVERIFY(occupant.open(QIODevice::WriteOnly));
        occupant.write("mine\n");
        occupant.close();

        VaultModel vault;
        vault.setRoot(vaultDirectory.path());
        const QString created = vault.createNote();

        QQmlEngine engine;
        QScopedPointer<QObject> editor(createEditor(&engine));
        QVERIFY(editor);
        Backend backend;
        backend.attachDocument(editor->property("textDocument").value<QObject *>());
        backend.open(QUrl::fromLocalFile(created));
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "insert", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("# Standup"))));
        backend.editorTextChanged();
        backend.save();

        QCOMPARE(backend.fileUrl(),
                 QUrl::fromLocalFile(vaultDirectory.filePath(QStringLiteral("Standup-2.md"))));
        QFile untouched(taken);
        QVERIFY(untouched.open(QIODevice::ReadOnly));
        QCOMPARE(untouched.readAll(), QByteArray("mine\n"));
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

    // Backticks fence off Markdown. Two code spans on one line used to pair
    // their underscores across the gap and italicise everything between them.
    void codeSpansAreNotMarkdown() {
        const auto kinds = [](const QString &line) {
            QList<int> found;
            for (const auto &item : MarkdownHighlighter::inlineMarkup(line))
                found.append(int(item.kind));
            return found;
        };
        // A code span is markup of its own, and nothing else may claim any
        // part of one.
        QCOMPARE(kinds(QStringLiteral("### `contact_id` and `test_id`")),
                 (QList<int>{int(MarkdownHighlighter::InlineKind::Heading),
                             int(MarkdownHighlighter::InlineKind::Code),
                             int(MarkdownHighlighter::InlineKind::Code)}));
        QCOMPARE(kinds(QStringLiteral("`a *b* c`")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Code)});
        QCOMPARE(kinds(QStringLiteral("`[a](b)`")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Code)});
        QCOMPARE(kinds(QStringLiteral("`a ~~b~~ c`")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Code)});

        // An underscore inside a word is a character, not a marker.
        QVERIFY(kinds(QStringLiteral("snake_case_name here")).isEmpty());
        QVERIFY(kinds(QStringLiteral("call get_user_id() twice")).isEmpty());
        // Real emphasis still works, both markers.
        QCOMPARE(kinds(QStringLiteral("an _italic_ word")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Italic)});
        QCOMPARE(kinds(QStringLiteral("an *italic* word")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Italic)});
        QCOMPARE(kinds(QStringLiteral("__bold__ start")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::Bold)});
        QCOMPARE(kinds(QStringLiteral("___both___ start")),
                 QList<int>{int(MarkdownHighlighter::InlineKind::BoldItalic)});
    }

    // A heading made of nothing but code came out as a plain heading: the
    // heading format was written over the code span rather than under it.
    void codeInAHeadingKeepsBoth() {
        QTextDocument document;
        document.setDefaultFont(bodyFont());
        MarkdownHighlighter highlighter(&document);
        setDocumentText(document, QStringLiteral("### `client_id`\n"
                                                 "# plain heading\n"
                                                 "body `code` here"));

        const QTextCharFormat heading = formatAt(document, 1, 3);
        const QTextCharFormat headingCode = formatAt(document, 0, 5);
        const QTextCharFormat bodyCode = formatAt(document, 2, 6);

        // The code span carries the chip background and the monospace family.
        QVERIFY(headingCode.background() != heading.background());
        QCOMPARE(headingCode.background(), bodyCode.background());
        QCOMPARE(headingCode.fontFamilies().toStringList(),
                 bodyCode.fontFamilies().toStringList());
        // And it is still heading-sized, which is larger than code in prose.
        QVERIFY(headingCode.fontPointSize() > bodyCode.fontPointSize());

        // The backticks keep their cells and paint nothing, so the chip has a
        // space of its own either side of the code rather than clamping to it.
        const QTextCharFormat tick = formatAt(document, 2, 5);
        QCOMPARE(tick.background(), bodyCode.background());
        QCOMPARE(tick.foreground().color().alpha(), 0);
        QVERIFY(!tick.hasProperty(QTextFormat::FontLetterSpacing));
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

    // A highlighter's formats only reach the block layout when the document is
    // laid out, so ask for a layout before reading any of them back.
    static void setDocumentText(QTextDocument &document, const QString &text) {
        document.setPlainText(text);
        document.setTextWidth(600);
        (void)document.size();
    }

    // Recovery slots are shared by every Backend in the process, so a test that
    // cares which snapshot is restored has to start from an empty set.
    static void clearRecoverySnapshots() {
        const QString stateDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QFileInfoList leftovers = QDir(stateDirectory).entryInfoList(
            QStringList{QStringLiteral("recovery-*")}, QDir::Files);
        for (const QFileInfo &leftover : leftovers)
            QFile::remove(leftover.absoluteFilePath());
    }

    static QFont bodyFont() {
        QFont font(QStringLiteral("monospace"));
        font.setPointSizeF(12.0);
        return font;
    }

    // Syntax highlighting lives in the block layout's format ranges, not in the
    // characters' own formats, so read it back from there.
    static QTextCharFormat formatAt(const QTextDocument &document, int blockNumber,
                                    int position) {
        QTextCharFormat merged;
        const QTextBlock block = document.findBlockByNumber(blockNumber);
        if (!block.isValid() || !block.layout())
            return merged;
        const QList<QTextLayout::FormatRange> ranges = block.layout()->formats();
        for (const QTextLayout::FormatRange &range : ranges) {
            if (position >= range.start && position < range.start + range.length)
                merged.merge(range.format);
        }
        return merged;
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
        for (int row = 0; row < model.rowCount(); ++row)
            titles.append(roleOf(model, row, VaultModel::TitleRole).toString());
        return titles;
    }

    static QList<int> depthsOf(const VaultModel &model) {
        QList<int> depths;
        for (int row = 0; row < model.rowCount(); ++row)
            depths.append(roleOf(model, row, VaultModel::DepthRole).toInt());
        return depths;
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

QTEST_MAIN(OmanotesTest)
#include "tst_omanotes.moc"
