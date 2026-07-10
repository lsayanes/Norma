#include "Norma.h"
#include "FtpClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <QTextCursor>

Norma::Norma(QWidget *parent)
    : QMainWindow(parent)
    , m_sourcePanel(nullptr)
    , m_destPanel(nullptr)
    , m_hd24Panel(nullptr)
    , m_transformBtn(nullptr)
    , m_uploadBtn(nullptr)
    , m_ftpConnectBtn(nullptr)
    , m_ftpStatusLabel(nullptr)
    , m_ftpHostEdit(nullptr)
    , m_ftpUserEdit(nullptr)
    , m_ftpPasswordEdit(nullptr)
    , m_splitter(nullptr)
    , m_log(nullptr)
    , m_ftp(nullptr)
    , m_ftpConnected(false)
{
}

Norma::~Norma()
{
    disconnectFtp();
    delete m_ftp;
}

bool Norma::create(const std::string &title)
{
    setWindowTitle(title.c_str());
    resize(1280, 640);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    m_sourcePanel = new FilePanel("Source", /*selectable=*/true,  m_splitter);
    m_destPanel   = new FilePanel("Normalized", /*selectable=*/true, m_splitter,
                                  /*allowTrackAssignment=*/false, /*allowCreateFolder=*/true);
    m_hd24Panel   = new Hd24Panel(m_splitter);

    m_splitter->addWidget(m_sourcePanel);
    m_splitter->addWidget(m_destPanel);
    m_splitter->addWidget(m_hd24Panel);
    m_splitter->setSizes({420, 420, 420});
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

    m_ftpConnectBtn = new QPushButton("Connect", central);
    m_ftpStatusLabel = new QLabel("Disconnected", central);

    ftpLayout->addWidget(new QLabel("FTP host", central), 0, 0);
    ftpLayout->addWidget(m_ftpHostEdit, 0, 1);
    ftpLayout->addWidget(new QLabel("User", central), 0, 2);
    ftpLayout->addWidget(m_ftpUserEdit, 0, 3);
    ftpLayout->addWidget(m_ftpConnectBtn, 0, 4);
    ftpLayout->addWidget(new QLabel("Password", central), 1, 0);
    ftpLayout->addWidget(m_ftpPasswordEdit, 1, 1);
    ftpLayout->addWidget(m_ftpStatusLabel, 1, 4);
    mainLayout->addLayout(ftpLayout);

    auto *buttonLayout = new QHBoxLayout();
    m_transformBtn = new QPushButton("▶  Normalize for HD24", central);
    m_transformBtn->setFixedHeight(36);
    m_uploadBtn = new QPushButton("⇧  Upload selected to HD24", central);
    m_uploadBtn->setFixedHeight(36);
    buttonLayout->addWidget(m_transformBtn);
    buttonLayout->addWidget(m_uploadBtn);
    mainLayout->addLayout(buttonLayout);

    m_ftp = new FtpClient();
    m_hd24Panel->setClient(m_ftp);
    loadFtpSettings();
    setFtpConnected(false);

    connect(m_transformBtn, &QPushButton::clicked, this, &Norma::applyTransformation);
    connect(m_ftpConnectBtn, &QPushButton::clicked, this, &Norma::connectToFtp);
    connect(m_uploadBtn, &QPushButton::clicked, this, &Norma::uploadSelectedToHd24);
    connect(m_ftpHostEdit, &QLineEdit::textEdited, this, &Norma::onFtpCredentialsEdited);
    connect(m_ftpUserEdit, &QLineEdit::textEdited, this, &Norma::onFtpCredentialsEdited);
    connect(m_ftpPasswordEdit, &QLineEdit::textEdited, this, &Norma::onFtpCredentialsEdited);
    connect(m_hd24Panel, &Hd24Panel::statusMessage, this, &Norma::onHd24StatusMessage);
    connect(m_hd24Panel, &Hd24Panel::pathChanged, this, [this](const QString &path) {
        m_savedRemotePath = path;
        saveFtpSettings();
    });

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

void Norma::outputProgress(const QString &msg)
{
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    const QString lastLine = cursor.selectedText();

    if (lastLine.startsWith("    ") && lastLine.contains('%'))
        cursor.insertText(msg);
    else
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

QString Norma::ftpConfigPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/ftp.conf";
}

void Norma::loadFtpSettings()
{
    QFile file(ftpConfigPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1);

        if (key == "host")
            m_ftpHostEdit->setText(value);
        else if (key == "user")
            m_ftpUserEdit->setText(value);
        else if (key == "password")
            m_ftpPasswordEdit->setText(value);
        else if (key == "remote_path")
            m_savedRemotePath = value;
    }
}

void Norma::saveFtpSettings()
{
    QFile file(ftpConfigPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        output("Could not save FTP settings: " + file.errorString());
        return;
    }

    const QString remotePath = m_hd24Panel ? m_hd24Panel->currentPath() : m_savedRemotePath;

    QTextStream out(&file);
    out << "host=" << m_ftpHostEdit->text().trimmed() << '\n';
    out << "user=" << m_ftpUserEdit->text() << '\n';
    out << "password=" << m_ftpPasswordEdit->text() << '\n';
    out << "remote_path=" << remotePath << '\n';
}

void Norma::setFtpConnected(bool connected)
{
    m_ftpConnected = connected;
    m_uploadBtn->setEnabled(connected);
    m_ftpConnectBtn->setText(connected ? "Disconnect" : "Connect");
    m_ftpStatusLabel->setText(connected ? "Connected" : "Disconnected");
    m_ftpStatusLabel->setStyleSheet(connected
        ? "color: #1a7f37; font-weight: bold;"
        : "color: #a40e26; font-weight: bold;");
    m_hd24Panel->setConnected(connected);
}

void Norma::disconnectFtp()
{
    if (m_ftp)
        m_ftp->disconnectFromServer();
    setFtpConnected(false);
}

void Norma::onFtpCredentialsEdited()
{
    if (m_ftpConnected)
        disconnectFtp();
}

void Norma::onHd24StatusMessage(const QString &msg)
{
    output(msg);
}

void Norma::connectToFtp()
{
    if (m_ftpConnected)
    {
        saveFtpSettings();
        disconnectFtp();
        output("FTP disconnected.");
        return;
    }

    const QString host = m_ftpHostEdit->text().trimmed();
    if (host.isEmpty())
    {
        QMessageBox::information(this, "Missing FTP host",
            "Enter the HD24 IP address or FTP host.");
        return;
    }

    saveFtpSettings();

    output("=== FTP connect ===");
    output(QString("Connecting to %1 ...").arg(host));

    setEnabled(false);
    QApplication::processEvents();

    QString error;
    const bool ok = m_ftp->connectToServer(host, 21, m_ftpUserEdit->text(), m_ftpPasswordEdit->text(), &error);

    setEnabled(true);

    if (!ok)
    {
        setFtpConnected(false);
        output("Connection failed: " + error);
        return;
    }

    setFtpConnected(true);
    output("Connected.");

    // Restaurar última carpeta remota si existe; si no, listar raíz actual
    if (!m_savedRemotePath.isEmpty() && m_savedRemotePath != "/")
    {
        if (!m_hd24Panel->navigateTo(m_savedRemotePath, &error))
        {
            output("Could not open saved path (" + m_savedRemotePath + "): " + error);
            if (!m_hd24Panel->refresh(&error))
                output("Browse failed: " + error);
        }
        else
        {
            output("Opened: " + m_hd24Panel->currentPath());
        }
    }
    else if (!m_hd24Panel->refresh(&error))
    {
        output("Browse failed: " + error);
    }
    else
    {
        output("Browsing: " + m_hd24Panel->currentPath());
    }
}

void Norma::uploadSelectedToHd24()
{
    if (!m_ftpConnected || !m_ftp || !m_ftp->isConnected())
    {
        setFtpConnected(false);
        QMessageBox::information(this, "Not connected",
            "Connect to the HD24 FTP server first.");
        return;
    }

    const QStringList files = m_destPanel->selectedFiles();
    if (files.isEmpty()) {
        QMessageBox::information(this, "No selection",
            "Select at least one normalized .wav file in the destination panel.");
        return;
    }

    const QString remotePath = m_hd24Panel->currentPath();
    saveFtpSettings();

    const QString host = m_ftpHostEdit->text().trimmed();
    m_log->clear();
    output("=== Upload selected to HD24 ===");
    output(QString("FTP host: %1").arg(host));
    output(QString("Remote path: %1").arg(remotePath));
    output(QString("Files: %1").arg(files.size()));
    output("─────────────────────────────────────");

    setEnabled(false);
    QApplication::processEvents();

    QString error;
    int nOk = 0;
    int nFail = 0;

    // Asegurar que el CWD remoto coincide con el panel HD24
    if (!m_ftp->cwd(remotePath, &error)) {
        output("Remote path failed: " + error);
        disconnectFtp();
        setEnabled(true);
        return;
    }

    for (const QString &filePath : files)
    {
        const QFileInfo fi(filePath);
        output(QString("[↑] %1  →  %2/%3").arg(fi.fileName(), remotePath, fi.fileName()));

        int lastPercent = -1;
        auto progress = [this, &lastPercent](qint64 sent, qint64 total) 
        {
            const int percent = (total > 0)
                ? static_cast<int>((sent * 100) / total)
                : 100;
            if (percent == lastPercent)
                return;
            lastPercent = percent;
            outputProgress(QString("    %1%  (%2 / %3 KB)")
                .arg(percent, 3, 10, QChar(' '))
                .arg(sent / 1024)
                .arg(total / 1024));
        };

        if (m_ftp->uploadFile(filePath, fi.fileName(), &error, progress)) 
        {
            const qint64 sizeKB = fi.size() / 1024;
            output(QString("    OK  (%1 KB)").arg(sizeKB));
            ++nOk;
        } 
        else 
        {
            output("    FAILED: " + error);
            ++nFail;
            if (!m_ftp->isConnected())
            {
                disconnectFtp();
                break;
            }
        }
    }

    setEnabled(true);

    // Refrescar el listado remoto para ver los tracks subidos
    if (m_ftpConnected)
    {
        QString refreshError;
        if (!m_hd24Panel->refresh(&refreshError))
            output("Refresh failed: " + refreshError);
    }

    output("─────────────────────────────────────");
    output(QString("Upload done: %1 OK, %2 failed.").arg(nOk).arg(nFail));
}
