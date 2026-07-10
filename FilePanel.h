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



// Un panel de navegación de archivos estilo Norton Commander.
// Si selectable=true muestra checkboxes y botones Select All / None.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(const QString &title, bool selectable, QWidget *parent = nullptr,
                       bool allowTrackAssignment = true, bool allowCreateFolder = false);

    QString     currentPath()   const;
    void        setPath(const QString &path);

    // Solo válido cuando selectable=true
    QStringList selectedFiles() const;   // rutas completas de los archivos marcados
    // Retorna pares (rutaCompleta, nombreDestino) para los archivos marcados con track asignado
    QList<QPair<QString,QString>> selectedFilesWithNames() const;
    void        selectAll();
    void        selectNone();

signals:
    void pathChanged(const QString &path);

private slots:
    void onGoUp();
    void onCreateFolder();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onPathCommitted();
    void onShowContextMenu(const QPoint &pos);

private:
    void populate();

    bool         m_selectable;
    bool         m_allowTrackAssignment;
    bool         m_allowCreateFolder;
    QString      m_currentPath;

    QLabel      *m_titleLabel;
    QLineEdit   *m_pathEdit;
    QPushButton *m_upBtn;
    QPushButton *m_newFolderBtn; // nullptr si !allowCreateFolder
    QListWidget *m_list;
    QPushButton *m_selAllBtn;   // nullptr si !selectable
    QPushButton *m_selNoneBtn;  // nullptr si !selectable
};

