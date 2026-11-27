#pragma once

#include "core/Types.h"

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace finily {

// Metadatos ligeros leídos de un archivo .finily para la biblioteca.
struct NotebookMeta {
    QString path;
    QString title;
    QString folderRelativePath; // vacío = raíz; "Universidad/Álgebra" = subcarpeta
    ThemeId theme = ThemeId::DarkBoard;
    BackgroundTemplate tpl = BackgroundTemplate::Dots;
    PageFormat pageFormat = PageFormat::Infinite;
    double templateScale = 1.0;
    int coverColorIndex = 0; // resuelto [0..kCoverColorCount - 1]
    int coverIconIndex = 0;  // 0 = ninguno; 1..7 = ícono editorial
    int pages = 1;
    bool hasPdf = false;
    QDateTime modified;
    QImage preview;
};

// Metadatos de una carpeta real dentro de la biblioteca.
struct FolderMeta {
    QString relativePath;
    QString name;
    QString absolutePath;
    int depth = 0;
    int colorIndex = 0;
    int iconIndex = 0;
    int documentCount = 0;
    int directSubfolders = 0;
};

// Elemento en la papelera de la biblioteca (<raíz>/.papelera/<id>/<nombre>).
struct TrashEntry {
    QString id;
    QString name;             // nombre visible (sin .finily)
    QString originalRelative; // carpeta de origen relativa a la raíz ("" = raíz)
    QString rootDir;
    QString itemPath;         // ruta absoluta actual dentro de la papelera
    bool isFolder = false;
    QDateTime deletedAt;
};

// Acceso a disco, caché de metadatos, carpetas/subcarpetas, portadas y
// preferencias de creación fuera de la UI (sin Qt6::Widgets).
class LibraryStore {
public:
    static constexpr int kCoverColorCount = 8;
    static constexpr int kIconCount = 14;

    static QStringList libraryDirectories();
    static QString resolveDirectory(const QString& relativeFolder = {});
    static QString normalizeRelativeFolder(QString relativeFolder);
    static QString sanitizeBaseName(QString name);
    static QString uniqueNotebookPath(const QString& directory, const QString& baseName);
    static QString defaultQuickNoteTitle(const QDateTime& now = QDateTime::currentDateTime());

    // Documentos
    static QVector<NotebookMeta> listNotebooks(const QString& relativeFolder = {});
    static QVector<NotebookMeta> listNotebooksInDirectories(const QStringList& directories,
                                                            const QString& relativeFolder = {});
    static QVector<NotebookMeta> listAllNotebooksRecursive();
    static QVector<NotebookMeta> listAllNotebooksRecursiveInDirectories(const QStringList& directories);

    static int resolveCoverColor(const QString& path, const QString& title, int embeddedColorIndex);
    static bool setCoverColor(const QString& path, int colorIndex);
    static bool setCoverStyle(const QString& path, CoverStyle style);

    static bool renameNotebook(const QString& oldPath, const QString& rawNewName,
                               QString* outNewPath = nullptr, QString* outError = nullptr);
    static bool deleteNotebook(const QString& path, QString* outError = nullptr);
    static bool moveNotebookToFolder(const QString& notebookPath, const QString& targetRelativeFolder,
                                     QString* outNewPath = nullptr, QString* outError = nullptr);
    static bool moveNotebookToDirectory(const QString& notebookPath, const QString& targetAbsDir,
                                        QString* outNewPath = nullptr, QString* outError = nullptr);

    // Carpetas y subcarpetas (implementado en LibraryFolders.cpp)
    static QVector<FolderMeta> listFolders(const QString& parentRelativePath = {});
    static QVector<FolderMeta> listFoldersInDirectories(const QStringList& directories,
                                                        const QString& parentRelativePath = {});
    static QVector<FolderMeta> listAllFoldersTree();
    static QVector<FolderMeta> listAllFoldersTreeInDirectories(const QStringList& directories);

    static bool createFolder(const QString& parentRelativePath, const QString& rawName,
                             int colorIndex, int iconIndex,
                             QString* outRelativePath = nullptr, QString* outError = nullptr);
    static bool createFolderInDirectory(const QString& rootDir, const QString& parentRelativePath,
                                        const QString& rawName, int colorIndex, int iconIndex,
                                        QString* outRelativePath = nullptr, QString* outError = nullptr);
    static bool updateFolderStyle(const QString& relativePath, int colorIndex, int iconIndex);
    static bool updateFolderStyleInDirectory(const QString& rootDir, const QString& relativePath,
                                             int colorIndex, int iconIndex);
    static bool renameFolder(const QString& relativePath, const QString& rawNewName,
                             QString* outNewRelativePath = nullptr,
                             QString* outOldAbsPath = nullptr,
                             QString* outNewAbsPath = nullptr,
                             QString* outError = nullptr);
    static bool renameFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                        const QString& rawNewName,
                                        QString* outNewRelativePath = nullptr,
                                        QString* outOldAbsPath = nullptr,
                                        QString* outNewAbsPath = nullptr,
                                        QString* outError = nullptr);
    static bool deleteFolder(const QString& relativePath, QString* outAbsPath = nullptr,
                             QString* outError = nullptr);
    static bool deleteFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                        QString* outAbsPath = nullptr, QString* outError = nullptr);

    // Papelera (implementado en LibraryTrash.cpp). Mover es reversible;
    // solo deleteFromTrash / emptyTrash / purgeExpiredTrash borran de verdad.
    static constexpr int kTrashRetentionDays = 30;
    static bool moveToTrash(const QString& absolutePath, QString* outId = nullptr,
                            QString* outError = nullptr);
    static QVector<TrashEntry> listTrash();
    static bool restoreFromTrash(const QString& id, QString* outRestoredPath = nullptr,
                                 QString* outError = nullptr);
    static bool deleteFromTrash(const QString& id, QString* outError = nullptr);
    static int emptyTrash();
    static int purgeExpiredTrash(int days = kTrashRetentionDays);

    // Preferencias y borradores
    static NewDocumentSpec lastUsedQuickNoteSpec();
    static void saveLastUsedSpec(const NewDocumentSpec& spec);
    static bool hasRecoverableDraft();

    // Auxiliares de metadatos/caché compartidos con LibraryFolders.cpp
    static void migrateCoverSettings(const QString& oldPath, const QString& newPath);
    static void removeCoverSettings(const QString& path);
    static void invalidateCache(const QString& path = {});
};

} // namespace finily
