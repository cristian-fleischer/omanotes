#include <QtTest>
#include <QFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QColor>
#include <QFontDatabase>
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
        // nothing is drawn through a pipe that is not there.
        const QVariantMap ragged = regions.at(1).toMap();
        QVERIFY(ragged.value(QStringLiteral("columns")).toList().size() < 3);
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
        backend.saveViewState(1.0, false, QString());
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

        // Undoing puts both the text and the line heights back.
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "undo"));
        backend.editorTextChanged();
        QCOMPARE(lineHeight(0), 140.0);
        QCOMPARE(lineHeight(2), 120.0);
        QCOMPARE(backend.fencedCodeRegions().size(), 1);
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

        // The separator collapses; a rule is drawn where it was.
        QVERIFY(MarkdownHighlighter::isTableSeparator(
            document.findBlockByNumber(1).text()));
        QCOMPARE(formatAt(document, 1, 1).fontPointSize(), 1.0);
        QCOMPARE(document.findBlockByNumber(0).userState(),
                 int(MarkdownHighlighter::TableRow));

        // Pipes are dimmed away from the content.
        QVERIFY(formatAt(document, 0, 0).foreground() != formatAt(document, 0, 2).foreground());
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

        const QString created = model.createNote();
        QVERIFY(!created.isEmpty());
        QCOMPARE(QFileInfo(created).fileName(), QStringLiteral("untitled.md"));
        QCOMPARE(QFileInfo(model.createNote()).fileName(), QStringLiteral("untitled-2.md"));
        QCOMPARE(model.count(), 6);
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

        QVERIFY(QMetaObject::invokeMethod(window.data(), "createNote"));
        QCOMPARE(QFileInfo(backend.fileUrl().toLocalFile()).fileName(),
                 QStringLiteral("untitled.md"));
        QCOMPARE(list->property("count").toInt(), 5);

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

QTEST_MAIN(OmanoteTest)
#include "tst_omanote.moc"
