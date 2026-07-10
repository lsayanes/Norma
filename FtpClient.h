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
    // La HD24 graba lento en disco mecánico: el 226 puede tardar mucho tras cerrar el data socket.
    static constexpr int kTransferTimeoutMs = 10 * 60 * 1000;

    QTcpSocket m_control;
    QString m_buffer;

    void close();
    bool command(const QString &commandText, const QList<int> &expectedCodes,
                 QString *response, QString *error, int timeoutMs = kTimeoutMs);
    bool expect(const QList<int> &expectedCodes, QString *response, QString *error,
                int timeoutMs = kTimeoutMs);
    bool readResponse(int *code, QString *response, QString *error, int timeoutMs = kTimeoutMs);
    bool enterPassiveMode(QHostAddress *host, quint16 *port, QString *error);
    bool openDataConnection(QTcpSocket *dataSocket, QString *error);
    bool readDataConnection(QTcpSocket *dataSocket, QByteArray *data, QString *error);
    bool waitUntilBytesToWriteAtMost(QTcpSocket *dataSocket, qint64 maxBuffered,
                                     int timeoutMs, QString *error);
    void setError(QString *error, const QString &message) const;
    static QList<FtpDirEntry> parseListOutput(const QByteArray &data);
};
