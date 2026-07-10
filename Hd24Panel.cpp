#include "Hd24Panel.h"
#include "FtpClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>

Hd24Panel::Hd24Panel(QWidget *parent)
    : QWidget(parent)
    , m_client(nullptr)
    , m_connected(false)
    , m_currentPath("/")
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    m_titleLabel = new QLabel("HD24", this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont f = m_titleLabel->font();
    f.setBold(true);
    m_titleLabel->setFont(f);
    root->addWidget(m_titleLabel);

    auto *pathBar = new QHBoxLayout();
    m_upBtn = new QPushButton("↑", this);
    m_upBtn->setFixedWidth(28);
    m_upBtn->setToolTip("Go up");

    m_pathEdit = new QLineEdit(m_currentPath, this);
    m_pathEdit->setReadOnly(false);

    m_refreshBtn = new QPushButton("↻", this);
    m_refreshBtn->setFixedWidth(28);
    m_refreshBtn->setToolTip("Refresh");

    pathBar->addWidget(m_upBtn);
    pathBar->addWidget(m_pathEdit);
    pathBar->addWidget(m_refreshBtn);
    root->addLayout(pathBar);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_list, 1);

    connect(m_upBtn, &QPushButton::clicked, this, &Hd24Panel::onGoUp);
    connect(m_refreshBtn, &QPushButton::clicked, this, &Hd24Panel::onRefresh);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &Hd24Panel::onPathCommitted);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &Hd24Panel::onItemDoubleClicked);

    clearListing("Connect to browse HD24 disks and songs");
    setEnabled(false);
}

void Hd24Panel::setClient(FtpClient *client)
{
    m_client = client;
}

void Hd24Panel::setConnected(bool connected)
{
    m_connected = connected;
    setEnabled(connected);
    if (!connected)
    {
        m_currentPath = "/";
        m_pathEdit->setText(m_currentPath);
        clearListing("Connect to browse HD24 disks and songs");
    }
}

QString Hd24Panel::currentPath() const
{
    return m_currentPath;
}

bool Hd24Panel::refresh(QString *error)
{
    if (!m_connected || !m_client || !m_client->isConnected())
    {
        if (error)
            *error = "Not connected";
        return false;
    }

    QList<FtpDirEntry> entries;
    if (!m_client->listDirectory(&entries, error))
        return false;

    QString pwdPath;
    if (m_client->pwd(&pwdPath, nullptr) && !pwdPath.isEmpty())
        m_currentPath = pwdPath;

    m_pathEdit->setText(m_currentPath);
    populate(entries);
    emit pathChanged(m_currentPath);
    return true;
}

bool Hd24Panel::navigateTo(const QString &path, QString *error)
{
    if (!m_connected || !m_client || !m_client->isConnected())
    {
        if (error)
            *error = "Not connected";
        return false;
    }

    const QString target = path.trimmed().isEmpty() ? QString("/") : path.trimmed();
    if (!m_client->cwd(target, error))
        return false;

    m_currentPath = target;
    return refresh(error);
}

void Hd24Panel::onGoUp()
{
    if (!m_connected || !m_client)
        return;

    QString error;
    if (!m_client->cdUp(&error))
    {
        // Fallback: try parent path via CWD
        const QString parent = parentRemotePath(m_currentPath);
        if (!m_client->cwd(parent, &error))
        {
            emit statusMessage("Cannot go up: " + error);
            return;
        }
    }

    if (!refresh(&error))
        emit statusMessage("Refresh failed: " + error);
}

void Hd24Panel::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item || !m_connected || !m_client)
        return;

    const bool isDir = item->data(Qt::UserRole + 1).toBool();
    if (!isDir)
        return;

    const QString name = item->data(Qt::UserRole).toString();
    QString error;
    if (!m_client->cwd(name, &error))
    {
        emit statusMessage("Cannot open " + name + ": " + error);
        return;
    }

    m_currentPath = joinRemotePath(m_currentPath, name);
    if (!refresh(&error))
        emit statusMessage("Refresh failed: " + error);
}

void Hd24Panel::onPathCommitted()
{
    QString error;
    if (!navigateTo(m_pathEdit->text().trimmed(), &error))
        emit statusMessage("Cannot open path: " + error);
}

void Hd24Panel::onRefresh()
{
    QString error;
    if (!refresh(&error))
        emit statusMessage("Refresh failed: " + error);
}

void Hd24Panel::populate(const QList<FtpDirEntry> &entries)
{
    m_list->clear();

    QList<FtpDirEntry> dirs;
    QList<FtpDirEntry> files;
    for (const FtpDirEntry &e : entries)
    {
        if (e.isDirectory)
            dirs.append(e);
        else
            files.append(e);
    }

    auto addEntry = [&](const FtpDirEntry &e) 
    {
        const QString label = e.isDirectory ? ("[" + e.name + "]") : e.name;
        auto *item = new QListWidgetItem(label);

        item->setData(Qt::UserRole, e.name);
        item->setData(Qt::UserRole + 1, e.isDirectory);

        if (e.isDirectory)
            item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        else
            item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));

        m_list->addItem(item);
    };

    for (const FtpDirEntry &e : dirs)
        addEntry(e);
    for (const FtpDirEntry &e : files)
        addEntry(e);

    if (m_list->count() == 0)
        clearListing("(empty)");
}

void Hd24Panel::clearListing(const QString &placeholder)
{
    m_list->clear();
    auto *item = new QListWidgetItem(placeholder);
    item->setFlags(Qt::NoItemFlags);
    m_list->addItem(item);
}

QString Hd24Panel::joinRemotePath(const QString &base, const QString &name) const
{
    if (base.isEmpty() || base == "/")
        return "/" + name;
    if (base.endsWith('/'))
        return base + name;
    return base + "/" + name;
}

QString Hd24Panel::parentRemotePath(const QString &path) const
{
    QString clean = path;
    while (clean.endsWith('/') && clean.size() > 1)
        clean.chop(1);

    const int slash = clean.lastIndexOf('/');
    if (slash < 0)
        return "/";
    if (slash == 0)
        return "/";
    return clean.left(slash);
}
