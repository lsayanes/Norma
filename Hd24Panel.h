#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QString>
#include "FtpClient.h"

// Panel de navegación remota FTP para la HD24 (discos / songs / tracks).
class Hd24Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Hd24Panel(QWidget *parent = nullptr);

    void setClient(FtpClient *client);
    void setConnected(bool connected);
    bool refresh(QString *error = nullptr);
    bool navigateTo(const QString &path, QString *error = nullptr);

    QString currentPath() const;

signals:
    void pathChanged(const QString &path);
    void statusMessage(const QString &msg);

private slots:
    void onGoUp();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onPathCommitted();
    void onRefresh();

private:
    void populate(const QList<FtpDirEntry> &entries);
    void clearListing(const QString &placeholder);
    QString joinRemotePath(const QString &base, const QString &name) const;
    QString parentRemotePath(const QString &path) const;

    FtpClient   *m_client;
    bool         m_connected;
    QString      m_currentPath;

    QLabel      *m_titleLabel;
    QLineEdit   *m_pathEdit;
    QPushButton *m_upBtn;
    QPushButton *m_refreshBtn;
    QListWidget *m_list;
};
