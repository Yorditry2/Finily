#include "engine/LibraryStore.h"

#include "core/Utils.h"
#include "engine/DocumentSerializer.h"
#include "engine/NotebookDocument.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace finily {
namespace {

constexpr qsizetype kMaxNotebookMetaCache = 128;

struct CachedNotebookMeta {
    QDateTime modified;
    qint64 size = -1;
    quint64 lastAccess = 0;
    NotebookMeta meta;
};

QMutex& metaCacheMutex() {
    static QMutex mutex;
    return mutex;
}

QHash<QString, CachedNotebookMeta>& notebookMetaCache() {
    static QHash<QString, CachedNotebookMeta> cache;
    return cache;
}

quint64 nextMetaCacheAccess() {
    static quint64 seq = 0;
    return ++seq;
}

QString coverKey(const QString& path) {
    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("covers/") + QString::fromLatin1(hash.toHex().left(16));
}

QString legacyCoverKey(const QString& path) {
    return QStringLiteral("covers/") + QFileInfo(path).fileName();
}

qint64 countNonOverlapping(const QByteArray& buf, const QByteArray& needle, qsizetype start) {
    qint64 n = 0;
    qsizetype pos = start;
    while ((pos = buf.indexOf(needle, pos)) != -1) {
        ++n;
        pos += needle.size();
    }
    return n;
}

NotebookMeta readNotebookMetaCached(const QFileInfo& info, const QString& folderRel) {
    const QString absPath = info.absoluteFilePath();
    const QDateTime mod = info.lastModified();
    const qint64 fsize = info.size();
    {
        QMutexLocker locker(&metaCacheMutex());
        auto& cache = notebookMetaCache();
        auto it = cache.find(absPath);
        if (it != cache.end() && it->modified == mod && it->size == fsize) {
            it->lastAccess = nextMetaCacheAccess();
            it->meta.title = info.completeBaseName();
            it->meta.folderRelativePath = folderRel;
            return it->meta;
        }
    }

    NotebookMeta meta;
    meta.path = absPath;
    meta.title = info.completeBaseName();
    meta.folderRelativePath = folderRel;
    meta.created = info.birthTime().isValid() ? info.birthTime() : mod;
    meta.modified = mod;

    QFile file(meta.path);
    if (!file.open(QIODevice::ReadOnly))
        return meta;

    static const QByteArray strokesNeedle = "\"strokes\":[";
    static const QByteArray pdfNeedle = "\"isPdfPage\":true";
    static const qsizetype carrySize = std::max(strokesNeedle.size(), pdfNeedle.size()) - 1;
    qint64 pages = 0;
    qint64 pdfPages = 0;
    QByteArray carry;
    while (!file.atEnd()) {
        const qsizetype prevCarry = carry.size();
        const QByteArray window = carry + file.read(1 << 20);
        const qsizetype strokesStart = std::max<qsizetype>(0, prevCarry - strokesNeedle.size() + 1);
        const qsizetype pdfStart = std::max<qsizetype>(0, prevCarry - pdfNeedle.size() + 1);
        pages += countNonOverlapping(window, strokesNeedle, strokesStart);
        pdfPages += countNonOverlapping(window, pdfNeedle, pdfStart);
        carry = window.right(carrySize);
    }
    file.close();

    const NotebookFileMeta fileMeta = readNotebookMetadata(meta.path);
    int embeddedCover = -1;
    if (fileMeta.found) {
        meta.theme = fileMeta.themeId;
        meta.tpl = fileMeta.tpl;
        meta.pageFormat = fileMeta.pageFormat;
        meta.templateScale = fileMeta.templateScale;
        meta.customBackground = fileMeta.customBackground;
        embeddedCover = fileMeta.coverColorIndex;
        meta.coverIconIndex = std::clamp(fileMeta.coverIconIndex, 0, LibraryStore::kIconCount - 1);
    }

    meta.coverColorIndex = LibraryStore::resolveCoverColor(meta.path, meta.title, embeddedCover);
    meta.pages = std::max(1, int(pages));
    meta.hasPdf = pdfPages > 0;
    meta.preview = loadOrGenerateCoverThumbnail(meta.path, meta.modified);

    QMutexLocker locker(&metaCacheMutex());
    auto& cache = notebookMetaCache();
    while (!cache.contains(absPath) && cache.size() >= kMaxNotebookMetaCache) {
        auto oldest = cache.begin();
        for (auto cur = cache.begin(); cur != cache.end(); ++cur) {
            if (cur->lastAccess < oldest->lastAccess)
                oldest = cur;
        }
        cache.erase(oldest);
    }
    cache.insert(absPath, CachedNotebookMeta{mod, fsize, nextMetaCacheAccess(), meta});
    return meta;
}

bool isActiveUiTemplate(int tplValue) {
    return tplValue == int(BackgroundTemplate::Blank)
        || tplValue == int(BackgroundTemplate::Grid)
        || tplValue == int(BackgroundTemplate::Dots)
        || tplValue == int(BackgroundTemplate::Ruled);
}

bool pageObjectHasContent(const QJsonObject& page) {
    return !page.value(QStringLiteral("strokes")).toArray().isEmpty()
        || !page.value(QStringLiteral("shapes")).toArray().isEmpty()
        || !page.value(QStringLiteral("images")).toArray().isEmpty()
        || !page.value(QStringLiteral("texts")).toArray().isEmpty();
}

struct RecursiveNotebookEntry {
    QFileInfo info;
    QString folderRel;
    QString rootDir;
};

void collectFinilyFilesRecursive(const QString& rootDir, const QString& currentDir,
                                 const QString& currentRel,
                                 QVector<RecursiveNotebookEntry>& out) {
    const QDir dir(currentDir);
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.finily")}, QDir::Files))
        out.append({fi, currentRel, rootDir});
    const auto subdirs =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& sub : subdirs) {
        if (sub.fileName().startsWith(QLatin1Char('.')))
            continue;
        const QString nextRel = currentRel.isEmpty()
            ? sub.fileName()
            : (currentRel + QLatin1Char('/') + sub.fileName());
        collectFinilyFilesRecursive(rootDir, sub.absoluteFilePath(), nextRel, out);
    }
}

template <typename Mutator>
bool updateNotebookRootJson(const QString& path, Mutator&& mutate) {
    if (path.isEmpty())
        return false;
    QJsonObject root = loadDocumentFromFile(path);
    if (root.isEmpty())
        return false;

    const QDateTime origMtime = QFileInfo(path).lastModified();
    mutate(root);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return false;

    if (origMtime.isValid()) {
        QFile f(path);
        if (f.open(QIODevice::ReadWrite)) {
            f.setFileTime(origMtime, QFileDevice::FileModificationTime);
            f.close();
        }
    }
    LibraryStore::invalidateCache(path);
    return true;
}

void applyMetadataFromSnapshot(NotebookMeta& meta, const LibraryMetadataSnapshot& snap,
                               const QString& relDocPath) {
    meta.isFavorite = snap.favoriteKeys.contains(QStringLiteral("doc:") + relDocPath);
    meta.tagIds = snap.docTags.value(relDocPath);
}

} // namespace

QStringList LibraryStore::libraryDirectories() {
    // Pruebas y capturas de desarrollo: una biblioteca aislada.
    const QString overrideDir = qEnvironmentVariable("FINILY_LIBRARY_DIR");
    if (!overrideDir.isEmpty())
        return {overrideDir};
    const QString customDir = customLibraryDirectory();
    if (!customDir.isEmpty()) {
        const QFileInfo fi(customDir);
        if ((fi.exists() && fi.isDir()) || (!fi.exists() && QDir().mkpath(customDir)))
            return {QDir::cleanPath(fi.absoluteFilePath())};
    }
    QStringList directories;
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!documents.isEmpty())
        directories.append(QDir(documents).filePath(QStringLiteral("Finily")));
#ifdef Q_OS_WIN
    const auto localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!localData.isEmpty())
        directories.append(QDir(localData).filePath(QStringLiteral("Cuadernos")));
#endif
    return directories;
}

QString LibraryStore::customLibraryDirectory() {
    return readAppSetting(QStringLiteral("library/customDirectory")).toString().trimmed();
}

void LibraryStore::setCustomLibraryDirectory(const QString& dir) {
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty())
        removeAppSetting(QStringLiteral("library/customDirectory"));
    else
        writeAppSetting(QStringLiteral("library/customDirectory"), QDir::cleanPath(trimmed));
    invalidateCache();
}

int LibraryStore::trashRetentionDays() {
    bool ok = false;
    const int days = readAppSetting(QStringLiteral("library/trashRetentionDays"),
                                    kTrashRetentionDays).toInt(&ok);
    return ok ? std::max(0, days) : kTrashRetentionDays;
}

void LibraryStore::setTrashRetentionDays(int days) {
    writeAppSetting(QStringLiteral("library/trashRetentionDays"), std::max(0, days));
}

QString LibraryStore::normalizeRelativeFolder(QString relativeFolder) {
    relativeFolder.replace(QLatin1Char('\\'), QLatin1Char('/'));
    relativeFolder = relativeFolder.trimmed();
    while (relativeFolder.startsWith(QLatin1Char('/')))
        relativeFolder.remove(0, 1);
    while (relativeFolder.endsWith(QLatin1Char('/')))
        relativeFolder.chop(1);
    if (relativeFolder.isEmpty())
        return {};
    const QString cleaned = QDir::cleanPath(relativeFolder);
    if (cleaned.isEmpty() || cleaned == QLatin1String(".") || cleaned == QLatin1String("..")
        || cleaned.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(cleaned)) {
        return {};
    }
    const QStringList parts = cleaned.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part.startsWith(QLatin1Char('.')) || part == QLatin1String(".."))
            return {};
    }
    return parts.join(QLatin1Char('/'));
}

QString LibraryStore::resolveDirectory(const QString& relativeFolder) {
    const QStringList dirs = libraryDirectories();
    if (dirs.isEmpty())
        return {};
    const QString norm = normalizeRelativeFolder(relativeFolder);
    return norm.isEmpty() ? dirs.first() : QDir(dirs.first()).filePath(norm);
}

QString LibraryStore::sanitizeBaseName(QString name) {
    static const QRegularExpression invalid(QStringLiteral(R"([\\/:*?"<>|\x00-\x1f])"));
    name.remove(invalid);
    name = name.simplified();
#ifdef Q_OS_WIN
    static const QRegularExpression trailing(QStringLiteral(R"([. ]+$)"));
    static const QRegularExpression reserved(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|(?:COM|LPT)[1-9¹²³])$)"),
        QRegularExpression::CaseInsensitiveOption);
    name.remove(trailing);
    if (reserved.match(name.section(QLatin1Char('.'), 0, 0).trimmed()).hasMatch())
        name.prepend(QLatin1Char('_'));
#endif
    return name;
}

QString LibraryStore::uniqueNotebookPath(const QString& directory, const QString& baseName) {
    const QDir dir(directory);
    QString path = dir.filePath(baseName + QStringLiteral(".finily"));
    int suffix = 2;
    while (QFileInfo::exists(path))
        path = dir.filePath(baseName + QStringLiteral(" (%1).finily").arg(suffix++));
    return path;
}

QString LibraryStore::defaultQuickNoteTitle(const QDateTime& now) {
    const QLocale spanish(QLocale::Spanish);
    QString stamp = spanish.toString(now, QStringLiteral("d MMM HH.mm"));
    stamp.remove(QLatin1Char(':'));
    const QString clean = sanitizeBaseName(QStringLiteral("Nota rápida · %1").arg(stamp.simplified()));
    return clean.isEmpty() ? QStringLiteral("Nota rápida") : clean;
}

QVector<NotebookMeta> LibraryStore::listNotebooks(const QString& relativeFolder) {
    return listNotebooksInDirectories(libraryDirectories(), relativeFolder);
}

QVector<NotebookMeta> LibraryStore::listNotebooksInDirectories(const QStringList& directories,
                                                               const QString& relativeFolder) {
    const QString norm = normalizeRelativeFolder(relativeFolder);
    struct NotebookEntry {
        QFileInfo info;
        QString rootDir;
    };
    QVector<NotebookEntry> entries;
    QHash<QString, LibraryMetadataSnapshot> snapshots;
    for (const QString& directory : directories) {
        if (!snapshots.contains(directory))
            snapshots.insert(directory, metadataSnapshotForDirectory(directory));
        const QString targetDir = norm.isEmpty() ? directory : QDir(directory).filePath(norm);
        for (const QFileInfo& fi : QDir(targetDir).entryInfoList({QStringLiteral("*.finily")}, QDir::Files))
            entries.append({fi, directory});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const NotebookEntry& a, const NotebookEntry& b) {
        return a.info.lastModified() > b.info.lastModified();
    });
    QVector<NotebookMeta> result;
    result.reserve(entries.size());
    for (const NotebookEntry& entry : entries) {
        NotebookMeta meta = readNotebookMetaCached(entry.info, norm);
        const QString relDoc = norm.isEmpty()
            ? entry.info.fileName()
            : (norm + QLatin1Char('/') + entry.info.fileName());
        applyMetadataFromSnapshot(meta, snapshots.value(entry.rootDir), relDoc);
        result.append(std::move(meta));
    }
    return result;
}

QVector<NotebookMeta> LibraryStore::listAllNotebooksRecursive() {
    return listAllNotebooksRecursiveInDirectories(libraryDirectories());
}

QVector<NotebookMeta> LibraryStore::listAllNotebooksRecursiveInDirectories(
    const QStringList& directories) {
    QVector<RecursiveNotebookEntry> entries;
    QHash<QString, LibraryMetadataSnapshot> snapshots;
    for (const QString& directory : directories) {
        if (!snapshots.contains(directory))
            snapshots.insert(directory, metadataSnapshotForDirectory(directory));
        if (QDir(directory).exists())
            collectFinilyFilesRecursive(directory, directory, QString(), entries);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const RecursiveNotebookEntry& a, const RecursiveNotebookEntry& b) {
        return a.info.lastModified() > b.info.lastModified();
    });
    QVector<NotebookMeta> result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        NotebookMeta meta = readNotebookMetaCached(entry.info, entry.folderRel);
        const QString relDoc = entry.folderRel.isEmpty()
            ? entry.info.fileName()
            : (entry.folderRel + QLatin1Char('/') + entry.info.fileName());
        applyMetadataFromSnapshot(meta, snapshots.value(entry.rootDir), relDoc);
        result.append(std::move(meta));
    }
    return result;
}

int LibraryStore::resolveCoverColor(const QString& path, const QString& title, int embeddedColorIndex) {
    if (embeddedColorIndex >= 0 && embeddedColorIndex < kCoverColorCount)
        return embeddedColorIndex;
    const auto settings = appSettings();
    QVariant stored = settings->value(coverKey(path));
    if (!stored.isValid())
        stored = settings->value(legacyCoverKey(path));
    if (stored.isValid())
        return std::clamp(stored.toInt(), 0, kCoverColorCount - 1);
    return int(qHash(title) % uint(kCoverColorCount - 1));
}

bool LibraryStore::setCoverColor(const QString& path, int colorIndex) {
    const NotebookFileMeta meta = readNotebookMetadata(path);
    return setCoverStyle(path, CoverStyle{std::clamp(colorIndex, 0, kCoverColorCount - 1),
                                          std::clamp(meta.coverIconIndex, 0, kIconCount - 1)});
}

bool LibraryStore::setCoverStyle(const QString& path, CoverStyle style) {
    const int colorClamped = std::clamp(style.colorIndex, 0, kCoverColorCount - 1);
    const int iconClamped = std::clamp(style.iconIndex, 0, kIconCount - 1);
    const bool ok = updateNotebookRootJson(path, [&](QJsonObject& root) {
        root.insert(QStringLiteral("coverColor"), colorClamped);
        if (iconClamped > 0)
            root.insert(QStringLiteral("coverIcon"), iconClamped);
        else
            root.remove(QStringLiteral("coverIcon"));
    });
    if (ok)
        appSettings()->setValue(coverKey(path), colorClamped);
    return ok;
}

bool LibraryStore::setBoardStyle(const QString& path, ThemeId theme,
                                 const QColor& customBackground, BackgroundTemplate tpl,
                                 double templateScale) {
    const int themeClamped = std::clamp(int(theme), 0, int(ThemeId::PaperCream));
    const int tplClamped = std::clamp(int(tpl), 0, int(BackgroundTemplate::School));
    const double scaleClamped = clampTemplateScale(templateScale);
    const bool ok = updateNotebookRootJson(path, [&](QJsonObject& root) {
        root.insert(QStringLiteral("theme"), themeClamped);
        if (customBackground.isValid())
            root.insert(QStringLiteral("customBackground"), customBackground.name(QColor::HexRgb));
        else
            root.remove(QStringLiteral("customBackground"));
        root.insert(QStringLiteral("template"), tplClamped);
        root.insert(QStringLiteral("templateScale"), scaleClamped);
    });
    if (ok)
        removeCoverThumbnail(path);
    return ok;
}

void LibraryStore::migrateCoverSettings(const QString& oldPath, const QString& newPath) {
    const auto settings = appSettings();
    const QString oldKey = coverKey(oldPath);
    const QString oldLegacyKey = legacyCoverKey(oldPath);
    QVariant cover = settings->value(oldKey);
    if (!cover.isValid())
        cover = settings->value(oldLegacyKey);
    if (cover.isValid())
        settings->setValue(coverKey(newPath), cover);
    settings->remove(oldKey);
    settings->remove(oldLegacyKey);
}

void LibraryStore::removeCoverSettings(const QString& path) {
    const auto settings = appSettings();
    settings->remove(coverKey(path));
    settings->remove(legacyCoverKey(path));
}

bool LibraryStore::renameNotebook(const QString& oldPath, const QString& rawNewName,
                                  QString* outNewPath, QString* outError) {
    if (oldPath.isEmpty())
        return false;
    QString clean = sanitizeBaseName(rawNewName);
    if (clean.isEmpty())
        clean = QStringLiteral("Cuaderno sin título");
    const QString dir = QFileInfo(oldPath).absolutePath();
    const QString target = QDir(dir).filePath(clean + QStringLiteral(".finily"));
    if (target == oldPath) {
        if (outNewPath)
            *outNewPath = oldPath;
        return true;
    }
    const QString newPath = uniqueNotebookPath(dir, clean);
    if (!QFile::rename(oldPath, newPath)) {
        if (outError)
            *outError = QStringLiteral("No se pudo renombrar el cuaderno.");
        return false;
    }
    migrateCoverSettings(oldPath, newPath);
    moveCoverThumbnail(oldPath, newPath);
    migrateMetadataForDocument(oldPath, newPath);
    invalidateCache(oldPath);
    invalidateCache(newPath);
    if (outNewPath)
        *outNewPath = newPath;
    return true;
}

bool LibraryStore::duplicateNotebook(const QString& path, QString* outNewPath, QString* outError) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.exists() || !info.isFile()) {
        if (outError)
            *outError = QStringLiteral("El cuaderno no existe.");
        return false;
    }
    const QString newPath = uniqueNotebookPath(
        info.absolutePath(), info.completeBaseName() + QStringLiteral(" (copia)"));
    if (!QFile::copy(path, newPath)) {
        if (outError)
            *outError = QStringLiteral("No se pudo duplicar el cuaderno.");
        return false;
    }
    const auto settings = appSettings();
    QVariant cover = settings->value(coverKey(path));
    if (!cover.isValid())
        cover = settings->value(legacyCoverKey(path));
    if (cover.isValid())
        settings->setValue(coverKey(newPath), cover);

    const QString oldThumb = coverThumbnailPath(path);
    if (QFileInfo::exists(oldThumb)) {
        const QString newThumb = coverThumbnailPath(newPath);
        QDir().mkpath(QFileInfo(newThumb).absolutePath());
        QFile::remove(newThumb);
        QFile::copy(oldThumb, newThumb);
    }
    invalidateCache(newPath);
    if (outNewPath)
        *outNewPath = newPath;
    return true;
}

bool LibraryStore::deleteNotebook(const QString& path, QString* outError) {
    if (path.isEmpty())
        return false;
    if (!QFile::remove(path)) {
        if (outError)
            *outError = QStringLiteral("No se pudo eliminar el archivo. Revisa los permisos de la carpeta.");
        return false;
    }
    removeCoverSettings(path);
    removeCoverThumbnail(path);
    removeMetadataForDocument(path);
    invalidateCache(path);
    return true;
}

bool LibraryStore::exportNotebookToPdf(const QString& finilyPath, const QString& pdfPath,
                                       QString* outError) {
    if (finilyPath.isEmpty() || pdfPath.isEmpty()) {
        if (outError)
            *outError = QStringLiteral("Ruta de archivo no válida.");
        return false;
    }
    NotebookDocument doc;
    if (!doc.loadFromFile(finilyPath)) {
        if (outError)
            *outError = QStringLiteral("No se pudo abrir el cuaderno.");
        return false;
    }
    if (!doc.exportToPdf(pdfPath)) {
        if (outError)
            *outError = QStringLiteral("No se pudo exportar a PDF.");
        return false;
    }
    return true;
}

NewDocumentSpec LibraryStore::lastUsedQuickNoteSpec() {
    const auto settings = appSettings();
    const int themeInt = std::clamp(settings->value(QStringLiteral("home/lastTheme"),
                                                    int(ThemeId::DarkBoard)).toInt(),
                                    0, int(ThemeId::PaperCream));
    const int tplInt = settings->value(QStringLiteral("home/lastTemplate"),
                                       int(BackgroundTemplate::Dots)).toInt();
    const BackgroundTemplate tpl = isActiveUiTemplate(tplInt)
        ? static_cast<BackgroundTemplate>(tplInt)
        : BackgroundTemplate::Dots;
    const double scale = clampTemplateScale(
        settings->value(QStringLiteral("home/lastTemplateScale"), 1.0).toDouble());

    NewDocumentSpec spec;
    spec.kind = DocumentKind::QuickNote;
    spec.title = defaultQuickNoteTitle();
    spec.theme = static_cast<ThemeId>(themeInt);
    spec.tpl = tpl;
    spec.format = PageFormat::Infinite;
    spec.templateScale = scale;
    spec.cover = CoverStyle{-1, 0};
    spec.ephemeralIfEmpty = true;
    return spec;
}

void LibraryStore::saveLastUsedSpec(const NewDocumentSpec& spec) {
    const auto settings = appSettings();
    settings->setValue(QStringLiteral("home/lastTheme"), int(spec.theme));
    if (isActiveUiTemplate(int(spec.tpl)))
        settings->setValue(QStringLiteral("home/lastTemplate"), int(spec.tpl));
    settings->setValue(QStringLiteral("home/lastTemplateScale"),
                       clampTemplateScale(spec.templateScale));
}

bool LibraryStore::hasRecoverableDraft() {
    const QString autosave = autosavePath();
    if (!QFileInfo::exists(autosave))
        return false;
    const QJsonObject root = loadDocumentFromFile(autosave);
    if (root.isEmpty())
        return false;
    const QString sessionPath = root.value(QStringLiteral("sessionFilePath")).toString();
    const bool sessionDirty = root.value(QStringLiteral("sessionDirty")).toBool(false);
    if (!sessionPath.isEmpty())
        return sessionDirty;

    if (root.value(QStringLiteral("version")).toInt() == 5) {
        const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
        if (pages.size() > 1)
            return true;
        for (const auto& p : pages) {
            if (p.isObject() && pageObjectHasContent(p.toObject()))
                return true;
        }
        return false;
    }
    return pageObjectHasContent(root);
}

void LibraryStore::invalidateCache(const QString& path) {
    QMutexLocker locker(&metaCacheMutex());
    if (path.isEmpty())
        notebookMetaCache().clear();
    else
        notebookMetaCache().remove(QFileInfo(path).absoluteFilePath());
}

} // namespace finily
