#pragma once

#include "core/Types.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace finily {

struct TagInfo {
    QString id;
    QString name;
    int colorIndex = 0;
};

struct LibraryMetadataSnapshot {
    QSet<QString> favoriteKeys;
    QHash<QString, QStringList> docTags;
};

// Metadatos ligeros leídos de un archivo .finily para la biblioteca.
struct NotebookMeta {
    QString path;
    QString title;
    QString folderRelativePath; // vacío = raíz; "Universidad/Álgebra" = subcarpeta
    ThemeId theme = ThemeId::DarkBoard;
    BackgroundTemplate tpl = BackgroundTemplate::Dots;
    PageFormat pageFormat = PageFormat::Infinite;
    double templateScale = 1.0;
    QColor customBackground;
    int coverColorIndex = 0; // resuelto [0..kCoverColorCount - 1]
    int coverIconIndex = 0;  // 0 = ninguno; 1..7 = ícono editorial
    int pages = 1;
    bool hasPdf = false;
    bool isFavorite = false;
    QStringList tagIds;
    QDateTime created;
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
    bool isFavorite = false;
    QDateTime modified;
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
    static QString customLibraryDirectory();
    static void setCustomLibraryDirectory(const QString& dir);
    static int trashRetentionDays();
    static void setTrashRetentionDays(int days);
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
    static bool setBoardStyle(const QString& path, ThemeId theme, const QColor& customBackground,
                              BackgroundTemplate tpl, double templateScale);

    static bool renameNotebook(const QString& oldPath, const QString& rawNewName,
                               QString* outNewPath = nullptr, QString* outError = nullptr);
    static bool duplicateNotebook(const QString& path, QString* outNewPath = nullptr,
                                  QString* outError = nullptr);
    static bool deleteNotebook(const QString& path, QString* outError = nullptr);
    static bool moveNotebookToFolder(const QString& notebookPath, const QString& targetRelativeFolder,
                                     QString* outNewPath = nullptr, QString* outError = nullptr);
    static bool moveNotebookToDirectory(const QString& notebookPath, const QString& targetAbsDir,
                                        QString* outNewPath = nullptr, QString* outError = nullptr);
    static bool exportNotebookToPdf(const QString& finilyPath, const QString& pdfPath,
                                    QString* outError = nullptr);

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
    static bool moveFolder(const QString& relativePath, const QString& targetParentRelativePath,
                           QString* outNewRelativePath = nullptr,
                           QString* outOldAbsPath = nullptr,
                           QString* outNewAbsPath = nullptr,
                           QString* outError = nullptr);
    static bool moveFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                      const QString& targetParentRelativePath,
                                      QString* outNewRelativePath = nullptr,
                                      QString* outOldAbsPath = nullptr,
                                      QString* outNewAbsPath = nullptr,
                                      QString* outError = nullptr);
    static bool deleteFolder(const QString& relativePath, QString* outAbsPath = nullptr,
                             QString* outError = nullptr);
    static bool deleteFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                        QString* outAbsPath = nullptr, QString* outError = nullptr);

    // Favoritos, etiquetas y destinos recientes (implementado en LibraryMetadata.cpp)
    static QStringList favoriteKeys();
    static QStringList favoriteKeysInDirectory(const QString& rootDir);
    static bool isDocumentFavorite(const QString& absPath);
    static bool setDocumentFavorite(const QString& absPath, bool fav);
    static bool isFolderFavorite(const QString& relativePath);
    static bool setFolderFavorite(const QString& relativePath, bool fav);
    static bool reorderFavorite(const QString& key, int newIndex);

    static QVector<TagInfo> listTags();
    static QVector<TagInfo> listTagsInDirectory(const QString& rootDir);
    static TagInfo createTag(const QString& name, int colorIndex = -1);
    static bool renameTag(const QString& tagId, const QString& newName);
    static bool setTagColor(const QString& tagId, int colorIndex);
    static QStringList deleteTag(const QString& tagId);
    static bool restoreTag(const TagInfo& tag, const QStringList& relDocPaths);
    static QStringList documentTags(const QString& absPath);
    static bool setDocumentTag(const QString& absPath, const QString& tagId, bool on);

    static QStringList recentMoveFolders();
    static void recordRecentMoveFolder(const QString& relativeFolder);

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
    static int purgeExpiredTrash(int days = -1);

    // Preferencias y borradores
    static NewDocumentSpec lastUsedQuickNoteSpec();
    static void saveLastUsedSpec(const NewDocumentSpec& spec);
    static bool hasRecoverableDraft();

    // Auxiliares de metadatos/caché compartidos con LibraryFolders.cpp, LibraryTrash.cpp y LibraryMetadata.cpp
    static void migrateCoverSettings(const QString& oldPath, const QString& newPath);
    static void removeCoverSettings(const QString& path);
    static void invalidateCache(const QString& path = {});
    static LibraryMetadataSnapshot metadataSnapshotForDirectory(const QString& rootDir);
    static void migrateMetadataForDocument(const QString& oldAbsPath, const QString& newAbsPath);
    static void removeMetadataForDocument(const QString& absPath);
    static void migrateMetadataForFolder(const QString& rootDir, const QString& oldRelPath,
                                         const QString& newRelPath);
    static void removeMetadataForFolder(const QString& rootDir, const QString& relPath);
};

} // namespace finily
