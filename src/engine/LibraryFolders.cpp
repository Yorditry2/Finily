#include "engine/LibraryStore.h"

#include "engine/DocumentSerializer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace finily {
namespace {

constexpr QLatin1StringView kFolderMetaFile(".finily-folder.json");

struct FolderStyleData {
    int colorIndex = 0;
    int iconIndex = 0;
};

int defaultFolderColor(const QString& name) {
    return int(qHash(name) % uint(LibraryStore::kCoverColorCount - 1));
}

FolderStyleData readFolderStyle(const QString& absDir, const QString& folderName) {
    FolderStyleData data;
    data.colorIndex = defaultFolderColor(folderName);
    QFile f(QDir(absDir).filePath(QString(kFolderMetaFile)));
    if (!f.open(QIODevice::ReadOnly))
        return data;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return data;
    const QJsonObject obj = doc.object();
    data.colorIndex = std::clamp(obj.value(QStringLiteral("color")).toInt(data.colorIndex),
                                 0, LibraryStore::kCoverColorCount - 1);
    data.iconIndex = std::clamp(obj.value(QStringLiteral("icon")).toInt(0),
                                0, LibraryStore::kIconCount - 1);
    return data;
}

bool writeFolderStyle(const QString& absDir, int colorIndex, int iconIndex) {
    QJsonObject obj;
    obj.insert(QStringLiteral("color"), std::clamp(colorIndex, 0, LibraryStore::kCoverColorCount - 1));
    obj.insert(QStringLiteral("icon"), std::clamp(iconIndex, 0, LibraryStore::kIconCount - 1));
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    QSaveFile file(QDir(absDir).filePath(QString(kFolderMetaFile)));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void countFolderContents(const QString& absDir, int& outDocs, int& outDirectSubfolders) {
    outDocs = 0;
    outDirectSubfolders = 0;
    const QDir dir(absDir);
    outDocs += int(dir.entryInfoList({QStringLiteral("*.finily")}, QDir::Files).size());
    const auto subdirs =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& sub : subdirs) {
        if (sub.fileName().startsWith(QLatin1Char('.')))
            continue;
        ++outDirectSubfolders;
        int childDocs = 0;
        int childSubs = 0;
        countFolderContents(sub.absoluteFilePath(), childDocs, childSubs);
        outDocs += childDocs;
    }
}

QString uniqueFolderDirPath(const QString& parentAbsDir, const QString& cleanName,
                            QString* outFinalName = nullptr) {
    const QDir dir(parentAbsDir);
    QString candidateName = cleanName;
    QString path = dir.filePath(candidateName);
    int suffix = 2;
    while (QFileInfo::exists(path)) {
        candidateName = QStringLiteral("%1 (%2)").arg(cleanName).arg(suffix++);
        path = dir.filePath(candidateName);
    }
    if (outFinalName)
        *outFinalName = candidateName;
    return path;
}

QStringList collectDescendantFinilyFiles(const QString& absDir) {
    QStringList files;
    QDirIterator it(absDir, {QStringLiteral("*.finily")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    return files;
}

void appendFolderTreeRecursive(const QString& absDir, const QString& relPath, int depth,
                               QVector<FolderMeta>& out, QHash<QString, qsizetype>& indexByRel) {
    const QDir dir(absDir);
    const auto entries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& sub : entries) {
        if (sub.fileName().startsWith(QLatin1Char('.')))
            continue;
        const QString childRel =
            relPath.isEmpty() ? sub.fileName() : (relPath + QLatin1Char('/') + sub.fileName());
        int docCount = 0;
        int subCount = 0;
        countFolderContents(sub.absoluteFilePath(), docCount, subCount);
        const FolderStyleData style = readFolderStyle(sub.absoluteFilePath(), sub.fileName());

        if (indexByRel.contains(childRel)) {
            out[indexByRel.value(childRel)].documentCount += docCount;
            out[indexByRel.value(childRel)].directSubfolders =
                std::max(out[indexByRel.value(childRel)].directSubfolders, subCount);
        } else {
            FolderMeta meta;
            meta.relativePath = childRel;
            meta.name = sub.fileName();
            meta.absolutePath = sub.absoluteFilePath();
            meta.depth = depth;
            meta.colorIndex = style.colorIndex;
            meta.iconIndex = style.iconIndex;
            meta.documentCount = docCount;
            meta.directSubfolders = subCount;
            indexByRel.insert(childRel, out.size());
            out.append(meta);
        }
        appendFolderTreeRecursive(sub.absoluteFilePath(), childRel, depth + 1, out, indexByRel);
    }
}

QString findExistingFolderAbsPath(const QStringList& dirs, const QString& normRel) {
    for (const QString& root : dirs) {
        const QString candidate = QDir(root).filePath(normRel);
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isDir())
            return candidate;
    }
    return {};
}

} // namespace

QVector<FolderMeta> LibraryStore::listFolders(const QString& parentRelativePath) {
    return listFoldersInDirectories(libraryDirectories(), parentRelativePath);
}

QVector<FolderMeta> LibraryStore::listFoldersInDirectories(const QStringList& directories,
                                                           const QString& parentRelativePath) {
    const QString parentNorm = normalizeRelativeFolder(parentRelativePath);
    QVector<FolderMeta> out;
    QHash<QString, qsizetype> indexByRel;

    for (const QString& root : directories) {
        const QString targetDir = parentNorm.isEmpty() ? root : QDir(root).filePath(parentNorm);
        const QDir dir(targetDir);
        if (!dir.exists())
            continue;
        const auto subdirs =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& sub : subdirs) {
            if (sub.fileName().startsWith(QLatin1Char('.')))
                continue;
            const QString rel = parentNorm.isEmpty()
                ? sub.fileName()
                : (parentNorm + QLatin1Char('/') + sub.fileName());
            int docCount = 0;
            int subCount = 0;
            countFolderContents(sub.absoluteFilePath(), docCount, subCount);
            if (indexByRel.contains(rel)) {
                out[indexByRel.value(rel)].documentCount += docCount;
                out[indexByRel.value(rel)].directSubfolders =
                    std::max(out[indexByRel.value(rel)].directSubfolders, subCount);
                continue;
            }
            const FolderStyleData style = readFolderStyle(sub.absoluteFilePath(), sub.fileName());
            FolderMeta meta;
            meta.relativePath = rel;
            meta.name = sub.fileName();
            meta.absolutePath = sub.absoluteFilePath();
            meta.depth = parentNorm.isEmpty() ? 0 : int(parentNorm.count(QLatin1Char('/')) + 1);
            meta.colorIndex = style.colorIndex;
            meta.iconIndex = style.iconIndex;
            meta.documentCount = docCount;
            meta.directSubfolders = subCount;
            indexByRel.insert(rel, out.size());
            out.append(meta);
        }
    }
    return out;
}

QVector<FolderMeta> LibraryStore::listAllFoldersTree() {
    return listAllFoldersTreeInDirectories(libraryDirectories());
}

QVector<FolderMeta> LibraryStore::listAllFoldersTreeInDirectories(const QStringList& directories) {
    QVector<FolderMeta> out;
    QHash<QString, qsizetype> indexByRel;
    for (const QString& root : directories) {
        if (QDir(root).exists())
            appendFolderTreeRecursive(root, QString(), 0, out, indexByRel);
    }
    return out;
}

bool LibraryStore::createFolder(const QString& parentRelativePath, const QString& rawName,
                                int colorIndex, int iconIndex,
                                QString* outRelativePath, QString* outError) {
    for (const QString& root : libraryDirectories()) {
        if (createFolderInDirectory(root, parentRelativePath, rawName, colorIndex, iconIndex,
                                    outRelativePath, outError)) {
            return true;
        }
    }
    return false;
}

bool LibraryStore::createFolderInDirectory(const QString& rootDir, const QString& parentRelativePath,
                                           const QString& rawName, int colorIndex, int iconIndex,
                                           QString* outRelativePath, QString* outError) {
    if (rootDir.isEmpty())
        return false;
    QString clean = sanitizeBaseName(rawName);
    if (clean.isEmpty())
        clean = QStringLiteral("Nueva carpeta");
    while (clean.startsWith(QLatin1Char('.')))
        clean.remove(0, 1);
    if (clean.isEmpty())
        clean = QStringLiteral("Nueva carpeta");

    const QString parentNorm = normalizeRelativeFolder(parentRelativePath);
    const QString parentAbs = parentNorm.isEmpty() ? rootDir : QDir(rootDir).filePath(parentNorm);
    if (!QDir().mkpath(parentAbs)) {
        if (outError)
            *outError = QStringLiteral("No se pudo crear la carpeta.");
        return false;
    }

    QString finalName;
    const QString absNew = uniqueFolderDirPath(parentAbs, clean, &finalName);
    if (!QDir().mkpath(absNew)) {
        if (outError)
            *outError = QStringLiteral("No se pudo crear la carpeta.");
        return false;
    }
    writeFolderStyle(absNew, colorIndex, iconIndex);
    if (outRelativePath) {
        *outRelativePath =
            parentNorm.isEmpty() ? finalName : (parentNorm + QLatin1Char('/') + finalName);
    }
    return true;
}

bool LibraryStore::updateFolderStyle(const QString& relativePath, int colorIndex, int iconIndex) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (norm.isEmpty())
        return false;
    const QString abs = findExistingFolderAbsPath(libraryDirectories(), norm);
    return !abs.isEmpty() && writeFolderStyle(abs, colorIndex, iconIndex);
}

bool LibraryStore::updateFolderStyleInDirectory(const QString& rootDir, const QString& relativePath,
                                                int colorIndex, int iconIndex) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (rootDir.isEmpty() || norm.isEmpty())
        return false;
    const QString abs = QDir(rootDir).filePath(norm);
    return QFileInfo(abs).isDir() && writeFolderStyle(abs, colorIndex, iconIndex);
}

bool LibraryStore::renameFolder(const QString& relativePath, const QString& rawNewName,
                                QString* outNewRelativePath, QString* outOldAbsPath,
                                QString* outNewAbsPath, QString* outError) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (norm.isEmpty())
        return false;
    for (const QString& root : libraryDirectories()) {
        const QString abs = QDir(root).filePath(norm);
        if (QFileInfo(abs).isDir()) {
            return renameFolderInDirectory(root, norm, rawNewName, outNewRelativePath,
                                           outOldAbsPath, outNewAbsPath, outError);
        }
    }
    return false;
}

bool LibraryStore::renameFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                           const QString& rawNewName, QString* outNewRelativePath,
                                           QString* outOldAbsPath, QString* outNewAbsPath,
                                           QString* outError) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (rootDir.isEmpty() || norm.isEmpty())
        return false;
    const QString oldAbs = QDir(rootDir).filePath(norm);
    if (!QFileInfo(oldAbs).isDir())
        return false;

    QString clean = sanitizeBaseName(rawNewName);
    while (clean.startsWith(QLatin1Char('.')))
        clean.remove(0, 1);
    if (clean.isEmpty())
        clean = QStringLiteral("Carpeta");

    const qsizetype slash = norm.lastIndexOf(QLatin1Char('/'));
    const QString parentRel = (slash >= 0) ? norm.left(slash) : QString();
    const QString parentAbs = parentRel.isEmpty() ? rootDir : QDir(rootDir).filePath(parentRel);
    const QString exactTarget = QDir(parentAbs).filePath(clean);
    if (exactTarget == oldAbs) {
        if (outNewRelativePath)
            *outNewRelativePath = norm;
        if (outOldAbsPath)
            *outOldAbsPath = oldAbs;
        if (outNewAbsPath)
            *outNewAbsPath = oldAbs;
        return true;
    }

    QString finalName;
    const QString newAbs = uniqueFolderDirPath(parentAbs, clean, &finalName);
    const QStringList oldFiles = collectDescendantFinilyFiles(oldAbs);
    if (!QDir().rename(oldAbs, newAbs)) {
        if (outError)
            *outError = QStringLiteral("No se pudo renombrar la carpeta.");
        return false;
    }
    const QDir oldBase(oldAbs);
    const QDir newBase(newAbs);
    for (const QString& oldFile : oldFiles) {
        const QString newFile = newBase.filePath(oldBase.relativeFilePath(oldFile));
        migrateCoverSettings(oldFile, newFile);
        moveCoverThumbnail(oldFile, newFile);
        invalidateCache(oldFile);
        invalidateCache(newFile);
    }
    if (outNewRelativePath) {
        *outNewRelativePath =
            parentRel.isEmpty() ? finalName : (parentRel + QLatin1Char('/') + finalName);
    }
    if (outOldAbsPath)
        *outOldAbsPath = oldAbs;
    if (outNewAbsPath)
        *outNewAbsPath = newAbs;
    return true;
}

bool LibraryStore::deleteFolder(const QString& relativePath, QString* outAbsPath, QString* outError) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (norm.isEmpty())
        return false;
    for (const QString& root : libraryDirectories()) {
        const QString abs = QDir(root).filePath(norm);
        if (QFileInfo(abs).isDir())
            return deleteFolderInDirectory(root, norm, outAbsPath, outError);
    }
    return false;
}

bool LibraryStore::deleteFolderInDirectory(const QString& rootDir, const QString& relativePath,
                                           QString* outAbsPath, QString* outError) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (rootDir.isEmpty() || norm.isEmpty())
        return false;
    const QString abs = QDir(rootDir).filePath(norm);
    if (!QFileInfo(abs).isDir())
        return false;

    const QStringList files = collectDescendantFinilyFiles(abs);
    if (!QDir(abs).removeRecursively()) {
        if (outError)
            *outError = QStringLiteral("No se pudo eliminar la carpeta.");
        return false;
    }
    for (const QString& f : files) {
        removeCoverSettings(f);
        removeCoverThumbnail(f);
        invalidateCache(f);
    }
    if (outAbsPath)
        *outAbsPath = abs;
    return true;
}

bool LibraryStore::moveNotebookToFolder(const QString& notebookPath,
                                        const QString& targetRelativeFolder,
                                        QString* outNewPath, QString* outError) {
    const QString targetDir = resolveDirectory(targetRelativeFolder);
    return moveNotebookToDirectory(notebookPath, targetDir, outNewPath, outError);
}

bool LibraryStore::moveNotebookToDirectory(const QString& notebookPath, const QString& targetAbsDir,
                                           QString* outNewPath, QString* outError) {
    if (notebookPath.isEmpty() || targetAbsDir.isEmpty() || !QFileInfo::exists(notebookPath))
        return false;
    const QFileInfo srcInfo(notebookPath);
    if (QDir::cleanPath(srcInfo.absolutePath()) == QDir::cleanPath(targetAbsDir)) {
        if (outNewPath)
            *outNewPath = notebookPath;
        return true;
    }
    if (!QDir().mkpath(targetAbsDir)) {
        if (outError)
            *outError = QStringLiteral("No se pudo crear la carpeta de destino.");
        return false;
    }
    const QString newPath = uniqueNotebookPath(targetAbsDir, srcInfo.completeBaseName());
    if (!QFile::rename(notebookPath, newPath)) {
        if (!QFile::copy(notebookPath, newPath)) {
            if (outError)
                *outError = QStringLiteral("No se pudo mover el documento.");
            return false;
        }
        QFile::remove(notebookPath);
    }
    migrateCoverSettings(notebookPath, newPath);
    moveCoverThumbnail(notebookPath, newPath);
    invalidateCache(notebookPath);
    invalidateCache(newPath);
    if (outNewPath)
        *outNewPath = newPath;
    return true;
}

} // namespace finily
