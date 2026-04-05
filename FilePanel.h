#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <string>



// ─── FilePanel ────────────────────────────────────────────────────────────────
// Un panel de navegación de archivos estilo Norton Commander.
// Si selectable=true muestra checkboxes y botones Select All / None.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(const QString &title, bool selectable, QWidget *parent = nullptr);

    QString     currentPath()   const;
    void        setPath(const QString &path);

    // Solo válido cuando selectable=true
    QStringList selectedFiles() const;   // rutas completas de los archivos marcados
    void        selectAll();
    void        selectNone();

signals:
    void pathChanged(const QString &path);

private slots:
    void onGoUp();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onPathCommitted();

private:
    void populate();

    bool         m_selectable;
    QString      m_currentPath;

    QLabel      *m_titleLabel;
    QLineEdit   *m_pathEdit;
    QPushButton *m_upBtn;
    QListWidget *m_list;
    QPushButton *m_selAllBtn;   // nullptr si !selectable
    QPushButton *m_selNoneBtn;  // nullptr si !selectable
};

