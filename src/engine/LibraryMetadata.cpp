#include "engine/LibraryStore.h"

#include "core/Utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace finily {
namespace {

constexpr QLatin1StringView kMetadataFile(".finily-library.json");
constexpr int kMaxRecentMoves = 5;

QMutex& metadataMutex() {
    static QMutex mutex;
    return mutex;
}

QString cleanAbsPath(const QString& path) {
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString normalizeDocRel(QString rel) {
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    rel = rel.trimmed();
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);
    while (rel.endsWith(QLatin1Char('/')))
        rel.chop(1);
    if (rel.isEmpty())
        return {};
    const QString cleaned = QDir::cleanPath(rel);
    if (cleaned.isEmpty() || cleaned == QLatin1String(".") || cleaned == QLatin1String("..")
        || cleaned.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(cleaned)) {
        return {};
    }
    return cleaned;
}

QString normalizeFavKey(const QString& key) {
    if (key.startsWith(QStringLiteral("doc:"))) {
        const QString rel = normalizeDocRel(key.mid(4));
        return rel.isEmpty() ? QString() : (QStringLiteral("doc:") + rel);
    }
    if (key.startsWith(QStringLiteral("folder:"))) {
        const QString rel = LibraryStore::normalizeRelativeFolder(key.mid(7));
        return rel.isEmpty() ? QString() : (QStringLiteral("folder:") + rel);
    }
    return {};
}

bool hasPathPrefix(const QString& path, const QString& prefix) {
#ifdef Q_OS_WIN
    return path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return path.startsWith(prefix, Qt::CaseSensitive);
#endif
}

QString owningRoot(const QString& absPath) {
    const QString cleanFile = cleanAbsPath(absPath);
    if (cleanFile.isEmpty())
        return {};
    for (const QString& root : LibraryStore::libraryDirectories()) {
        const QString cleanRoot = cleanAbsPath(root);
        if (!cleanRoot.isEmpty() && hasPathPrefix(cleanFile, cleanRoot + QLatin1Char('/')))
            return cleanRoot;
    }
    return {};
}

QString relativePathInRoot(const QString& cleanRoot, const QString& absPath) {
    const QString cleanFile = cleanAbsPath(absPath);
    if (cleanRoot.isEmpty() || cleanFile.isEmpty())
        return {};
    const QString prefix = cleanRoot + QLatin1Char('/');
    if (!hasPathPrefix(cleanFile, prefix))
        return {};
    return normalizeDocRel(cleanFile.mid(prefix.size()));
}

QString findFolderRoot(const QString& normRel) {
    if (normRel.isEmpty())
        return {};
    for (const QString& root : LibraryStore::libraryDirectories()) {
        const QString candidate = QDir(root).filePath(normRel);
        if (QFileInfo(candidate).isDir())
            return cleanAbsPath(root);
    }
    return {};
}

struct LibraryIndexData {
    QStringList favorites;
    QVector<TagInfo> tags;
    QHash<QString, QStringList> docTags;
    QStringList recentMoves;
};

LibraryIndexData loadIndexUnlocked(const QString& rootDir) {
    LibraryIndexData data;
    if (rootDir.isEmpty())
        return data;
    QFile f(QDir(rootDir).filePath(QString(kMetadataFile)));
    if (!f.open(QIODevice::ReadOnly))
        return data;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return data;
    const QJsonObject root = doc.object();

    QSet<QString> seenFavs;
    for (const QJsonValue& val : root.value(QStringLiteral("favorites")).toArray()) {
        const QString key = normalizeFavKey(val.toString());
        if (!key.isEmpty() && !seenFavs.contains(key)) {
            seenFavs.insert(key);
            data.favorites.append(key);
        }
    }

    QSet<QString> seenTagIds;
    for (const QJsonValue& val : root.value(QStringLiteral("tags")).toArray()) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        const QString id = obj.value(QStringLiteral("id")).toString();
        const QString name = obj.value(QStringLiteral("name")).toString().trimmed();
        if (id.isEmpty() || name.isEmpty() || seenTagIds.contains(id))
            continue;
        seenTagIds.insert(id);
        const int color = std::clamp(obj.value(QStringLiteral("color")).toInt(0),
                                     0, LibraryStore::kCoverColorCount - 1);
        data.tags.append(TagInfo{id, name, color});
    }

    const QJsonObject docTagsObj = root.value(QStringLiteral("docTags")).toObject();
    for (auto it = docTagsObj.constBegin(); it != docTagsObj.constEnd(); ++it) {
        const QString rel = normalizeDocRel(it.key());
        if (rel.isEmpty())
            continue;
        QStringList ids;
        for (const QJsonValue& val : it.value().toArray()) {
            const QString id = val.toString();
            if (!id.isEmpty() && !ids.contains(id))
                ids.append(id);
        }
        if (!ids.isEmpty())
            data.docTags.insert(rel, ids);
    }

    for (const QJsonValue& val : root.value(QStringLiteral("recentMoves")).toArray()) {
        const QString norm = LibraryStore::normalizeRelativeFolder(val.toString());
        if (!norm.isEmpty() && !data.recentMoves.contains(norm)) {
            data.recentMoves.append(norm);
            if (data.recentMoves.size() >= kMaxRecentMoves)
                break;
        }
    }
    return data;
}

bool saveIndexUnlocked(const QString& rootDir, const LibraryIndexData& data) {
    if (rootDir.isEmpty() || !QDir().mkpath(rootDir))
        return false;

    QJsonArray tagsArr;
    for (const TagInfo& tag : data.tags) {
        tagsArr.append(QJsonObject{
            {QStringLiteral("id"), tag.id},
            {QStringLiteral("name"), tag.name},
            {QStringLiteral("color"), std::clamp(tag.colorIndex, 0, LibraryStore::kCoverColorCount - 1)}
        });
    }

    QJsonObject docTagsObj;
    for (auto it = data.docTags.cbegin(); it != data.docTags.cend(); ++it) {
        if (!it.value().isEmpty())
            docTagsObj.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("favorites"), QJsonArray::fromStringList(data.favorites));
    root.insert(QStringLiteral("tags"), tagsArr);
    root.insert(QStringLiteral("docTags"), docTagsObj);
    root.insert(QStringLiteral("recentMoves"), QJsonArray::fromStringList(data.recentMoves));

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(QDir(rootDir).filePath(QString(kMetadataFile)));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QSet<QString> validTagIdsUnlocked(const QString& rootDir, const LibraryIndexData& data) {
    QSet<QString> validIds;
    for (const TagInfo& t : data.tags)
        validIds.insert(t.id);
    if (validIds.isEmpty()) {
        const QString primary = cleanAbsPath(LibraryStore::resolveDirectory());
        if (!primary.isEmpty() && primary != rootDir) {
            for (const TagInfo& t : loadIndexUnlocked(primary).tags)
                validIds.insert(t.id);
        }
    }
    return validIds;
}

bool removeTagFromDocTags(LibraryIndexData& data, const QString& tagId, QStringList* outAffected) {
    bool modified = false;
    for (auto it = data.docTags.begin(); it != data.docTags.end();) {
        if (it.value().removeAll(tagId) > 0) {
            modified = true;
            if (outAffected)
                outAffected->append(it.key());
        }
        if (it.value().isEmpty())
            it = data.docTags.erase(it);
        else
            ++it;
    }
    return modified;
}

} // namespace

QStringList LibraryStore::favoriteKeys() {
    QMutexLocker locker(&metadataMutex());
    QStringList out;
    QSet<QString> seen;
    for (const QString& root : libraryDirectories()) {
        const LibraryIndexData data = loadIndexUnlocked(cleanAbsPath(root));
        for (const QString& key : data.favorites) {
            if (!seen.contains(key)) {
                seen.insert(key);
                out.append(key);
            }
        }
    }
    return out;
}

QStringList LibraryStore::favoriteKeysInDirectory(const QString& rootDir) {
    QMutexLocker locker(&metadataMutex());
    return loadIndexUnlocked(cleanAbsPath(rootDir)).favorites;
}

bool LibraryStore::isDocumentFavorite(const QString& absPath) {
    QMutexLocker locker(&metadataMutex());
    const QString root = owningRoot(absPath);
    const QString rel = relativePathInRoot(root, absPath);
    if (root.isEmpty() || rel.isEmpty())
        return false;
    return loadIndexUnlocked(root).favorites.contains(QStringLiteral("doc:") + rel);
}

bool LibraryStore::setDocumentFavorite(const QString& absPath, bool fav) {
    bool ok = false;
    bool changed = false;
    {
        QMutexLocker locker(&metadataMutex());
        const QString root = owningRoot(absPath);
        const QString rel = relativePathInRoot(root, absPath);
        if (root.isEmpty() || rel.isEmpty())
            return false;
        const QString key = QStringLiteral("doc:") + rel;
        LibraryIndexData data = loadIndexUnlocked(root);
        if (fav) {
            if (data.favorites.contains(key))
                return true;
            data.favorites.append(key);
            changed = true;
        } else {
            if (data.favorites.removeAll(key) == 0)
                return true;
            changed = true;
        }
        ok = saveIndexUnlocked(root, data);
    }
    if (ok && changed)
        invalidateCache(absPath);
    return ok;
}

bool LibraryStore::isFolderFavorite(const QString& relativePath) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (norm.isEmpty())
        return false;
    const QString key = QStringLiteral("folder:") + norm;
    QMutexLocker locker(&metadataMutex());
    for (const QString& root : libraryDirectories()) {
        if (loadIndexUnlocked(cleanAbsPath(root)).favorites.contains(key))
            return true;
    }
    return false;
}

bool LibraryStore::setFolderFavorite(const QString& relativePath, bool fav) {
    const QString norm = normalizeRelativeFolder(relativePath);
    if (norm.isEmpty())
        return false;
    const QString key = QStringLiteral("folder:") + norm;
    QMutexLocker locker(&metadataMutex());
    if (fav) {
        QString root = findFolderRoot(norm);
        if (root.isEmpty())
            root = cleanAbsPath(resolveDirectory());
        if (root.isEmpty())
            return false;
        LibraryIndexData data = loadIndexUnlocked(root);
        if (data.favorites.contains(key))
            return true;
        data.favorites.append(key);
        return saveIndexUnlocked(root, data);
    }
    bool ok = true;
    for (const QString& dir : libraryDirectories()) {
        const QString root = cleanAbsPath(dir);
        LibraryIndexData data = loadIndexUnlocked(root);
        if (data.favorites.removeAll(key) > 0)
            ok = saveIndexUnlocked(root, data) && ok;
    }
    return ok;
}

bool LibraryStore::reorderFavorite(const QString& key, int newIndex) {
    const QString normKey = normalizeFavKey(key);
    if (normKey.isEmpty())
        return false;
    QMutexLocker locker(&metadataMutex());
    for (const QString& dir : libraryDirectories()) {
        const QString root = cleanAbsPath(dir);
        LibraryIndexData data = loadIndexUnlocked(root);
        const qsizetype oldIndex = data.favorites.indexOf(normKey);
        if (oldIndex < 0)
            continue;
        const qsizetype clamped = std::clamp<qsizetype>(newIndex, 0, data.favorites.size() - 1);
        if (clamped == oldIndex)
            return true;
        data.favorites.move(oldIndex, clamped);
        return saveIndexUnlocked(root, data);
    }
    return false;
}

QVector<TagInfo> LibraryStore::listTags() {
    return listTagsInDirectory(resolveDirectory());
}

QVector<TagInfo> LibraryStore::listTagsInDirectory(const QString& rootDir) {
    QMutexLocker locker(&metadataMutex());
    return loadIndexUnlocked(cleanAbsPath(rootDir)).tags;
}

TagInfo LibraryStore::createTag(const QString& name, int colorIndex) {
    const QString clean = name.trimmed();
    if (clean.isEmpty())
        return {};
    const QString root = cleanAbsPath(resolveDirectory());
    if (root.isEmpty())
        return {};
    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    for (const TagInfo& existing : std::as_const(data.tags)) {
        if (existing.name.compare(clean, Qt::CaseInsensitive) == 0)
            return existing;
    }
    const int resolvedColor = (colorIndex < 0)
        ? int(data.tags.size() % kCoverColorCount)
        : std::clamp(colorIndex, 0, kCoverColorCount - 1);
    TagInfo created{freshId(), clean, resolvedColor};
    data.tags.append(created);
    if (!saveIndexUnlocked(root, data))
        return {};
    return created;
}

bool LibraryStore::renameTag(const QString& tagId, const QString& newName) {
    const QString clean = newName.trimmed();
    if (tagId.isEmpty() || clean.isEmpty())
        return false;
    const QString root = cleanAbsPath(resolveDirectory());
    if (root.isEmpty())
        return false;
    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    for (const TagInfo& other : std::as_const(data.tags)) {
        if (other.id != tagId && other.name.compare(clean, Qt::CaseInsensitive) == 0)
            return false;
    }
    for (TagInfo& tag : data.tags) {
        if (tag.id == tagId) {
            if (tag.name == clean)
                return true;
            tag.name = clean;
            return saveIndexUnlocked(root, data);
        }
    }
    return false;
}

bool LibraryStore::setTagColor(const QString& tagId, int colorIndex) {
    if (tagId.isEmpty())
        return false;
    const QString root = cleanAbsPath(resolveDirectory());
    if (root.isEmpty())
        return false;
    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    const int clamped = std::clamp(colorIndex, 0, kCoverColorCount - 1);
    for (TagInfo& tag : data.tags) {
        if (tag.id == tagId) {
            if (tag.colorIndex == clamped)
                return true;
            tag.colorIndex = clamped;
            return saveIndexUnlocked(root, data);
        }
    }
    return false;
}

QStringList LibraryStore::deleteTag(const QString& tagId) {
    if (tagId.isEmpty())
        return {};
    const QString primary = cleanAbsPath(resolveDirectory());
    if (primary.isEmpty())
        return {};
    QStringList affected;
    {
        QMutexLocker locker(&metadataMutex());
        LibraryIndexData primaryData = loadIndexUnlocked(primary);
        const qsizetype prevCount = primaryData.tags.size();
        primaryData.tags.removeIf([&](const TagInfo& t) { return t.id == tagId; });
        const bool removedDef = primaryData.tags.size() != prevCount;
        const bool removedDocs = removeTagFromDocTags(primaryData, tagId, &affected);
        if (removedDef || removedDocs)
            saveIndexUnlocked(primary, primaryData);

        for (const QString& dir : libraryDirectories()) {
            const QString r = cleanAbsPath(dir);
            if (r.isEmpty() || r == primary)
                continue;
            LibraryIndexData secondaryData = loadIndexUnlocked(r);
            if (removeTagFromDocTags(secondaryData, tagId, nullptr))
                saveIndexUnlocked(r, secondaryData);
        }
    }
    std::sort(affected.begin(), affected.end());
    invalidateCache();
    return affected;
}

bool LibraryStore::restoreTag(const TagInfo& tag, const QStringList& relDocPaths) {
    const QString clean = tag.name.trimmed();
    if (tag.id.isEmpty() || clean.isEmpty())
        return false;
    const QString primary = cleanAbsPath(resolveDirectory());
    if (primary.isEmpty())
        return false;
    bool ok = false;
    {
        QMutexLocker locker(&metadataMutex());
        LibraryIndexData data = loadIndexUnlocked(primary);
        const bool hasId = std::any_of(data.tags.cbegin(), data.tags.cend(),
                                       [&](const TagInfo& t) { return t.id == tag.id; });
        if (!hasId) {
            data.tags.append(TagInfo{
                tag.id,
                clean,
                std::clamp(tag.colorIndex, 0, kCoverColorCount - 1)
            });
        }
        for (const QString& rawRel : relDocPaths) {
            const QString rel = normalizeDocRel(rawRel);
            if (rel.isEmpty())
                continue;
            QStringList& list = data.docTags[rel];
            if (!list.contains(tag.id))
                list.append(tag.id);
        }
        ok = saveIndexUnlocked(primary, data);
    }
    if (ok)
        invalidateCache();
    return ok;
}

QStringList LibraryStore::documentTags(const QString& absPath) {
    QMutexLocker locker(&metadataMutex());
    const QString root = owningRoot(absPath);
    const QString rel = relativePathInRoot(root, absPath);
    if (root.isEmpty() || rel.isEmpty())
        return {};
    const LibraryIndexData data = loadIndexUnlocked(root);
    QStringList ids = data.docTags.value(rel);
    if (ids.isEmpty())
        return {};
    const QSet<QString> validIds = validTagIdsUnlocked(root, data);
    ids.removeIf([&](const QString& id) { return !validIds.contains(id); });
    return ids;
}

bool LibraryStore::setDocumentTag(const QString& absPath, const QString& tagId, bool on) {
    if (tagId.isEmpty())
        return false;
    bool ok = false;
    bool changed = false;
    {
        QMutexLocker locker(&metadataMutex());
        const QString root = owningRoot(absPath);
        const QString rel = relativePathInRoot(root, absPath);
        if (root.isEmpty() || rel.isEmpty())
            return false;
        LibraryIndexData data = loadIndexUnlocked(root);
        if (on) {
            if (!validTagIdsUnlocked(root, data).contains(tagId))
                return false;
            QStringList& list = data.docTags[rel];
            if (list.contains(tagId))
                return true;
            list.append(tagId);
            changed = true;
            ok = saveIndexUnlocked(root, data);
        } else {
            auto it = data.docTags.find(rel);
            if (it == data.docTags.end() || it->removeAll(tagId) == 0)
                return true;
            if (it->isEmpty())
                data.docTags.erase(it);
            changed = true;
            ok = saveIndexUnlocked(root, data);
        }
    }
    if (ok && changed)
        invalidateCache(absPath);
    return ok;
}

QStringList LibraryStore::recentMoveFolders() {
    const QString root = cleanAbsPath(resolveDirectory());
    if (root.isEmpty())
        return {};
    QMutexLocker locker(&metadataMutex());
    return loadIndexUnlocked(root).recentMoves;
}

void LibraryStore::recordRecentMoveFolder(const QString& relativeFolder) {
    const QString norm = normalizeRelativeFolder(relativeFolder);
    if (norm.isEmpty())
        return;
    const QString root = cleanAbsPath(resolveDirectory());
    if (root.isEmpty())
        return;
    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    data.recentMoves.removeAll(norm);
    data.recentMoves.prepend(norm);
    while (data.recentMoves.size() > kMaxRecentMoves)
        data.recentMoves.removeLast();
    saveIndexUnlocked(root, data);
}

LibraryMetadataSnapshot LibraryStore::metadataSnapshotForDirectory(const QString& rootDir) {
    const QString root = cleanAbsPath(rootDir);
    if (root.isEmpty())
        return {};
    QMutexLocker locker(&metadataMutex());
    const LibraryIndexData data = loadIndexUnlocked(root);
    LibraryMetadataSnapshot snap;
    snap.favoriteKeys = QSet<QString>(data.favorites.cbegin(), data.favorites.cend());
    const QSet<QString> validIds = validTagIdsUnlocked(root, data);
    for (auto it = data.docTags.cbegin(); it != data.docTags.cend(); ++it) {
        QStringList filtered = it.value();
        filtered.removeIf([&](const QString& id) { return !validIds.contains(id); });
        if (!filtered.isEmpty())
            snap.docTags.insert(it.key(), filtered);
    }
    return snap;
}

void LibraryStore::migrateMetadataForDocument(const QString& oldAbsPath, const QString& newAbsPath) {
    QMutexLocker locker(&metadataMutex());
    const QString oldRoot = owningRoot(oldAbsPath);
    const QString oldRel = relativePathInRoot(oldRoot, oldAbsPath);
    if (oldRoot.isEmpty() || oldRel.isEmpty())
        return;
    const QString newRoot = owningRoot(newAbsPath);
    const QString newRel = relativePathInRoot(newRoot, newAbsPath);
    const QString oldFav = QStringLiteral("doc:") + oldRel;
    const QString newFav = newRel.isEmpty() ? QString() : (QStringLiteral("doc:") + newRel);

    if (oldRoot == newRoot) {
        if (newRel.isEmpty() || oldRel == newRel)
            return;
        LibraryIndexData data = loadIndexUnlocked(oldRoot);
        bool changed = false;
        for (qsizetype i = 0; i < data.favorites.size(); ++i) {
            if (data.favorites.at(i) == oldFav) {
                data.favorites[i] = newFav;
                changed = true;
            }
        }
        if (data.docTags.contains(oldRel)) {
            const QStringList tags = data.docTags.take(oldRel);
            if (!tags.isEmpty())
                data.docTags.insert(newRel, tags);
            changed = true;
        }
        if (changed)
            saveIndexUnlocked(oldRoot, data);
        return;
    }

    LibraryIndexData oldData = loadIndexUnlocked(oldRoot);
    const bool wasFav = oldData.favorites.removeAll(oldFav) > 0;
    const QStringList tags = oldData.docTags.take(oldRel);
    if (wasFav || !tags.isEmpty())
        saveIndexUnlocked(oldRoot, oldData);
    if (!newRoot.isEmpty() && !newRel.isEmpty() && (wasFav || !tags.isEmpty())) {
        LibraryIndexData newData = loadIndexUnlocked(newRoot);
        if (wasFav && !newData.favorites.contains(newFav))
            newData.favorites.append(newFav);
        if (!tags.isEmpty())
            newData.docTags.insert(newRel, tags);
        saveIndexUnlocked(newRoot, newData);
    }
}

void LibraryStore::removeMetadataForDocument(const QString& absPath) {
    QMutexLocker locker(&metadataMutex());
    const QString root = owningRoot(absPath);
    const QString rel = relativePathInRoot(root, absPath);
    if (root.isEmpty() || rel.isEmpty())
        return;
    LibraryIndexData data = loadIndexUnlocked(root);
    bool changed = data.favorites.removeAll(QStringLiteral("doc:") + rel) > 0;
    changed = (data.docTags.remove(rel) > 0) || changed;
    if (changed)
        saveIndexUnlocked(root, data);
}

void LibraryStore::migrateMetadataForFolder(const QString& rootDir, const QString& oldRelPath,
                                            const QString& newRelPath) {
    const QString root = cleanAbsPath(rootDir);
    const QString oldNorm = normalizeRelativeFolder(oldRelPath);
    const QString newNorm = normalizeRelativeFolder(newRelPath);
    if (root.isEmpty() || oldNorm.isEmpty() || newNorm.isEmpty() || oldNorm == newNorm)
        return;

    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    bool changed = false;

    const QString oldFolderFav = QStringLiteral("folder:") + oldNorm;
    const QString newFolderFav = QStringLiteral("folder:") + newNorm;
    const QString oldFolderPrefix = oldFolderFav + QLatin1Char('/');
    const QString oldDocPrefix = QStringLiteral("doc:") + oldNorm + QLatin1Char('/');
    const QString newDocPrefix = QStringLiteral("doc:") + newNorm + QLatin1Char('/');
    for (qsizetype i = 0; i < data.favorites.size(); ++i) {
        const QString& fav = data.favorites.at(i);
        if (fav == oldFolderFav) {
            data.favorites[i] = newFolderFav;
            changed = true;
        } else if (fav.startsWith(oldFolderPrefix)) {
            data.favorites[i] = newFolderFav + QLatin1Char('/') + fav.mid(oldFolderPrefix.size());
            changed = true;
        } else if (fav.startsWith(oldDocPrefix)) {
            data.favorites[i] = newDocPrefix + fav.mid(oldDocPrefix.size());
            changed = true;
        }
    }

    const QString oldRelPrefix = oldNorm + QLatin1Char('/');
    const QString newRelPrefix = newNorm + QLatin1Char('/');
    QHash<QString, QStringList> updatedDocTags;
    bool docTagsChanged = false;
    for (auto it = data.docTags.cbegin(); it != data.docTags.cend(); ++it) {
        if (it.key().startsWith(oldRelPrefix)) {
            updatedDocTags.insert(newRelPrefix + it.key().mid(oldRelPrefix.size()), it.value());
            docTagsChanged = true;
        } else {
            updatedDocTags.insert(it.key(), it.value());
        }
    }
    if (docTagsChanged) {
        data.docTags = std::move(updatedDocTags);
        changed = true;
    }

    for (qsizetype i = 0; i < data.recentMoves.size(); ++i) {
        const QString& recent = data.recentMoves.at(i);
        if (recent == oldNorm) {
            data.recentMoves[i] = newNorm;
            changed = true;
        } else if (recent.startsWith(oldRelPrefix)) {
            data.recentMoves[i] = newRelPrefix + recent.mid(oldRelPrefix.size());
            changed = true;
        }
    }
    if (changed)
        saveIndexUnlocked(root, data);
}

void LibraryStore::removeMetadataForFolder(const QString& rootDir, const QString& relPath) {
    const QString root = cleanAbsPath(rootDir);
    const QString norm = normalizeRelativeFolder(relPath);
    if (root.isEmpty() || norm.isEmpty())
        return;

    QMutexLocker locker(&metadataMutex());
    LibraryIndexData data = loadIndexUnlocked(root);
    const QString folderFav = QStringLiteral("folder:") + norm;
    const QString folderPrefix = folderFav + QLatin1Char('/');
    const QString docPrefix = QStringLiteral("doc:") + norm + QLatin1Char('/');
    const qsizetype prevFavs = data.favorites.size();
    data.favorites.removeIf([&](const QString& fav) {
        return fav == folderFav || fav.startsWith(folderPrefix) || fav.startsWith(docPrefix);
    });
    bool changed = data.favorites.size() != prevFavs;

    const QString relPrefix = norm + QLatin1Char('/');
    for (auto it = data.docTags.begin(); it != data.docTags.end();) {
        if (it.key().startsWith(relPrefix)) {
            it = data.docTags.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    const qsizetype prevRecent = data.recentMoves.size();
    data.recentMoves.removeIf([&](const QString& recent) {
        return recent == norm || recent.startsWith(relPrefix);
    });
    changed = changed || (data.recentMoves.size() != prevRecent);
    if (changed)
        saveIndexUnlocked(root, data);
}

} // namespace finily
