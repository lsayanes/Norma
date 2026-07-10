#include "FtpClient.h"

#include <QFile>
#include <QRegularExpression>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
static constexpr auto kSkipEmpty = Qt::SkipEmptyParts;
#else
static constexpr auto kSkipEmpty = QString::SkipEmptyParts;
#endif

bool FtpClient::isConnected() const
{
    return m_control.state() == QAbstractSocket::ConnectedState;
}

void FtpClient::disconnectFromServer()
{
    close();
    m_control.abort();
    m_buffer.clear();
}

bool FtpClient::connectToServer(const QString &host, quint16 port,
                                const QString &user, const QString &password,
                                QString *error)
{
    disconnectFromServer();

    m_control.connectToHost(host, port);
    if (!m_control.waitForConnected(kTimeoutMs)) {
        setError(error, QString("Cannot connect to %1:%2 (%3)")
                 .arg(host).arg(port).arg(m_control.errorString()));
        return false;
    }

    QString response;
    if (!expect({220}, &response, error))
        return false;

    const QString ftpUser = user.trimmed().isEmpty() ? "anonymous" : user.trimmed();
    if (!command("USER " + ftpUser, {230, 331}, &response, error))
        return false;

    if (response.startsWith("331")) {
        const QString ftpPassword = password.isEmpty() ? "anonymous@" : password;
        if (!command("PASS " + ftpPassword, {230, 202}, &response, error))
            return false;
    }

    return command("TYPE I", {200}, &response, error);
}

bool FtpClient::cwd(const QString &remotePath, QString *error)
{
    const QString cleanPath = remotePath.trimmed().isEmpty() ? QString("/") : remotePath.trimmed();
    QString response;
    return command("CWD " + cleanPath, {250}, &response, error);
}

bool FtpClient::cdUp(QString *error)
{
    QString response;
    return command("CDUP", {200, 250}, &response, error);
}

bool FtpClient::pwd(QString *path, QString *error)
{
    QString response;
    if (!command("PWD", {257}, &response, error))
        return false;

    const auto match = QRegularExpression("\"([^\"]+)\"").match(response);
    if (!match.hasMatch()) {
        setError(error, "Cannot parse PWD response: " + response);
        return false;
    }

    if (path)
        *path = match.captured(1);
    return true;
}

bool FtpClient::listDirectory(QList<FtpDirEntry> *entries, QString *error)
{
    QTcpSocket dataSocket;
    if (!openDataConnection(&dataSocket, error))
        return false;

    QString response;
    if (!command("LIST", {125, 150}, &response, error))
        return false;

    QByteArray data;
    if (!readDataConnection(&dataSocket, &data, error))
        return false;

    if (!expect({226, 250}, &response, error))
        return false;

    if (entries)
        *entries = parseListOutput(data);
    return true;
}

bool FtpClient::uploadFile(const QString &localPath, const QString &remoteName, QString *error,
                           ProgressCallback progress)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QString("Cannot read %1").arg(localPath));
        return false;
    }

    const qint64 totalBytes = file.size();
    qint64 sentBytes = 0;

    QTcpSocket dataSocket;
    if (!openDataConnection(&dataSocket, error))
        return false;

    QString response;
    if (!command("STOR " + remoteName, {125, 150}, &response, error))
        return false;

    if (progress)
        progress(0, totalBytes);

    constexpr qint64 kBufSize = 64 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kBufSize);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            setError(error, QString("Read error in %1").arg(localPath));
            return false;
        }

        qint64 written = 0;
        while (written < chunk.size()) {
            const qint64 n = dataSocket.write(chunk.constData() + written, chunk.size() - written);
            if (n < 0 || !dataSocket.waitForBytesWritten(kTimeoutMs)) {
                setError(error, QString("FTP data write failed (%1)").arg(dataSocket.errorString()));
                return false;
            }
            written += n;
            sentBytes += n;
            if (progress)
                progress(sentBytes, totalBytes);
        }
    }

    if (progress)
        progress(totalBytes, totalBytes);

    dataSocket.disconnectFromHost();
    if (dataSocket.state() != QAbstractSocket::UnconnectedState)
        dataSocket.waitForDisconnected(kTimeoutMs);

    return expect({226, 250}, &response, error);
}

void FtpClient::close()
{
    QString response;
    if (m_control.state() == QAbstractSocket::ConnectedState)
        command("QUIT", {221}, &response, nullptr);
}

bool FtpClient::command(const QString &commandText, const QList<int> &expectedCodes,
                        QString *response, QString *error)
{
    const QByteArray bytes = commandText.toUtf8() + "\r\n";
    if (m_control.write(bytes) != bytes.size() || !m_control.waitForBytesWritten(kTimeoutMs)) {
        setError(error, QString("FTP command failed: %1").arg(commandText));
        return false;
    }

    return expect(expectedCodes, response, error);
}

bool FtpClient::expect(const QList<int> &expectedCodes, QString *response, QString *error)
{
    int code = 0;
    if (!readResponse(&code, response, error))
        return false;

    if (!expectedCodes.contains(code)) {
        setError(error, QString("Unexpected FTP response: %1")
                 .arg(response ? *response : QString::number(code)));
        return false;
    }

    return true;
}

bool FtpClient::readResponse(int *code, QString *response, QString *error)
{
    QStringList lines;
    int firstCode = 0;
    bool multiline = false;

    while (true) {
        const int newlinePos = m_buffer.indexOf('\n');
        if (newlinePos < 0) {
            if (!m_control.waitForReadyRead(kTimeoutMs)) {
                setError(error, QString("FTP response timeout (%1)").arg(m_control.errorString()));
                return false;
            }
            m_buffer += QString::fromUtf8(m_control.readAll());
            continue;
        }

        QString line = m_buffer.left(newlinePos);
        m_buffer.remove(0, newlinePos + 1);
        line.remove('\r');
        lines << line;

        const auto match = QRegularExpression("^(\\d{3})([ -]).*").match(line);
        if (!match.hasMatch()) {
            if (firstCode == 0)
                continue;
        } else if (firstCode == 0) {
            firstCode = match.captured(1).toInt();
            multiline = (match.captured(2) == "-");
            if (!multiline)
                break;
        } else if (multiline && line.startsWith(QString::number(firstCode) + " ")) {
            break;
        }
    }

    if (code)
        *code = firstCode;
    if (response)
        *response = lines.join('\n');
    return true;
}

bool FtpClient::enterPassiveMode(QHostAddress *host, quint16 *port, QString *error)
{
    QString response;
    if (!command("PASV", {227}, &response, error))
        return false;

    const auto match = QRegularExpression("\\((\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)\\)").match(response);
    if (!match.hasMatch()) {
        setError(error, "Cannot parse FTP passive response: " + response);
        return false;
    }

    const QString hostText = QString("%1.%2.%3.%4")
        .arg(match.captured(1), match.captured(2), match.captured(3), match.captured(4));
    const int p1 = match.captured(5).toInt();
    const int p2 = match.captured(6).toInt();

    *host = QHostAddress(hostText);
    if (host->isNull() || hostText == "0.0.0.0")
        *host = m_control.peerAddress();
    *port = static_cast<quint16>((p1 << 8) + p2);
    return true;
}

bool FtpClient::openDataConnection(QTcpSocket *dataSocket, QString *error)
{
    QHostAddress dataHost;
    quint16 dataPort = 0;
    if (!enterPassiveMode(&dataHost, &dataPort, error))
        return false;

    dataSocket->connectToHost(dataHost, dataPort);
    if (!dataSocket->waitForConnected(kTimeoutMs)) {
        setError(error, QString("Cannot open FTP data connection (%1)").arg(dataSocket->errorString()));
        return false;
    }
    return true;
}

bool FtpClient::readDataConnection(QTcpSocket *dataSocket, QByteArray *data, QString *error)
{
    QByteArray buffer;
    while (dataSocket->state() == QAbstractSocket::ConnectedState
           || dataSocket->bytesAvailable() > 0)
    {
        if (dataSocket->bytesAvailable() == 0
            && !dataSocket->waitForReadyRead(kTimeoutMs))
        {
            if (dataSocket->state() != QAbstractSocket::ConnectedState)
                break;
            setError(error, QString("FTP data read timeout (%1)").arg(dataSocket->errorString()));
            return false;
        }
        buffer += dataSocket->readAll();
    }

    if (dataSocket->state() != QAbstractSocket::UnconnectedState)
        dataSocket->waitForDisconnected(kTimeoutMs);

    if (data)
        *data = buffer;
    return true;
}

void FtpClient::setError(QString *error, const QString &message) const
{
    if (error)
        *error = message;
}

QList<FtpDirEntry> FtpClient::parseListOutput(const QByteArray &data)
{
    QList<FtpDirEntry> entries;
    const QStringList lines = QString::fromUtf8(data).split(QRegularExpression("[\r\n]+"), kSkipEmpty);

    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith("total ", Qt::CaseInsensitive))
            continue;

        FtpDirEntry entry;

        // Unix: drwxr-xr-x ... name
        if (line.size() > 10 && (line[0] == 'd' || line[0] == '-' || line[0] == 'l'))
        {
            entry.isDirectory = line.startsWith('d') || line.startsWith('l');
            const int namePos = line.indexOf(QRegularExpression("\\d{2}:\\d{2}|\\d{4}"));
            if (namePos >= 0) {
                const int space = line.indexOf(' ', namePos);
                entry.name = (space >= 0) ? line.mid(space + 1).trimmed() : QString();
            }
            if (entry.name.isEmpty()) {
                const QStringList parts = line.split(QRegularExpression("\\s+"), kSkipEmpty);
                if (parts.size() >= 9)
                    entry.name = parts.mid(8).join(' ');
            }
        }
        // DOS: 01-01-80 12:00AM <DIR> name
        else if (line.contains("<DIR>", Qt::CaseInsensitive))
        {
            entry.isDirectory = true;
            const int dirPos = line.indexOf("<DIR>", 0, Qt::CaseInsensitive);
            entry.name = line.mid(dirPos + 5).trimmed();
        }
        else
        {
            // Fallback: last token is the name; treat extension-less as directory
            const QStringList parts = line.split(QRegularExpression("\\s+"), kSkipEmpty);
            if (parts.isEmpty())
                continue;
            entry.name = parts.last();
            entry.isDirectory = !entry.name.contains('.');
        }

        if (entry.name.isEmpty() || entry.name == "." || entry.name == "..")
            continue;

        entries.append(entry);
    }

    return entries;
}
