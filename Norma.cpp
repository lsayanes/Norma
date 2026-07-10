#include "Norma.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QHostAddress>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTcpSocket>
#include <QSplitter>
#include <QFrame>
#include <QApplication>
#include <QRegularExpression>

namespace
{
class FtpClient
{
public:
    bool connectToServer(const QString &host, quint16 port, const QString &user, const QString &password, QString *error)
    {
        m_control.connectToHost(host, port);
        if (!m_control.waitForConnected(kTimeoutMs)) {
            setError(error, QString("Cannot connect to %1:%2 (%3)").arg(host).arg(port).arg(m_control.errorString()));
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

    bool cwd(const QString &remotePath, QString *error)
    {
        const QString cleanPath = remotePath.trimmed();
        if (cleanPath.isEmpty() || cleanPath == "/")
            return true;

        QString response;
        return command("CWD " + cleanPath, {250}, &response, error);
    }

    bool uploadFile(const QString &localPath, const QString &remoteName, QString *error)
    {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) {
            setError(error, QString("Cannot read %1").arg(localPath));
            return false;
        }

        QHostAddress dataHost;
        quint16 dataPort = 0;
        if (!enterPassiveMode(&dataHost, &dataPort, error))
            return false;

        QTcpSocket dataSocket;
        dataSocket.connectToHost(dataHost, dataPort);
        if (!dataSocket.waitForConnected(kTimeoutMs)) {
            setError(error, QString("Cannot open FTP data connection (%1)").arg(dataSocket.errorString()));
            return false;
        }

        QString response;
        if (!command("STOR " + remoteName, {125, 150}, &response, error))
            return false;

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
            }
        }

        dataSocket.disconnectFromHost();
        if (dataSocket.state() != QAbstractSocket::UnconnectedState)
            dataSocket.waitForDisconnected(kTimeoutMs);

        return expect({226, 250}, &response, error);
    }

    void close()
    {
        QString response;
        if (m_control.state() == QAbstractSocket::ConnectedState)
            command("QUIT", {221}, &response, nullptr);
    }

private:
    static constexpr int kTimeoutMs = 15000;

    QTcpSocket m_control;
    QString m_buffer;

    bool command(const QString &commandText, const QList<int> &expectedCodes, QString *response, QString *error)
    {
        const QByteArray bytes = commandText.toUtf8() + "\r\n";
        if (m_control.write(bytes) != bytes.size() || !m_control.waitForBytesWritten(kTimeoutMs)) {
            setError(error, QString("FTP command failed: %1").arg(commandText));
            return false;
        }

        return expect(expectedCodes, response, error);
    }

    bool expect(const QList<int> &expectedCodes, QString *response, QString *error)
    {
        int code = 0;
        if (!readResponse(&code, response, error))
            return false;

        if (!expectedCodes.contains(code)) {
            setError(error, QString("Unexpected FTP response: %1").arg(response ? *response : QString::number(code)));
            return false;
        }

        return true;
    }

    bool readResponse(int *code, QString *response, QString *error)
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

    bool enterPassiveMode(QHostAddress *host, quint16 *port, QString *error)
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

    void setError(QString *error, const QString &message) const
    {
        if (error)
            *error = message;
    }
};
}


Norma::Norma(QWidget *parent)
    : QMainWindow(parent)
    , m_sourcePanel(nullptr)
    , m_destPanel(nullptr)
    , m_transformBtn(nullptr)
    , m_uploadBtn(nullptr)
    , m_ftpHostEdit(nullptr)
    , m_ftpUserEdit(nullptr)
    , m_ftpPasswordEdit(nullptr)
    , m_ftpRemotePathEdit(nullptr)
    , m_splitter(nullptr)
    , m_log(nullptr)
{
}

Norma::~Norma() {}

bool Norma::create(const std::string &title)
{
    setWindowTitle(title.c_str());
    resize(1024, 640);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    m_sourcePanel = new FilePanel("Source", /*selectable=*/true,  m_splitter);
    m_destPanel   = new FilePanel("Destination", /*selectable=*/true, m_splitter,
                                  /*allowTrackAssignment=*/false, /*allowCreateFolder=*/true);

    m_splitter->addWidget(m_sourcePanel);
    m_splitter->addWidget(m_destPanel);
    m_splitter->setSizes({500, 500});
    mainLayout->addWidget(m_splitter, 1);

    // ── output log ──
    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    m_log->setFixedHeight(120);
    m_log->setFont(QFont("Courier", 10));
    m_log->setPlaceholderText("Output...");
    mainLayout->addWidget(m_log);

    auto *ftpLayout = new QGridLayout();
    ftpLayout->setHorizontalSpacing(6);
    ftpLayout->setVerticalSpacing(4);

    m_ftpHostEdit = new QLineEdit(central);
    m_ftpHostEdit->setPlaceholderText("HD24 IP address");

    m_ftpUserEdit = new QLineEdit("anonymous", central);
    m_ftpPasswordEdit = new QLineEdit("anonymous@", central);
    m_ftpPasswordEdit->setEchoMode(QLineEdit::Password);

    m_ftpRemotePathEdit = new QLineEdit("/", central);
    m_ftpRemotePathEdit->setPlaceholderText("Remote folder");

    ftpLayout->addWidget(new QLabel("FTP host", central), 0, 0);
    ftpLayout->addWidget(m_ftpHostEdit, 0, 1);
    ftpLayout->addWidget(new QLabel("User", central), 0, 2);
    ftpLayout->addWidget(m_ftpUserEdit, 0, 3);
    ftpLayout->addWidget(new QLabel("Password", central), 1, 0);
    ftpLayout->addWidget(m_ftpPasswordEdit, 1, 1);
    ftpLayout->addWidget(new QLabel("Remote path", central), 1, 2);
    ftpLayout->addWidget(m_ftpRemotePathEdit, 1, 3);
    mainLayout->addLayout(ftpLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_transformBtn = new QPushButton("▶  Normalize for HD24", central);
    m_transformBtn->setFixedHeight(36);
    m_uploadBtn = new QPushButton("⇧  Upload selected to HD24", central);
    m_uploadBtn->setFixedHeight(36);
    buttonLayout->addWidget(m_transformBtn);
    buttonLayout->addWidget(m_uploadBtn);
    mainLayout->addLayout(buttonLayout);

    connect(m_transformBtn, &QPushButton::clicked, this, &Norma::applyTransformation);
    connect(m_uploadBtn, &QPushButton::clicked, this, &Norma::uploadSelectedToHd24);

    return true;
}

int Norma::deleteJunk(const QString &pathIn, const QString &pathOut)
{
    QFile in(pathIn);
    if (!in.open(QIODevice::ReadOnly))
        return 2;

    QFile out(pathOut);
    if (!out.open(QIODevice::WriteOnly))
        return 1;

    // Copiar cabecera RIFF + WAVE (12 bytes)
    const QByteArray header = in.read(12);
    if (header.size() != 12)
        return 3;
    out.write(header);

    constexpr qint64 kBufSize = 4096;

    // Copiar todos los chunks excepto "JUNK"
    while (true)
    {
        const QByteArray chunkId = in.read(4);
        if (chunkId.size() < 4)
            break;

        const QByteArray sizeBytes = in.read(4);
        if (sizeBytes.size() < 4)
            break;

        // WAV es little-endian
        quint32 chunkSize = 0;
        memcpy(&chunkSize, sizeBytes.constData(), sizeof(chunkSize));

        // Alinear a 2 bytes si es impar
        const quint32 chunkSizePadded = chunkSize + (chunkSize % 2);

        if (chunkId == "JUNK")
        {
            in.skip(chunkSizePadded);
            continue;
        }

        out.write(chunkId);
        out.write(sizeBytes);

        quint32 remaining = chunkSizePadded;
        while (remaining > 0)
        {
            const QByteArray data = in.read(qMin<qint64>(remaining, kBufSize));
            if (data.isEmpty())
                break;   // EOF o error: evita loop infinito
            out.write(data);
            remaining -= static_cast<quint32>(data.size());
        }
    }

    return 0;  // QFile se cierra solo (RAII)
}


void Norma::output(const QString &msg)
{
    m_log->appendPlainText(msg);
    QApplication::processEvents();   
}

void Norma::applyTransformation()
{
    auto pairs = m_sourcePanel->selectedFilesWithNames();
    if (pairs.isEmpty()) {
        QMessageBox::information(this, "No selection",
            "Select at least one .wav file and assign it a track name (right-click).");
        return;
    }

    QString dest = m_destPanel->currentPath();

    m_log->clear();
    output("=== Normalize for HD24 ===");
    output(QString("Destination: %1").arg(dest));
    output(QString("Files: %1").arg(pairs.size()));
    output("─────────────────────────────────────");

    int nOk = 0;
    int nFail = 0;

    for (const auto &pair : pairs)
    {
        const QString &srcPath  = pair.first;
        const QString &destName = pair.second;
        const QString  destPath = dest + "/" + destName + ".wav";
        const QString  srcName  = QFileInfo(srcPath).fileName();

        output(QString("[>] %1  →  %2").arg(srcName, destName + ".wav"));

        if (QFile::exists(destPath))
        {
            QFile::remove(destPath);
            output("    existing file removed");
        }

        const int result = deleteJunk(srcPath, destPath);
        if (result == 0)
        {
            const qint64 sizeKB = QFileInfo(destPath).size() / 1024;
            output(QString("    OK  (%1 KB)").arg(sizeKB));
            ++nOk;
        }
        else
        {
            output(QString("    FAILED (error %1)").arg(result));
            ++nFail;
        }
    }

    output("─────────────────────────────────────");
    output(QString("Done: %1 OK, %2 failed.").arg(nOk).arg(nFail));

    m_destPanel->setPath(dest);
}

void Norma::uploadSelectedToHd24()
{
    const QStringList files = m_destPanel->selectedFiles();
    if (files.isEmpty()) {
        QMessageBox::information(this, "No selection",
            "Select at least one normalized .wav file in the destination panel.");
        return;
    }

    const QString host = m_ftpHostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::information(this, "Missing FTP host",
            "Enter the HD24 IP address or FTP host.");
        return;
    }

    m_log->clear();
    output("=== Upload selected to HD24 ===");
    output(QString("FTP host: %1").arg(host));
    output(QString("Remote path: %1").arg(m_ftpRemotePathEdit->text().trimmed().isEmpty()
        ? "/" : m_ftpRemotePathEdit->text().trimmed()));
    output(QString("Files: %1").arg(files.size()));
    output("─────────────────────────────────────");

    setEnabled(false);
    QApplication::processEvents();

    QString error;
    FtpClient ftp;
    int nOk = 0;
    int nFail = 0;

    if (!ftp.connectToServer(host, 21, m_ftpUserEdit->text(), m_ftpPasswordEdit->text(), &error)) {
        output("Connection failed: " + error);
        setEnabled(true);
        return;
    }

    if (!ftp.cwd(m_ftpRemotePathEdit->text(), &error)) {
        output("Remote path failed: " + error);
        ftp.close();
        setEnabled(true);
        return;
    }

    for (const QString &filePath : files)
    {
        const QFileInfo fi(filePath);
        output(QString("[↑] %1").arg(fi.fileName()));

        if (ftp.uploadFile(filePath, fi.fileName(), &error)) {
            const qint64 sizeKB = fi.size() / 1024;
            output(QString("    OK  (%1 KB)").arg(sizeKB));
            ++nOk;
        } else {
            output("    FAILED: " + error);
            ++nFail;
        }
    }

    ftp.close();
    setEnabled(true);

    output("─────────────────────────────────────");
    output(QString("Upload done: %1 OK, %2 failed.").arg(nOk).arg(nFail));
}
