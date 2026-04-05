#include "Norma.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
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

    m_transformBtn = new QPushButton("▶  Apply Transformation", central);
    m_transformBtn->setFixedHeight(36);
    mainLayout->addWidget(m_transformBtn);

    connect(m_transformBtn, &QPushButton::clicked, this, &Norma::applyTransformation);

    return true;
}

void Norma::applyTransformation()
{
    QStringList files = m_sourcePanel->selectedFiles();
    if (files.isEmpty()) {
        QMessageBox::information(this, "No selection",
            "Select at least one file in the Source panel.");
        return;
    }

    QString dest = m_destPanel->currentPath();

    /*
        NORMALIZE
    */
    QMessageBox::information(this, "Transform",
        QString("Files to transform: %1\nDestination: %2")
            .arg(files.join("\n"))
            .arg(dest));

    m_destPanel->setPath(dest); // refresh destination
}
