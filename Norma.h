#pragma once

#include <QMainWindow>
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QString>
#include <QStringList>
#include <string>
#include "FilePanel.h"

class FtpClient;


class Norma : public QMainWindow
{
    Q_OBJECT

public:
    explicit Norma(QWidget *parent = nullptr);
    ~Norma();

    bool create(const std::string &title);
    int  deleteJunk(const QString &pathIn, const QString &pathOut);

private:
    void output(const QString &msg);
    QString ftpConfigPath() const;
    void loadFtpSettings();
    void saveFtpSettings();
    void setFtpConnected(bool connected);
    void disconnectFtp();

private slots:
    void applyTransformation();
    void connectToFtp();
    void uploadSelectedToHd24();
    void onFtpCredentialsEdited();

private:
    FilePanel       *m_sourcePanel;
    FilePanel       *m_destPanel;
    QPushButton     *m_transformBtn;
    QPushButton     *m_uploadBtn;
    QPushButton     *m_ftpConnectBtn;
    QLabel          *m_ftpStatusLabel;
    QLineEdit       *m_ftpHostEdit;
    QLineEdit       *m_ftpUserEdit;
    QLineEdit       *m_ftpPasswordEdit;
    QLineEdit       *m_ftpRemotePathEdit;
    QSplitter       *m_splitter;
    QPlainTextEdit  *m_log;
    FtpClient       *m_ftp;
    bool             m_ftpConnected;
};
