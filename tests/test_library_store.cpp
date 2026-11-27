#include "TestHelpers.h"

#include "core/Types.h"
#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/LibraryStore.h"
#include "engine/NotebookDocument.h"

#include <QDir>
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

namespace {

class ScopedLibraryDirEnv {
public:
    explicit ScopedLibraryDirEnv(const QString& dir)
        : m_hadPrev(qEnvironmentVariableIsSet("FINILY_LIBRARY_DIR"))
        , m_prev(qgetenv("FINILY_LIBRARY_DIR")) {
        if (dir.isEmpty()) {
            qunsetenv("FINILY_LIBRARY_DIR");
        } else {
#ifdef Q_OS_WIN
            qputenv("FINILY_LIBRARY_DIR", dir.toLocal8Bit());
#else
            qputenv("FINILY_LIBRARY_DIR", dir.toUtf8());
#endif
        }
    }

    ~ScopedLibraryDirEnv() {
        if (m_hadPrev)
            qputenv("FINILY_LIBRARY_DIR", m_prev);
        else
            qunsetenv("FINILY_LIBRARY_DIR");
    }

    ScopedLibraryDirEnv(const ScopedLibraryDirEnv&) = delete;
    ScopedLibraryDirEnv& operator=(const ScopedLibraryDirEnv&) = delete;

private:
    bool m_hadPrev = false;
    QByteArray m_prev;
};

} // namespace

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

    void trashMoveRestoreAndPurge() {
        ScopedTestPaths paths;
        QTemporaryDir lib;
        QVERIFY(lib.isValid());
        ScopedLibraryDirEnv env(lib.path());
        const QString root = LibraryStore::libraryDirectories().first();
        QCOMPARE(root, lib.path());

        QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("Apuntes/Clases"))));
        const QString doc = QDir(root).filePath(QStringLiteral("Apuntes/Tema.finily"));
        QFile f(doc);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();

        QString docId;
        QVERIFY(LibraryStore::moveToTrash(doc, &docId));
        QVERIFY(!QFileInfo::exists(doc));
        QString folderId;
        QVERIFY(LibraryStore::moveToTrash(QDir(root).filePath(QStringLiteral("Apuntes/Clases")), &folderId));

        // La papelera queda oculta para la biblioteca.
        QVERIFY(LibraryStore::listFoldersInDirectories({root}, QStringLiteral("Apuntes")).isEmpty());
        const auto items = LibraryStore::listTrash();
        QCOMPARE(items.size(), 2);
        for (const TrashEntry& e : items)
            QCOMPARE(e.originalRelative, QStringLiteral("Apuntes"));

        // Restaurar con conflicto de nombre -> «Tema (2)».
        QFile clash(doc);
        QVERIFY(clash.open(QIODevice::WriteOnly));
        clash.close();
        QString restored;
        QVERIFY(LibraryStore::restoreFromTrash(docId, &restored));
        QCOMPARE(QFileInfo(restored).fileName(), QStringLiteral("Tema (2).finily"));
        QCOMPARE(LibraryStore::listTrash().size(), 1);

        QVERIFY(!LibraryStore::restoreFromTrash(QStringLiteral("../x")));
        QVERIFY(!LibraryStore::restoreFromTrash(QStringLiteral("..\\x")));
        QCOMPARE(LibraryStore::purgeExpiredTrash(30), 0);
        QCOMPARE(LibraryStore::purgeExpiredTrash(), 0);

        LibraryStore::setTrashRetentionDays(0);
        QCOMPARE(LibraryStore::trashRetentionDays(), 0);
        const auto remaining = LibraryStore::listTrash();
        QCOMPARE(remaining.size(), 1);
        const QString trashMetaPath = QDir(QFileInfo(remaining.first().itemPath).absolutePath())
                                          .filePath(QStringLiteral(".finily-trash.json"));
        {
            QFile tf(trashMetaPath);
            QVERIFY(tf.open(QIODevice::ReadOnly));
            QJsonObject obj = QJsonDocument::fromJson(tf.readAll()).object();
            tf.close();
            obj.insert(QStringLiteral("deletedAt"),
                       QDateTime::currentDateTimeUtc().addDays(-40).toString(Qt::ISODate));
            QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Truncate));
            tf.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            tf.close();
        }
        // Retención 0 = nunca purgar automáticamente.
        QCOMPARE(LibraryStore::purgeExpiredTrash(), 0);
        LibraryStore::setTrashRetentionDays(30);
        QCOMPARE(LibraryStore::purgeExpiredTrash(), 1);
        QVERIFY(LibraryStore::listTrash().isEmpty());
    }

    void a5AndLetterPageFormatsRoundTrip() {
        QVERIFY(isValidPageFormat(int(PageFormat::A5)));
        QVERIFY(isValidPageFormat(int(PageFormat::Letter)));
        QVERIFY(isValidPageFormat(int(PageFormat::A5Landscape)));
        QVERIFY(isValidPageFormat(int(PageFormat::LetterLandscape)));
        QVERIFY(isValidPageFormat(int(PageFormat::Legal)));
        QVERIFY(isValidPageFormat(int(PageFormat::LegalLandscape)));
        QVERIFY(isValidPageFormat(int(PageFormat::Square)));
        QVERIFY(!isValidPageFormat(int(PageFormat::Square) + 1));

        QCOMPARE(sheetRectForPageFormat(PageFormat::A5), QRectF(-280.0, -397.0, 560.0, 794.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::A5Landscape), QRectF(-397.0, -280.0, 794.0, 560.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::Letter), QRectF(-408.0, -528.0, 816.0, 1056.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::LetterLandscape), QRectF(-528.0, -408.0, 1056.0, 816.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::Legal), QRectF(-408.0, -672.0, 816.0, 1344.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::LegalLandscape), QRectF(-672.0, -408.0, 1344.0, 816.0));
        QCOMPARE(sheetRectForPageFormat(PageFormat::Square), QRectF(-450.0, -450.0, 900.0, 900.0));

        const QSize viewSize(1280, 820);
        for (PageFormat fmt : {PageFormat::A5, PageFormat::A5Landscape, PageFormat::Letter,
                               PageFormat::LetterLandscape, PageFormat::Legal,
                               PageFormat::LegalLandscape, PageFormat::Square}) {
            const QRectF sheet = sheetRectForPageFormat(fmt);
            const Viewport vp = fittedSheetViewport(fmt, viewSize);
            QVERIFY(vp.zoom >= 0.3 && vp.zoom <= 5.0);
            const double centerX = sheet.center().x() * vp.zoom + vp.x;
            QVERIFY(std::abs(centerX - viewSize.width() / 2.0) < 1e-6);
        }

        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        NotebookDocument::DocumentToolSettings ts;
        for (PageFormat fmt : {PageFormat::A5, PageFormat::Letter, PageFormat::Legal, PageFormat::Square}) {
            const QString nbPath = paths.path(QStringLiteral("fmt_%1.finily").arg(int(fmt)));
            NotebookDocument doc;
            QVERIFY(doc.createNotebook(nbPath, ThemeId::PaperLight, BackgroundTemplate::Ruled,
                                       fmt, fittedSheetViewport(fmt, viewSize), ts, 1.0));
            QCOMPARE(doc.pageFormat(), fmt);

            const NotebookFileMeta meta = readNotebookMetadata(nbPath);
            QVERIFY(meta.found);
            QCOMPARE(meta.pageFormat, fmt);

            NotebookDocument reloaded;
            QVERIFY(reloaded.loadFromFile(nbPath));
            QCOMPARE(reloaded.pageFormat(), fmt);

            const QString pdfPath = paths.path(QStringLiteral("fmt_%1.pdf").arg(int(fmt)));
            QVERIFY(reloaded.exportToPdf(pdfPath));
            QFile pdfFile(pdfPath);
            QVERIFY(pdfFile.open(QIODevice::ReadOnly));
            QCOMPARE(pdfFile.read(5), QByteArrayLiteral("%PDF-"));
        }
    }

    void customBackgroundLuminanceAndRoundTrip() {
        const ThemeConfig darkFallback = effectiveThemeConfig(ThemeId::PaperLight, QColor());
        QCOMPARE(darkFallback.background, getThemeConfig(ThemeId::PaperLight).background);

        const ThemeConfig darkCustom = effectiveThemeConfig(ThemeId::PaperLight, QColor("#14213d"));
        QVERIFY(darkCustom.isDark);
        QCOMPARE(darkCustom.background, QColor("#14213d"));
        QCOMPARE(darkCustom.defaultInk, QColor("#f8fafc"));

        const ThemeConfig lightCustom = effectiveThemeConfig(ThemeId::DarkBoard, QColor("#fef3c7"));
        QVERIFY(!lightCustom.isDark);
        QCOMPARE(lightCustom.background, QColor("#fef3c7"));
        QCOMPARE(lightCustom.defaultInk, QColor("#0f172a"));

        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        const QString nbPath = paths.path(QStringLiteral("custom_bg.finily"));

        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(nbPath, ThemeId::DarkBoard, BackgroundTemplate::Dots,
                                   PageFormat::Infinite, Viewport{}, ts, 1.0, CoverStyle{-1, 0},
                                   QColor("#1e293b")));
        QCOMPARE(doc.customBackground(), QColor("#1e293b"));
        for (int i = 0; i < 60; ++i)
            doc.strokes().push_back(makeLineStroke(QPointF(i * 10.0, 0.0), QPointF(i * 10.0 + 30.0, 20.0), 12));
        doc.markDirty();
        QVERIFY(doc.saveToFile(nbPath, ts));

        const NotebookFileMeta fastMeta = readNotebookMetadata(nbPath);
        QVERIFY(fastMeta.found);
        QCOMPARE(fastMeta.customBackground, QColor("#1e293b"));

        NotebookDocument reloaded;
        QVERIFY(reloaded.loadFromFile(nbPath));
        QCOMPARE(reloaded.customBackground(), QColor("#1e293b"));

        const NotebookSession exported = reloaded.exportSession();
        QCOMPARE(exported.customBackground, QColor("#1e293b"));
        NotebookDocument imported;
        imported.importSession(exported);
        QCOMPARE(imported.customBackground(), QColor("#1e293b"));

        QVERIFY(LibraryStore::setBoardStyle(nbPath, ThemeId::PaperCream, QColor("#e2e8f0"),
                                            BackgroundTemplate::Grid, 1.25));
        const NotebookFileMeta updated = readNotebookMetadata(nbPath);
        QCOMPARE(updated.themeId, ThemeId::PaperCream);
        QCOMPARE(updated.customBackground, QColor("#e2e8f0"));
        QCOMPARE(updated.tpl, BackgroundTemplate::Grid);
        QVERIFY(std::abs(updated.templateScale - 1.25) < 1e-4);

        QVERIFY(LibraryStore::setBoardStyle(nbPath, ThemeId::DarkBoard, QColor(),
                                            BackgroundTemplate::Blank, 1.0));
        QVERIFY(!readNotebookMetadata(nbPath).customBackground.isValid());
    }

    void duplicateMoveFolderAndExportPdf() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        QTemporaryDir customRoot;
        QVERIFY(customRoot.isValid());
        ScopedLibraryDirEnv clearEnv(QString{});

        LibraryStore::setCustomLibraryDirectory(customRoot.path());
        QCOMPARE(LibraryStore::customLibraryDirectory(), QDir::cleanPath(customRoot.path()));
        QCOMPARE(LibraryStore::libraryDirectories(), QStringList{QDir::cleanPath(customRoot.path())});

        QString uniRel, algRel, archiveRel;
        QVERIFY(LibraryStore::createFolder(QString(), QStringLiteral("Uni"), 1, 0, &uniRel));
        QVERIFY(LibraryStore::createFolder(uniRel, QStringLiteral("Álgebra"), 2, 1, &algRel));
        QVERIFY(LibraryStore::createFolder(QString(), QStringLiteral("Archivo"), 3, 2, &archiveRel));

        const QString origPath =
            QDir(LibraryStore::resolveDirectory(algRel)).filePath(QStringLiteral("Clase 1.finily"));
        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(origPath, ThemeId::DarkBoard, BackgroundTemplate::Grid,
                                   PageFormat::Letter, Viewport{}, ts, 1.0, CoverStyle{4, 2},
                                   QColor("#102a43")));

        QString dupPath;
        QVERIFY(LibraryStore::duplicateNotebook(origPath, &dupPath));
        QVERIFY(dupPath != origPath);
        QVERIFY(QFileInfo::exists(dupPath));
        QCOMPARE(QFileInfo(dupPath).absolutePath(), QFileInfo(origPath).absolutePath());
        QCOMPARE(readNotebookMetadata(dupPath).customBackground, QColor("#102a43"));
        QCOMPARE(readNotebookMetadata(dupPath).pageFormat, PageFormat::Letter);

        const QString pdfPath = paths.path(QStringLiteral("exported.pdf"));
        QVERIFY(LibraryStore::exportNotebookToPdf(dupPath, pdfPath));
        QFile pdfFile(pdfPath);
        QVERIFY(pdfFile.open(QIODevice::ReadOnly));
        QCOMPARE(pdfFile.read(5), QByteArrayLiteral("%PDF-"));
        QVERIFY(pdfFile.size() > 200);

        // Rechazar mover una carpeta dentro de sí misma o de una descendiente.
        QVERIFY(!LibraryStore::moveFolder(uniRel, uniRel));
        QVERIFY(!LibraryStore::moveFolder(uniRel, algRel));

        QString movedRel;
        QVERIFY(LibraryStore::moveFolder(algRel, archiveRel, &movedRel));
        QCOMPARE(movedRel, QStringLiteral("Archivo/Álgebra"));
        const QVector<NotebookMeta> docs = LibraryStore::listAllNotebooksRecursive();
        QCOMPARE(docs.size(), 2);
        for (const NotebookMeta& m : docs)
            QCOMPARE(m.folderRelativePath, QStringLiteral("Archivo/Álgebra"));

        LibraryStore::setCustomLibraryDirectory(QString{});
    }

    void favoritesAndMetadataMigration() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        QTemporaryDir lib;
        QVERIFY(lib.isValid());
        ScopedLibraryDirEnv env(lib.path());

        QString mathRel, archiveRel;
        QVERIFY(LibraryStore::createFolder(QString(), QStringLiteral("Matemáticas"), 1, 0, &mathRel));
        QVERIFY(LibraryStore::createFolder(QString(), QStringLiteral("Archivo"), 2, 0, &archiveRel));

        const QString docA = QDir(lib.path()).filePath(QStringLiteral("Borrador.finily"));
        const QString docB = QDir(lib.path()).filePath(QStringLiteral("Matemáticas/Cálculo.finily"));
        NotebookDocument::DocumentToolSettings ts;
        NotebookDocument d;
        QVERIFY(d.createNotebook(docA, ThemeId::DarkBoard, BackgroundTemplate::Dots,
                                 PageFormat::Infinite, Viewport{}, ts));
        QVERIFY(d.createNotebook(docB, ThemeId::PaperLight, BackgroundTemplate::Grid,
                                 PageFormat::A4, Viewport{}, ts));

        QVERIFY(LibraryStore::setDocumentFavorite(docA, true));
        QVERIFY(LibraryStore::setDocumentFavorite(docB, true));
        QVERIFY(LibraryStore::setFolderFavorite(mathRel, true));
        QVERIFY(LibraryStore::isDocumentFavorite(docA));
        QVERIFY(LibraryStore::isDocumentFavorite(docB));
        QVERIFY(LibraryStore::isFolderFavorite(mathRel));

        QCOMPARE(LibraryStore::favoriteKeys(),
                 (QStringList{QStringLiteral("doc:Borrador.finily"),
                              QStringLiteral("doc:Matemáticas/Cálculo.finily"),
                              QStringLiteral("folder:Matemáticas")}));

        QVERIFY(LibraryStore::reorderFavorite(QStringLiteral("folder:Matemáticas"), 0));
        QCOMPARE(LibraryStore::favoriteKeys().first(), QStringLiteral("folder:Matemáticas"));

        const QVector<FolderMeta> folders = LibraryStore::listFolders();
        for (const FolderMeta& f : folders) {
            if (f.relativePath == mathRel)
                QVERIFY(f.isFavorite);
            else
                QVERIFY(!f.isFavorite);
        }

        // Renombrar y mover documento migra su clave de favorito en orden.
        QString renamedA;
        QVERIFY(LibraryStore::renameNotebook(docA, QStringLiteral("Apuntes"), &renamedA));
        QVERIFY(!LibraryStore::isDocumentFavorite(docA));
        QVERIFY(LibraryStore::isDocumentFavorite(renamedA));

        QString movedA;
        QVERIFY(LibraryStore::moveNotebookToFolder(renamedA, archiveRel, &movedA));
        QVERIFY(LibraryStore::isDocumentFavorite(movedA));
        QVERIFY(LibraryStore::favoriteKeys().contains(QStringLiteral("doc:Archivo/Apuntes.finily")));

        // Mover y renombrar carpeta migra su clave y las de sus hijos.
        QString movedMathRel;
        QVERIFY(LibraryStore::moveFolder(mathRel, archiveRel, &movedMathRel));
        QCOMPARE(movedMathRel, QStringLiteral("Archivo/Matemáticas"));
        QVERIFY(LibraryStore::isFolderFavorite(movedMathRel));
        QVERIFY(LibraryStore::isDocumentFavorite(
            QDir(lib.path()).filePath(QStringLiteral("Archivo/Matemáticas/Cálculo.finily"))));

        QString renamedMathRel;
        QVERIFY(LibraryStore::renameFolder(movedMathRel, QStringLiteral("Mate"), &renamedMathRel));
        QCOMPARE(renamedMathRel, QStringLiteral("Archivo/Mate"));
        QVERIFY(LibraryStore::isFolderFavorite(renamedMathRel));
        const QString nestedDoc = QDir(lib.path()).filePath(QStringLiteral("Archivo/Mate/Cálculo.finily"));
        QVERIFY(LibraryStore::isDocumentFavorite(nestedDoc));

        // Papelera + restaurar conserva favorito.
        QString trashId;
        QVERIFY(LibraryStore::moveToTrash(nestedDoc, &trashId));
        QVERIFY(!LibraryStore::isDocumentFavorite(nestedDoc));
        QString restoredDoc;
        QVERIFY(LibraryStore::restoreFromTrash(trashId, &restoredDoc));
        QVERIFY(LibraryStore::isDocumentFavorite(restoredDoc));

        // Destinos recientes (máx. 5).
        for (int i = 1; i <= 6; ++i)
            LibraryStore::recordRecentMoveFolder(QStringLiteral("Dest%1").arg(i));
        QCOMPARE(LibraryStore::recentMoveFolders(),
                 (QStringList{QStringLiteral("Dest6"), QStringLiteral("Dest5"),
                              QStringLiteral("Dest4"), QStringLiteral("Dest3"),
                              QStringLiteral("Dest2")}));
    }

    void tagsCrudAssignUndoAndMigration() {
        ScopedTestPaths paths;
        QVERIFY(paths.isValid());
        QTemporaryDir lib;
        QVERIFY(lib.isValid());
        ScopedLibraryDirEnv env(lib.path());

        const TagInfo t1 = LibraryStore::createTag(QStringLiteral("Física"));
        QVERIFY(!t1.id.isEmpty());
        QCOMPARE(t1.colorIndex, 0);
        const TagInfo dup = LibraryStore::createTag(QStringLiteral("física"));
        QCOMPARE(dup.id, t1.id);

        const TagInfo t2 = LibraryStore::createTag(QStringLiteral("Química"), 4);
        QCOMPARE(t2.colorIndex, 4);
        QCOMPARE(LibraryStore::listTags().size(), 2);

        QVERIFY(!LibraryStore::renameTag(t1.id, QStringLiteral("química")));
        QVERIFY(LibraryStore::renameTag(t1.id, QStringLiteral("Física II")));
        QVERIFY(LibraryStore::setTagColor(t1.id, 6));

        QString folderRel;
        QVERIFY(LibraryStore::createFolder(QString(), QStringLiteral("Semestre"), 0, 0, &folderRel));
        const QString docPath =
            QDir(LibraryStore::resolveDirectory(folderRel)).filePath(QStringLiteral("Guía.finily"));
        NotebookDocument doc;
        NotebookDocument::DocumentToolSettings ts;
        QVERIFY(doc.createNotebook(docPath, ThemeId::DarkBoard, BackgroundTemplate::Dots,
                                   PageFormat::Infinite, Viewport{}, ts));

        QVERIFY(LibraryStore::setDocumentTag(docPath, t1.id, true));
        QVERIFY(LibraryStore::setDocumentTag(docPath, t2.id, true));
        QCOMPARE(LibraryStore::documentTags(docPath), (QStringList{t1.id, t2.id}));

        const QVector<NotebookMeta> listed = LibraryStore::listAllNotebooksRecursive();
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().tagIds, (QStringList{t1.id, t2.id}));

        // Eliminar etiqueta + restaurar (Undo).
        TagInfo deletedInfo = t1;
        deletedInfo.name = QStringLiteral("Física II");
        deletedInfo.colorIndex = 6;
        const QStringList affected = LibraryStore::deleteTag(t1.id);
        QCOMPARE(affected, QStringList{QStringLiteral("Semestre/Guía.finily")});
        QCOMPARE(LibraryStore::documentTags(docPath), QStringList{t2.id});

        QVERIFY(LibraryStore::restoreTag(deletedInfo, affected));
        QCOMPARE(LibraryStore::documentTags(docPath), (QStringList{t2.id, t1.id}));

        // Renombrar documento, mover carpeta y mover a papelera / restaurar conservan etiquetas.
        QString renamedDoc;
        QVERIFY(LibraryStore::renameNotebook(docPath, QStringLiteral("Resumen"), &renamedDoc));
        QCOMPARE(LibraryStore::documentTags(renamedDoc), (QStringList{t2.id, t1.id}));

        QString renamedFolder;
        QVERIFY(LibraryStore::renameFolder(folderRel, QStringLiteral("Ciclo"), &renamedFolder));
        const QString movedDoc = QDir(lib.path()).filePath(QStringLiteral("Ciclo/Resumen.finily"));
        QCOMPARE(LibraryStore::documentTags(movedDoc), (QStringList{t2.id, t1.id}));

        QString trashId;
        QVERIFY(LibraryStore::moveToTrash(movedDoc, &trashId));
        QVERIFY(LibraryStore::documentTags(movedDoc).isEmpty());
        QString restoredPath;
        QVERIFY(LibraryStore::restoreFromTrash(trashId, &restoredPath));
        QCOMPARE(LibraryStore::documentTags(restoredPath), (QStringList{t2.id, t1.id}));
    }
};

QTEST_MAIN(TestLibraryStore)
#include "test_library_store.moc"
