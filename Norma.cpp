#include "Norma.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QMessageBox>
#include <QSplitter>
#include <QFrame>


Norma::Norma(QWidget *parent)
    : QMainWindow(parent)
    , m_sourcePanel(nullptr)
    , m_destPanel(nullptr)
    , m_transformBtn(nullptr)
    , m_splitter(nullptr)
{
}

Norma::~Norma() {}

bool Norma::create(const std::string &title)
{
    setWindowTitle(title.c_str());
    resize(1024, 640);

    // ── central widget ──
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


void Norma::applyTransformation()
{
    auto pairs = m_sourcePanel->selectedFilesWithNames();
    if (pairs.isEmpty()) {
        QMessageBox::information(this, "No selection",
            "Select at least one .wav file and assign it a track name (right-click).");
        return;
    }

    QString dest = m_destPanel->currentPath();

    QStringList errors;
    QStringList done;

    for (const auto &pair : pairs)
    {
        const QString &srcPath  = pair.first;
        const QString &destName = pair.second;     // e.g. "Track05"
        QString destPath = dest + "/" + destName + ".wav";

        // Si ya existe el destino, lo elimino primero
        if (QFile::exists(destPath))
            QFile::remove(destPath);
    
        if (0 == deleteJunk(srcPath, destPath))
            done << destName + ".wav";
        else
            errors << QFileInfo(srcPath).fileName() + " → " + destName + ".wav";
    }

    QString msg;
    if (!done.isEmpty())
        msg += "Copied:\n" + done.join("\n");
    if (!errors.isEmpty())
        msg += "\n\nFailed:\n" + errors.join("\n");

    QMessageBox::information(this, "Result", msg.trimmed());

    m_destPanel->setPath(dest); // refresh destination panel
}
