#pragma once

#include "ui/home/HomeTheme.h"

#include <QString>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QLabel;
class QLineEdit;

namespace finily {

class HomeHeader : public QWidget {
    Q_OBJECT

public:
    explicit HomeHeader(QWidget* parent = nullptr);

    void setSectionTitle(const QString& title, const QString& subtitle);
    void setFolderBreadcrumb(const QString& relativePath, const QString& subtitle);
    QString searchText() const;
    void applyTheme(const HomePalette& pal, bool dark);

signals:
    void searchTextChanged(const QString& text);
    void breadcrumbClicked(const QString& relativePath);

private:
    void clearBreadcrumb();

    QHBoxLayout* m_crumbRow = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLineEdit* m_search = nullptr;
    QAction* m_searchAction = nullptr;
};

} // namespace finily
