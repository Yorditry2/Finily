#include "TestHelpers.h"

#include "core/Types.h"
#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/LibraryStore.h"
#include "engine/NotebookDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace finily;
using finily::test::ScopedTestPaths;
using finily::test::makeLineStroke;
using finily::test::makeText;

class TestLibraryStore : public QObject {
    Q_OBJECT

private slots:
    void sanitizeAndUniquePath() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QCOMPARE(LibraryStore::sanitizeBaseName(QStringLiteral("  Álgebra / Sem:4?  ")),
                 QStringLiteral("Álgebra Sem4"));

        const QString first = LibraryStore::uniqueNotebookPath(dir.path(), QStringLiteral("Tema 1"));
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("Tema 1.finily"));
        QFile f1(first);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.close();

        const QString second = LibraryStore::uniqueNotebookPath(dir.path(), QStringLiteral("Tema 1"));
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("Tema 1 (2).finily"));
        QFile f2(second);
        QVERIFY(f2.open(QIODevice::WriteOnly));
        f2.close();

        const QString third = LibraryStore::uniqueNotebookPath(dir.path(), QStringLiteral("Tema 1"));
        QCOMPARE(QFileInfo(third).fileName(), QStringLiteral("Tema 1 (3).finily"));
    }

    void a4LandscapeGeometryAndSerialization() {
        const QRectF sheet = sheetRectForPageFormat(PageFormat::A4Landscape);
        QCOMPARE(sheet.width(), 1123.0);
        QCOMPARE(sheet.height(), 794.0);

        const QSize viewSize(1360, 840);
        const Viewport vp = fittedSheetViewport(PageFormat::A4Landscape, viewSize);
        QVERIFY(vp.zoom > 0.3 && vp.zoom <= 5.0);
        const double centerX = sheet.center().x() * vp.zoom + vp.x;
        const double centerY = sheet.center().y() * vp.zoom + vp.y;
        QVERIFY(std::abs(centerX - viewSize.width() / 2.0) < 1e-6);
        QVERIFY(std::abs(centerY - viewSize.height() / 2.0) < 1e-6);

        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        const QString nbPath = paths.path(QStringLiteral("landscape.finily"));

        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(nbPath, ThemeId::PaperLight, BackgroundTemplate::Ruled,
                                   PageFormat::A4Landscape, vp, ts, 0.70, CoverStyle{4}));
        QCOMPARE(doc.pageFormat(), PageFormat::A4Landscape);
        QVERIFY(std::abs(doc.templateScale() - 0.70) < 1e-4);
        QCOMPARE(doc.coverStyle().colorIndex, 4);

        const NotebookFileMeta meta = readNotebookMetadata(nbPath);
        QVERIFY(meta.found);
        QCOMPARE(meta.pageFormat, PageFormat::A4Landscape);
        QVERIFY(std::abs(meta.templateScale - 0.70) < 1e-4);
        QCOMPARE(meta.coverColorIndex, 4);

        NotebookDocument reloaded;
        QVERIFY(reloaded.loadFromFile(nbPath));
        QCOMPARE(reloaded.pageFormat(), PageFormat::A4Landscape);
        QVERIFY(std::abs(reloaded.templateScale() - 0.70) < 1e-4);
        QCOMPARE(reloaded.coverStyle().colorIndex, 4);

        const QString pdfPath = paths.path(QStringLiteral("landscape.pdf"));
        QVERIFY(reloaded.exportToPdf(pdfPath));
        QFile pdfFile(pdfPath);
        QVERIFY(pdfFile.exists());
        QVERIFY(pdfFile.open(QIODevice::ReadOnly));
        QCOMPARE(pdfFile.read(5), QByteArrayLiteral("%PDF-"));
        QVERIFY(pdfFile.size() > 200);
    }

    void libraryStoreListRenameDeleteAndCoverPersistence() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        QTemporaryDir libDir;
        QVERIFY(libDir.isValid());

        const QString nb1Path = libDir.filePath(QStringLiteral("Pizarra 1.finily"));
        const QString nb2Path = libDir.filePath(QStringLiteral("Cuaderno 2.finily"));
        NotebookDocument::DocumentToolSettings ts;

        NotebookDocument d1;
        QVERIFY(d1.createNotebook(nb1Path, ThemeId::DarkBoard, BackgroundTemplate::Dots,
                                  PageFormat::Infinite, Viewport{}, ts, 1.45, CoverStyle{-1}));
        d1.strokes().push_back(makeLineStroke(QPointF(0, 0), QPointF(120, 40)));
        d1.markDirty();
        QVERIFY(d1.saveToFile(nb1Path, ts));

        NotebookDocument d2;
        QVERIFY(d2.createNotebook(nb2Path, ThemeId::PaperCream, BackgroundTemplate::Grid,
                                  PageFormat::A4, Viewport{}, ts, 0.70, CoverStyle{2}));

        LibraryStore::invalidateCache();
        QVector<NotebookMeta> items = LibraryStore::listNotebooksInDirectories({libDir.path()});
        QCOMPARE(items.size(), 2);

        const auto findByTitle = [&](const QString& title) -> std::optional<NotebookMeta> {
            for (const auto& it : items) {
                if (it.title == title)
                    return it;
            }
            return std::nullopt;
        };
        const auto m1 = findByTitle(QStringLiteral("Pizarra 1"));
        const auto m2 = findByTitle(QStringLiteral("Cuaderno 2"));
        QVERIFY(m1.has_value());
        QVERIFY(m2.has_value());
        QCOMPARE(m1->pageFormat, PageFormat::Infinite);
        QVERIFY(std::abs(m1->templateScale - 1.45) < 1e-4);
        QVERIFY(!m1->preview.isNull());
        QCOMPARE(m2->pageFormat, PageFormat::A4);
        QCOMPARE(m2->coverColorIndex, 2);
        QVERIFY(std::abs(m2->templateScale - 0.70) < 1e-4);

        QVERIFY(LibraryStore::setCoverColor(nb2Path, 5));
        QCOMPARE(readNotebookMetadata(nb2Path).coverColorIndex, 5);

        QString renamedPath;
        QVERIFY(LibraryStore::renameNotebook(nb2Path, QStringLiteral("Geometría 2"), &renamedPath));
        QVERIFY(!QFileInfo::exists(nb2Path));
        QVERIFY(QFileInfo::exists(renamedPath));

        // Borrar QSettings de portadas verifica que el color persiste dentro del .finily.
        appSettings()->remove(QStringLiteral("covers"));
        LibraryStore::invalidateCache();
        items = LibraryStore::listNotebooksInDirectories({libDir.path()});
        const auto renamedMeta = findByTitle(QStringLiteral("Geometría 2"));
        QVERIFY(renamedMeta.has_value());
        QCOMPARE(renamedMeta->coverColorIndex, 5);

        QVERIFY(LibraryStore::deleteNotebook(renamedPath));
        QVERIFY(!QFileInfo::exists(renamedPath));
        items = LibraryStore::listNotebooksInDirectories({libDir.path()});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().title, QStringLiteral("Pizarra 1"));
    }

    void quickNoteSpecAndHasAnyContent() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());

        NewDocumentSpec saved;
        saved.kind = DocumentKind::Board;
        saved.theme = ThemeId::GreenBoard;
        saved.tpl = BackgroundTemplate::Ruled;
        saved.templateScale = 1.45;
        LibraryStore::saveLastUsedSpec(saved);

        const NewDocumentSpec qn = LibraryStore::lastUsedQuickNoteSpec();
        QCOMPARE(qn.kind, DocumentKind::QuickNote);
        QCOMPARE(qn.theme, ThemeId::GreenBoard);
        QCOMPARE(qn.tpl, BackgroundTemplate::Ruled);
        QVERIFY(std::abs(qn.templateScale - 1.45) < 1e-4);
        QCOMPARE(qn.format, PageFormat::Infinite);
        QVERIFY(qn.ephemeralIfEmpty);
        QVERIFY(!qn.title.isEmpty());

        // Plantillas retiradas en UI caen de forma segura a Puntos al crear Nota rápida.
        writeAppSetting(QStringLiteral("home/lastTemplate"), int(BackgroundTemplate::Cornell));
        const NewDocumentSpec fallback = LibraryStore::lastUsedQuickNoteSpec();
        QCOMPARE(fallback.tpl, BackgroundTemplate::Dots);

        NotebookDocument doc;
        doc.configureBlank(ThemeId::DarkBoard, BackgroundTemplate::Grid, PageFormat::Infinite,
                           1.0, CoverStyle{-1}, Viewport{});
        QVERIFY(!doc.hasAnyContent());
        QVERIFY(!doc.isDirty());

        doc.texts().push_back(makeText(QPointF(10.0, 20.0), QStringLiteral("Apunte rápido")));
        QVERIFY(doc.hasAnyContent());

        QFile::remove(autosavePath());
        QVERIFY(!LibraryStore::hasRecoverableDraft());
        const NotebookDocument::DocumentToolSettings ts;
        const auto emptyJob = doc.prepareAutosave(ts, std::nullopt, false);
        QVERIFY(emptyJob.pages.size() == 1);

        // Un autosave vacío sin contenido no debe activar "Continuar borrador".
        NotebookDocument emptyDoc;
        auto emptySettings = documentSettings(ShapeKind::Triangle, true, true, false,
                                              PressureProfile::Light, ThemeId::DarkBoard,
                                              BackgroundTemplate::Dots, Viewport{},
                                              PageFormat::Infinite);
        emptySettings.insert(QStringLiteral("sessionFilePath"), QString());
        emptySettings.insert(QStringLiteral("sessionDirty"), false);
        QVERIFY(serializeDocumentToFile(autosavePath(), {PageData{}}, 0, emptySettings));
        QVERIFY(!LibraryStore::hasRecoverableDraft());

        doc.markDirty();
        const auto draftJob = doc.prepareAutosave(ts, std::nullopt, false);
        QVERIFY(NotebookDocument::executeAutosaveJob(draftJob));
        QVERIFY(LibraryStore::hasRecoverableDraft());
        QFile::remove(autosavePath());
    }

    void coverIconRoundTrip() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        const QString nbPath = paths.path(QStringLiteral("icon_cover.finily"));

        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(nbPath, ThemeId::PaperLight, BackgroundTemplate::Grid,
                                   PageFormat::A4, Viewport{}, ts, 1.0, CoverStyle{3, 5}));
        QCOMPARE(doc.coverStyle().colorIndex, 3);
        QCOMPARE(doc.coverStyle().iconIndex, 5);

        const NotebookFileMeta meta = readNotebookMetadata(nbPath);
        QVERIFY(meta.found);
        QCOMPARE(meta.coverColorIndex, 3);
        QCOMPARE(meta.coverIconIndex, 5);

        NotebookDocument reloaded;
        QVERIFY(reloaded.loadFromFile(nbPath));
        QCOMPARE(reloaded.coverStyle().colorIndex, 3);
        QCOMPARE(reloaded.coverStyle().iconIndex, 5);

        QVERIFY(LibraryStore::setCoverStyle(nbPath, CoverStyle{6, 2}));
        const NotebookFileMeta updated = readNotebookMetadata(nbPath);
        QCOMPARE(updated.coverColorIndex, 6);
        QCOMPARE(updated.coverIconIndex, 2);
    }

    void foldersSubfoldersAndMoveNotebook() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        QTemporaryDir libDir;
        QVERIFY(libDir.isValid());

        QString rootRel;
        QVERIFY(LibraryStore::createFolderInDirectory(libDir.path(), QString(),
                                                      QStringLiteral("Universidad"), 1, 2, &rootRel));
        QCOMPARE(rootRel, QStringLiteral("Universidad"));

        QString subRel;
        QVERIFY(LibraryStore::createFolderInDirectory(libDir.path(), rootRel,
                                                      QStringLiteral("Álgebra"), 4, 5, &subRel));
        QCOMPARE(subRel, QStringLiteral("Universidad/Álgebra"));

        QVector<FolderMeta> tree = LibraryStore::listAllFoldersTreeInDirectories({libDir.path()});
        QCOMPARE(tree.size(), 2);
        QCOMPARE(tree[0].relativePath, QStringLiteral("Universidad"));
        QCOMPARE(tree[0].depth, 0);
        QCOMPARE(tree[0].colorIndex, 1);
        QCOMPARE(tree[0].iconIndex, 2);
        QCOMPARE(tree[1].relativePath, QStringLiteral("Universidad/Álgebra"));
        QCOMPARE(tree[1].depth, 1);
        QCOMPARE(tree[1].colorIndex, 4);
        QCOMPARE(tree[1].iconIndex, 5);

        const QString nbRoot = libDir.filePath(QStringLiteral("Guía 1.finily"));
        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(nbRoot, ThemeId::PaperLight, BackgroundTemplate::Grid,
                                   PageFormat::A4, Viewport{}, ts, 1.0, CoverStyle{0, 1}));

        QString movedPath;
        QVERIFY(LibraryStore::moveNotebookToDirectory(
            nbRoot, QDir(libDir.path()).filePath(subRel), &movedPath));
        QVERIFY(!QFileInfo::exists(nbRoot));
        QVERIFY(QFileInfo::exists(movedPath));

        LibraryStore::invalidateCache();
        const QVector<NotebookMeta> recursive =
            LibraryStore::listAllNotebooksRecursiveInDirectories({libDir.path()});
        QCOMPARE(recursive.size(), 1);
        QCOMPARE(recursive[0].folderRelativePath, QStringLiteral("Universidad/Álgebra"));
        QCOMPARE(recursive[0].coverIconIndex, 1);

        tree = LibraryStore::listAllFoldersTreeInDirectories({libDir.path()});
        QCOMPARE(tree[0].documentCount, 1);
        QCOMPARE(tree[1].documentCount, 1);

        QVERIFY(LibraryStore::updateFolderStyleInDirectory(libDir.path(), rootRel, 6, 7));
        QString renamedRel, oldAbs, newAbs;
        QVERIFY(LibraryStore::renameFolderInDirectory(
            libDir.path(), rootRel, QStringLiteral("Facultad"), &renamedRel, &oldAbs, &newAbs));
        QCOMPARE(renamedRel, QStringLiteral("Facultad"));

        LibraryStore::invalidateCache();
        const QVector<NotebookMeta> afterRename =
            LibraryStore::listAllNotebooksRecursiveInDirectories({libDir.path()});
        QCOMPARE(afterRename.size(), 1);
        QCOMPARE(afterRename[0].folderRelativePath, QStringLiteral("Facultad/Álgebra"));

        QString deletedAbs;
        QVERIFY(LibraryStore::deleteFolderInDirectory(libDir.path(), renamedRel, &deletedAbs));
        QVERIFY(!QFileInfo::exists(deletedAbs));
        QCOMPARE(LibraryStore::listAllFoldersTreeInDirectories({libDir.path()}).size(), 0);
        QCOMPARE(LibraryStore::listAllNotebooksRecursiveInDirectories({libDir.path()}).size(), 0);
    }
};

QTEST_MAIN(TestLibraryStore)
#include "test_library_store.moc"
