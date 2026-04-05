#pragma once

#include <QMainWindow>
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <string>
#include "FilePanel.h"


class Norma : public QMainWindow
{
    Q_OBJECT

public:
    explicit Norma(QWidget *parent = nullptr);
    ~Norma();

    bool create(const std::string &title);
    int  deleteJunk(const QString &pathIn, const QString &pathOut);

private slots:
    void applyTransformation();

private:
    FilePanel       *m_sourcePanel;
    FilePanel       *m_destPanel;
    QPushButton     *m_transformBtn;
    QSplitter       *m_splitter;
};
