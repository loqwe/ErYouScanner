#include "WindowAbout.h"

WindowAbout::WindowAbout(QWidget* parent) :
    QDialog(parent)
{
    setFixedSize(400, 200);
    QVBoxLayout* layout = new QVBoxLayout(this);
    setWindowTitle("关于二游扫码");
    QLabel* label = new QLabel("二游扫码 · ErYouScanner\n\n"
                               "版本" MHY_Scanner_VERSION +
                                   QString("\nQt Version: ") + QT_VERSION_STR,
                               this);
    layout->addWidget(label);
    layout->addWidget(label, 0, Qt::AlignCenter);
    setLayout(layout);
}
