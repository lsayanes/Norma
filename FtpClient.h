#pragma once

#include <QHostAddress>
#include <QString>
#include <QList>
#include <QTcpSocket>
#include <functional>

struct FtpDirEntry
{
    QString name;
    bool isDirectory = false;
};

class FtpClient
{
public:
    using ProgressCallback = std::function<void(qint64 bytesSent, qint64 bytesTotal)>;

    bool isConnected() const;
    void disconnectFromServer();

    bool connectToServer(const QString &host, quint16 port,
                         const QString &user, const QString &password,
                         QString *error);

    bool cwd(const QString &remotePath, QString *error);
    bool cdUp(QString *error);
    bool pwd(QString *path, QString *error);
    bool listDirectory(QList<FtpDirEntry> *entries, QString *error);
    bool uploadFile(const QString &localPath, const QString &remoteName, QString *error,
                    ProgressCallback progress = {});

private:
    static constexpr int kTimeoutMs = 15000;

    QTcpSocket m_control;
    QString m_buffer;

    void close();
    bool command(const QString &commandText, const QList<int> &expectedCodes,
                 QString *response, QString *error);
    bool expect(const QList<int> &expectedCodes, QString *response, QString *error);
    bool readResponse(int *code, QString *response, QString *error);
    bool enterPassiveMode(QHostAddress *host, quint16 *port, QString *error);
    bool openDataConnection(QTcpSocket *dataSocket, QString *error);
    bool readDataConnection(QTcpSocket *dataSocket, QByteArray *data, QString *error);
    void setError(QString *error, const QString &message) const;
    static QList<FtpDirEntry> parseListOutput(const QByteArray &data);
};
