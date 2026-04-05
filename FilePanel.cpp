#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QFileIconProvider>

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

    connect(m_upBtn,   &QPushButton::clicked,            this, &FilePanel::onGoUp);
    connect(m_pathEdit, &QLineEdit::returnPressed,        this, &FilePanel::onPathCommitted);
    connect(m_list,    &QListWidget::itemDoubleClicked,   this, &FilePanel::onItemDoubleClicked);

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

    QDir dir(m_currentPath);
    dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QFileIconProvider iconProvider;

    for (const QFileInfo &fi : dir.entryInfoList()) 
    {
        auto *item = new QListWidgetItem(
            iconProvider.icon(fi),
            fi.isDir() ? "[" + fi.fileName() + "]" : fi.fileName()
        );
    
        item->setData(Qt::UserRole, fi.fileName());

        if (m_selectable && fi.isFile()) 
        {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }

        m_list->addItem(item);
    }
}
