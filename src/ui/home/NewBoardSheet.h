#pragma once

#include "core/Types.h"
#include "ui/home/GlassSheet.h"

class QButtonGroup;
class QLineEdit;
class QToolButton;

namespace finily {

class BoardHeroPreview;

class NewBoardSheet : public GlassSheet {
    Q_OBJECT

public:
    explicit NewBoardSheet(QWidget* parent = nullptr);

    void openCreate(const QString& folderRelativePath, const HomePalette& pal, bool dark);

signals:
    void boardSubmitted(const finily::NewDocumentSpec& spec);

protected:
    void onConfirmed() override;

private:
    void refreshState();

    NewDocumentSpec m_spec;
    BoardHeroPreview* m_preview = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QButtonGroup* m_themeGroup = nullptr;
    QButtonGroup* m_tplGroup = nullptr;
    QButtonGroup* m_scaleGroup = nullptr;
    QVector<QToolButton*> m_scaleBtns;
};

} // namespace finily
