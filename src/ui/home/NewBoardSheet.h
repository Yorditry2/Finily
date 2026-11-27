#pragma once

#include "core/Types.h"
#include "ui/home/GlassSheet.h"

namespace finily {

class SheetHero;
class SwatchRow;
class TemplateRow;
class MmRuler;
class InfoChip;

// Tarjeta lateral «Nueva pizarra infinita» (mockup 00, escena 07).
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
    SheetHero* m_hero = nullptr;
    SwatchRow* m_swatches = nullptr;
    QLabel* m_hex = nullptr;
    TemplateRow* m_templates = nullptr;
    MmRuler* m_ruler = nullptr;
    InfoChip* m_folderChip = nullptr;
    QWidget* m_cta = nullptr;
};

} // namespace finily
