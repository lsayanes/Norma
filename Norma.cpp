#include "Norma.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QFrame>
#include <QApplication>


Norma::Norma(QWidget *parent)
    : QMainWindow(parent)
    , m_sourcePanel(nullptr)
    , m_destPanel(nullptr)
    , m_transformBtn(nullptr)
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
    m_destPanel   = new FilePanel("Destination", /*selectable=*/false, m_splitter);

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

    m_transformBtn = new QPushButton("▶  Normalize for HD24", central);
    m_transformBtn->setFixedHeight(36);
    mainLayout->addWidget(m_transformBtn);

    connect(m_transformBtn, &QPushButton::clicked, this, &Norma::applyTransformation);

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
