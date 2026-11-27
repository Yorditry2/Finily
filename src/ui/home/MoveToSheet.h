#pragma once

#include "engine/LibraryStore.h"
#include "ui/home/GlassSheet.h"

#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

namespace finily {

class MoveSearchBox;
class MoveTreeList;
class MoveNewFolderBar;

// Tarjeta lateral «Mover a…» (mockup 07, escena 05).
class MoveToSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit MoveToSheet(QWidget* parent = nullptr);

    void openMove(const QString& titleLabel, const QString& currentRel,
                  const QVector<FolderMeta>& folders, const HomePalette& pal, bool dark);

signals:
    void moveSubmitted(const QString& targetRelativePath);
    void folderCreated(const QString& newRelativePath);

protected:
    bool validateBeforeAccept() override;
    void onConfirmed() override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyDark(bool dark);
    void rebuildList(bool adjustHeight = false);
    void refreshCta();
    void expandNewFolderInput();
    void collapseNewFolderInput();
    void submitNewFolder();
    QString destinationLabel() const;

    QString m_currentRel;
    QString m_selectedRel;
    bool m_hasSelection = false;
    QVector<FolderMeta> m_folders;

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeBtn = nullptr;
    MoveSearchBox* m_searchBox = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QScrollArea* m_scroll = nullptr;
    MoveTreeList* m_tree = nullptr;
    MoveNewFolderBar* m_newFolderBar = nullptr;
    QLineEdit* m_newFolderEdit = nullptr;
    QPushButton* m_cta = nullptr;
};

} // namespace finily
