#include "engine/LibraryStore.h"

#include "engine/DocumentSerializer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>

namespace finily {
namespace {

constexpr QLatin1StringView kTrashDir(".papelera");
constexpr QLatin1StringView kTrashInfo(".finily-trash.json");

QString trashRoot(const QString& root) { return QDir(root).filePath(kTrashDir); }

// Raíz de biblioteca que contiene la ruta (o vacío si está fuera de la biblioteca).
QString owningRoot(const QString& absPath) {
    const QString clean = QDir::cleanPath(QFileInfo(absPath).absoluteFilePath());
    for (const QString& root : LibraryStore::libraryDirectories()) {
        const QString r = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
        if (clean.startsWith(r + QLatin1Char('/')))
            return r;
    }
    return {};
}

QStringList finilyFilesUnder(const QString& absPath) {
    if (QFileInfo(absPath).isFile())
        return {absPath};
    QStringList files;
    QDirIterator it(absPath, {QStringLiteral("*.finily")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    return files;
}

// Traslada portadas guardadas y cachés de todos los .finily al mover src → dst.
void migrateTree(const QString& srcAbs, const QString& dstAbs, const QStringList& filesBefore) {
    for (const QString& f : filesBefore) {
        const QString moved = dstAbs + f.mid(srcAbs.size());
        LibraryStore::migrateCoverSettings(f, moved);
        LibraryStore::invalidateCache(f);
        LibraryStore::invalidateCache(moved);
        removeCoverThumbnail(f);
    }
}

bool renameOrCopy(const QString& src, const QString& dst) {
    if (QFile::rename(src, dst) || QDir().rename(src, dst))
        return true;
    return false;
}

TrashEntry readEntry(const QString& root, const QString& idDir) {
    TrashEntry e;
    QFile f(QDir(idDir).filePath(kTrashInfo));
    if (!f.open(QIODevice::ReadOnly))
        return e;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QString item = o.value(QStringLiteral("item")).toString();
    if (item.isEmpty() || item.contains(QLatin1Char('/')))
        return e;
    const QString path = QDir(idDir).filePath(item);
    if (!QFileInfo::exists(path))
        return e;
    e.id = QFileInfo(idDir).fileName();
    e.rootDir = root;
    e.itemPath = path;
    e.isFolder = QFileInfo(path).isDir();
    e.name = e.isFolder ? item : QFileInfo(item).completeBaseName();
    e.originalRelative = LibraryStore::normalizeRelativeFolder(o.value(QStringLiteral("from")).toString());
    e.deletedAt = QDateTime::fromString(o.value(QStringLiteral("deletedAt")).toString(), Qt::ISODate);
    if (!e.deletedAt.isValid())
        e.deletedAt = QFileInfo(f).lastModified();
    return e;
}

bool findEntry(const QString& id, TrashEntry* out) {
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.startsWith(QLatin1Char('.')))
        return false;
    for (const QString& root : LibraryStore::libraryDirectories()) {
        const QString dir = QDir(trashRoot(root)).filePath(id);
        if (!QFileInfo(dir).isDir())
            continue;
        TrashEntry e = readEntry(root, dir);
        if (!e.id.isEmpty()) {
            *out = e;
            return true;
        }
    }
    return false;
}

bool purgeEntry(const TrashEntry& e) {
    for (const QString& f : finilyFilesUnder(e.itemPath)) {
        LibraryStore::removeCoverSettings(f);
        LibraryStore::invalidateCache(f);
        removeCoverThumbnail(f);
    }
    return QDir(QFileInfo(e.itemPath).absolutePath()).removeRecursively();
}

} // namespace

bool LibraryStore::moveToTrash(const QString& absolutePath, QString* outId, QString* outError) {
    const QFileInfo info(absolutePath);
    const QString root = owningRoot(absolutePath);
    if (!info.exists() || root.isEmpty()) {
        if (outError)
            *outError = QStringLiteral("El elemento ya no existe.");
        return false;
    }
    const QString src = QDir::cleanPath(info.absoluteFilePath());
    const QString id = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString idDir = QDir(trashRoot(root)).filePath(id);
    if (!QDir().mkpath(idDir)) {
        if (outError)
            *outError = QStringLiteral("No se pudo crear la papelera.");
        return false;
    }
    const QString dst = QDir(idDir).filePath(info.fileName());
    const QStringList files = finilyFilesUnder(src);
    const bool isDir = info.isDir();
    const QString folderRel = isDir
        ? normalizeRelativeFolder(QDir(root).relativeFilePath(src))
        : QString();
    const bool wasFav = isDir ? isFolderFavorite(folderRel) : isDocumentFavorite(src);
    const QStringList docTagIds = isDir ? QStringList{} : documentTags(src);
    QJsonObject childMeta;
    if (isDir) {
        const QDir srcBase(src);
        for (const QString& f : files) {
            const bool fFav = isDocumentFavorite(f);
            const QStringList fTags = documentTags(f);
            if (fFav || !fTags.isEmpty()) {
                QJsonObject entryMeta;
                if (fFav)
                    entryMeta.insert(QStringLiteral("fav"), true);
                if (!fTags.isEmpty())
                    entryMeta.insert(QStringLiteral("tags"), QJsonArray::fromStringList(fTags));
                childMeta.insert(srcBase.relativeFilePath(f), entryMeta);
            }
        }
    }

    if (!renameOrCopy(src, dst)) {
        QDir(idDir).removeRecursively();
        if (outError)
            *outError = QStringLiteral("No se pudo mover a la papelera.");
        return false;
    }
    const QString parentRel = QDir(root).relativeFilePath(info.absolutePath());
    QJsonObject o;
    o.insert(QStringLiteral("item"), info.fileName());
    o.insert(QStringLiteral("from"), parentRel == QLatin1String(".") ? QString() : parentRel);
    o.insert(QStringLiteral("deletedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (isDir) {
        if (wasFav)
            o.insert(QStringLiteral("folderWasFavorite"), true);
        if (!childMeta.isEmpty())
            o.insert(QStringLiteral("childMeta"), childMeta);
    } else {
        if (wasFav)
            o.insert(QStringLiteral("wasFavorite"), true);
        if (!docTagIds.isEmpty())
            o.insert(QStringLiteral("tagIds"), QJsonArray::fromStringList(docTagIds));
    }
    QSaveFile meta(QDir(idDir).filePath(kTrashInfo));
    if (meta.open(QIODevice::WriteOnly)) {
        meta.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        meta.commit();
    }
    if (isDir)
        removeMetadataForFolder(root, folderRel);
    else
        removeMetadataForDocument(src);
    migrateTree(src, dst, files);
    if (outId)
        *outId = id;
    return true;
}

QVector<TrashEntry> LibraryStore::listTrash() {
    QVector<TrashEntry> out;
    for (const QString& root : libraryDirectories()) {
        const QDir dir(trashRoot(root));
        if (!dir.exists())
            continue;
        for (const QFileInfo& sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            TrashEntry e = readEntry(root, sub.absoluteFilePath());
            if (!e.id.isEmpty())
                out.append(e);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const TrashEntry& a, const TrashEntry& b) { return a.deletedAt > b.deletedAt; });
    return out;
}

bool LibraryStore::restoreFromTrash(const QString& id, QString* outRestoredPath, QString* outError) {
    TrashEntry e;
    if (!findEntry(id, &e)) {
        if (outError)
            *outError = QStringLiteral("No se encontró en la papelera.");
        return false;
    }
    // Si la carpeta de origen ya no existe, se vuelve a crear.
    QString parent = e.originalRelative.isEmpty() ? e.rootDir : QDir(e.rootDir).filePath(e.originalRelative);
    if (!QDir().mkpath(parent))
        parent = e.rootDir;

    const QString fileName = QFileInfo(e.itemPath).fileName();
    QString target = QDir(parent).filePath(fileName);
    if (QFileInfo::exists(target)) {
        const QString base = e.isFolder ? fileName : QFileInfo(fileName).completeBaseName();
        const QString suffix = e.isFolder ? QString() : QStringLiteral(".finily");
        for (int n = 2; QFileInfo::exists(target); ++n)
            target = QDir(parent).filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix));
    }
    QJsonObject trashMeta;
    {
        QFile mf(QDir(QFileInfo(e.itemPath).absolutePath()).filePath(kTrashInfo));
        if (mf.open(QIODevice::ReadOnly))
            trashMeta = QJsonDocument::fromJson(mf.readAll()).object();
    }
    const QStringList files = finilyFilesUnder(e.itemPath);
    if (!renameOrCopy(e.itemPath, target)) {
        if (outError)
            *outError = QStringLiteral("No se pudo restaurar.");
        return false;
    }
    migrateTree(e.itemPath, target, files);
    if (e.isFolder) {
        if (trashMeta.value(QStringLiteral("folderWasFavorite")).toBool(false)) {
            const QString restoredRel =
                normalizeRelativeFolder(QDir(e.rootDir).relativeFilePath(target));
            setFolderFavorite(restoredRel, true);
        }
        const QJsonObject childMeta = trashMeta.value(QStringLiteral("childMeta")).toObject();
        if (!childMeta.isEmpty()) {
            const QDir targetBase(target);
            for (const QString& f : finilyFilesUnder(target)) {
                const QJsonObject entryMeta = childMeta.value(targetBase.relativeFilePath(f)).toObject();
                if (entryMeta.value(QStringLiteral("fav")).toBool(false))
                    setDocumentFavorite(f, true);
                for (const QJsonValue& tv : entryMeta.value(QStringLiteral("tags")).toArray()) {
                    const QString tid = tv.toString();
                    if (!tid.isEmpty())
                        setDocumentTag(f, tid, true);
                }
            }
        }
    } else {
        if (trashMeta.value(QStringLiteral("wasFavorite")).toBool(false))
            setDocumentFavorite(target, true);
        for (const QJsonValue& tv : trashMeta.value(QStringLiteral("tagIds")).toArray()) {
            const QString tid = tv.toString();
            if (!tid.isEmpty())
                setDocumentTag(target, tid, true);
        }
    }
    QDir(QFileInfo(e.itemPath).absolutePath()).removeRecursively();
    if (outRestoredPath)
        *outRestoredPath = target;
    return true;
}

bool LibraryStore::deleteFromTrash(const QString& id, QString* outError) {
    TrashEntry e;
    if (!findEntry(id, &e)) {
        if (outError)
            *outError = QStringLiteral("No se encontró en la papelera.");
        return false;
    }
    if (!purgeEntry(e)) {
        if (outError)
            *outError = QStringLiteral("No se pudo eliminar.");
        return false;
    }
    return true;
}

int LibraryStore::emptyTrash() {
    int n = 0;
    for (const TrashEntry& e : listTrash())
        n += purgeEntry(e) ? 1 : 0;
    return n;
}

int LibraryStore::purgeExpiredTrash(int days) {
    if (days < 0)
        days = trashRetentionDays();
    if (days == 0)
        return 0;
    const QDateTime limit = QDateTime::currentDateTimeUtc().addDays(-days);
    int n = 0;
    for (const TrashEntry& e : listTrash())
        if (e.deletedAt.isValid() && e.deletedAt < limit)
            n += purgeEntry(e) ? 1 : 0;
    return n;
}

} // namespace finily
