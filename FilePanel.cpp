#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileIconProvider>
#include <QMenu>
#include <QAction>

#include "FilePanel.h"

FilePanel::FilePanel(const QString &title, bool selectable, QWidget *parent)
    : QWidget(parent)
    , m_selectable(selectable)
    , m_currentPath(QDir::homePath())
    , m_selAllBtn(nullptr)
    , m_selNoneBtn(nullptr)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont f = m_titleLabel->font();
    f.setBold(true);
    m_titleLabel->setFont(f);
    root->addWidget(m_titleLabel);

    auto *pathBar = new QHBoxLayout();
    m_upBtn   = new QPushButton("↑", this);
    m_upBtn->setFixedWidth(28);
    m_upBtn->setToolTip("Go up");
    m_pathEdit = new QLineEdit(m_currentPath, this);
    pathBar->addWidget(m_upBtn);
    pathBar->addWidget(m_pathEdit);
    root->addLayout(pathBar);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_list, 1);

    if (m_selectable) 
    {
        auto *btnBar = new QHBoxLayout();
        m_selAllBtn  = new QPushButton("Select All",  this);
        m_selNoneBtn = new QPushButton("Select None", this);
        btnBar->addWidget(m_selAllBtn);
        btnBar->addWidget(m_selNoneBtn);
        root->addLayout(btnBar);

        connect(m_selAllBtn,  &QPushButton::clicked, this, &FilePanel::selectAll);
        connect(m_selNoneBtn, &QPushButton::clicked, this, &FilePanel::selectNone);
    }

    connect(m_upBtn,    &QPushButton::clicked,                        this, &FilePanel::onGoUp);
    connect(m_pathEdit, &QLineEdit::returnPressed,                    this, &FilePanel::onPathCommitted);
    connect(m_list,     &QListWidget::itemDoubleClicked,              this, &FilePanel::onItemDoubleClicked);
    connect(m_list,     &QListWidget::customContextMenuRequested,     this, &FilePanel::onShowContextMenu);

    populate();
}


QString FilePanel::currentPath() const
{
    return m_currentPath;
}

void FilePanel::setPath(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) return;
    m_currentPath = dir.absolutePath();
    m_pathEdit->setText(m_currentPath);
    populate();
    emit pathChanged(m_currentPath);
}

QStringList FilePanel::selectedFiles() const
{
    QStringList result;
    for (int i = 0; i < m_list->count(); ++i) 
    {
        QListWidgetItem *item = m_list->item(i);
        if (item->checkState() == Qt::Checked) 
        {
            QFileInfo fi(m_currentPath + "/" + item->data(Qt::UserRole).toString());
            if (fi.isFile())
                result << fi.absoluteFilePath();
        }
    }
    return result;
}

void FilePanel::selectAll()
{
    for (int i = 0; i < m_list->count(); ++i) 
    {
        QListWidgetItem *item = m_list->item(i);
        if (item->flags() & Qt::ItemIsUserCheckable)
            item->setCheckState(Qt::Checked);
    }
}

void FilePanel::selectNone()
{
    for (int i = 0; i < m_list->count(); ++i) 
    {
        QListWidgetItem *item = m_list->item(i);
        if (item->flags() & Qt::ItemIsUserCheckable)
            item->setCheckState(Qt::Unchecked);
    }
}


QList<QPair<QString,QString>> FilePanel::selectedFilesWithNames() const
{
    QList<QPair<QString,QString>> result;
    for (int i = 0; i < m_list->count(); ++i)
    {
        QListWidgetItem *item = m_list->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        QFileInfo fi(m_currentPath + "/" + item->data(Qt::UserRole).toString());
        if (!fi.isFile())
            continue;
        QString trackName = item->data(Qt::UserRole + 1).toString();
        if (trackName.isEmpty())
            trackName = fi.baseName(); // fallback: nombre original sin extensión
        result << QPair<QString,QString>(fi.absoluteFilePath(), trackName);
    }
    return result;
}

void FilePanel::onShowContextMenu(const QPoint &pos)
{
    // Solo en el panel de origen (selectable)
    if (!m_selectable) return;

    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item) return;

    // Solo archivos, no directorios
    QFileInfo fi(m_currentPath + "/" + item->data(Qt::UserRole).toString());
    if (!fi.isFile()) return;

    QMenu menu(this);
    menu.setTitle("Assign track name");

    for (int t = 1; t <= 24; ++t)
    {
        QString trackName = QString("Track%1").arg(t, 2, 10, QChar('0'));
        QAction *action = menu.addAction(trackName);
        connect(action, &QAction::triggered, this, [this, item, trackName]() {
            item->setData(Qt::UserRole + 1, trackName);
            // Mostrar en la lista: "filename.wav → Track05"
            QString fileName = item->data(Qt::UserRole).toString();
            item->setText(fileName + "  →  " + trackName);
            // Marcar el checkbox automáticamente
            if (item->flags() & Qt::ItemIsUserCheckable)
                item->setCheckState(Qt::Checked);
        });
    }

    // Opción para limpiar la asignación
    menu.addSeparator();
    QAction *clearAction = menu.addAction("Clear assignment");
    connect(clearAction, &QAction::triggered, this, [this, item]() {
        item->setData(Qt::UserRole + 1, QString());
        item->setText(item->data(Qt::UserRole).toString());
    });

    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void FilePanel::onGoUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp())
        setPath(dir.absolutePath());
}

void FilePanel::onItemDoubleClicked(QListWidgetItem *item)
{
    QString name = item->data(Qt::UserRole).toString();
    QFileInfo fi(m_currentPath + "/" + name);
    if (fi.isDir())
        setPath(fi.absoluteFilePath());
}

void FilePanel::onPathCommitted()
{
    setPath(m_pathEdit->text().trimmed());
}


void FilePanel::populate()
{
    m_list->clear();

    QFileIconProvider iconProvider;
    QDir dir(m_currentPath);

    // Directorios primero (sin ocultos)
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : dir.entryInfoList())
    {
        auto *item = new QListWidgetItem(iconProvider.icon(fi), "[" + fi.fileName() + "]");
        item->setData(Qt::UserRole, fi.fileName());
        m_list->addItem(item);
    }

    // Solo archivos .wav (sin ocultos)
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setNameFilters({"*.wav", "*.WAV"});
    dir.setSorting(QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : dir.entryInfoList())
    {
        auto *item = new QListWidgetItem(iconProvider.icon(fi), fi.fileName());
        item->setData(Qt::UserRole, fi.fileName());

        if (m_selectable)
        {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }

        m_list->addItem(item);
    }
}
