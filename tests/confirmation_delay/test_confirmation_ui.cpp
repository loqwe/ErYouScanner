#include "ui_WindowMain.h"

#include <iostream>
#include <QApplication>
#include <QFontMetrics>
#include <QFontInfo>
#include <QMainWindow>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextDocument>

static bool validateLayout(const Ui::WindowMain& ui)
{
    if (!ui.centralWidget->layout() ||
        ui.spinConfirmDelay->x() != ui.lineEditUname->x() ||
        ui.spinConfirmDelay->x() != ui.lineEditLiveId->x() ||
        ui.spinConfirmDelay->width() != ui.lineEditUname->width() ||
        ui.spinConfirmDelay->height() != ui.comboBox->height() ||
        ui.lineEditLiveId->height() != ui.comboBox->height() ||
        qAbs(ui.pBtstartScreen->width() - ui.pBtStream->width()) > 1 ||
        ui.tableWidget->horizontalScrollBar()->isVisible())
    {
        std::cerr << "grid or table geometry failed\n";
        return false;
    }

    const QList<QWidget*> controls = {
        ui.tableWidget, ui.lineEditUname, ui.labelUname, ui.labelLiveName,
        ui.lineEditLiveId, ui.labelLiveName_2, ui.comboBox, ui.labelConfirmDelay,
        ui.spinConfirmDelay, ui.checkBoxAutoLogin, ui.checkBoxAutoScreen,
        ui.checkBoxAutoExit, ui.pBtstartScreen, ui.pBtStream,
        ui.label_3, ui.label_2, ui.label, ui.label_4, ui.label_5
    };
    for (qsizetype i = 0; i < controls.size(); ++i)
    {
        QWidget* control = controls[i];
        if (!ui.centralWidget->rect().contains(control->geometry()))
        {
            std::cerr << "outside parent: " << control->objectName().toStdString() << "\n";
            return false;
        }
        if (auto* label = qobject_cast<QLabel*>(control))
        {
            QTextDocument text;
            text.setDocumentMargin(0);
            text.setDefaultFont(label->font());
            text.setHtml(label->text());
            if (text.idealWidth() > label->width() + 1)
            {
                std::cerr << "label clipped: " << label->objectName().toStdString()
                          << " text=" << text.idealWidth() << " width=" << label->width() << "\n";
                return false;
            }
        }
        if (auto* button = qobject_cast<QAbstractButton*>(control))
        {
            if (button->fontMetrics().horizontalAdvance(button->text()) + 24 > button->width())
            {
                std::cerr << "button clipped: " << button->objectName().toStdString() << "\n";
                return false;
            }
        }
        for (qsizetype j = i + 1; j < controls.size(); ++j)
            if (control->geometry().intersects(controls[j]->geometry()))
            {
                std::cerr << "overlap: " << control->objectName().toStdString() << " and "
                          << controls[j]->objectName().toStdString() << "\n";
                return false;
            }
    }
    return true;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QApplication::setFont(QFont("Microsoft YaHei", 9));
    QMainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    Ui::WindowMain ui;
    ui.setupUi(&window);
    window.show();
    app.processEvents();
    if (ui.spinConfirmDelay->minimum() != 0 || ui.spinConfirmDelay->maximum() != 60 ||
        ui.spinConfirmDelay->decimals() != 1 || ui.spinConfirmDelay->value() != 0 ||
        !validateLayout(ui))
    {
        std::cerr << "delay UI limits, default or layout failed\n";
        return 1;
    }
    ui.spinConfirmDelay->setValue(1.5);
    if (ui.spinConfirmDelay->value() != 1.5 || ui.spinConfirmDelay->cleanText() != "1.5") return 1;
    // Synthetic account data only. Match the application's initial table sizing.
    const int widths[] = {44, 104, 110, 72, 72};
    for (int col = 0; col < 5; ++col) ui.tableWidget->setColumnWidth(col, widths[col]);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    ui.tableWidget->setRowCount(3);
    const QStringList row = {"1", "100000001", QStringLiteral("\u6d4b\u8bd5\u8d26\u53f7"),
        QStringLiteral("\u5b98\u670d"), QStringLiteral("\u6709\u6548"), QStringLiteral("\u6d4b\u8bd5\u5907\u6ce8")};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 6; ++c) ui.tableWidget->setItem(r, c, new QTableWidgetItem(row[c]));
    ui.tableWidget->selectRow(0);
    ui.label_3->setText("1.16.4");
    ui.lineEditUname->setText(row[2]);
    ui.lineEditLiveId->setEditText("123456");
    ui.checkBoxAutoLogin->setChecked(true);
    app.processEvents();
    if (!window.grab().save(QString("confirmation-ui-preview-%1.png").arg(window.devicePixelRatioF()))) return 1;
    ui.lineEditUname->setText(QStringLiteral("\u6d4b\u8bd5\u8d26\u53f7") + QString(80, 'X'));
    ui.lineEditLiveId->setEditText("https://live.bilibili.com/123456?test=" + QString(180, 'x'));
    const QSize sizes[] = {QSize(620, 560), QSize(660, 620), QSize(900, 740)};
    const QStringList states = {QStringLiteral("\u76d1\u89c6\u5c4f\u5e55"),
        QStringLiteral("\u53d6\u6d88\u786e\u8ba4"), QStringLiteral("\u786e\u8ba4\u767b\u5f55\u4e2d")};
    for (const QSize& size : sizes)
    {
        window.resize(size);
        for (qsizetype state = 0; state < states.size(); ++state)
        {
            ui.pBtstartScreen->setText(states[state]);
            ui.pBtstartScreen->setChecked(state != 0);
            ui.pBtstartScreen->setEnabled(state != 2);
            ui.pBtStream->setEnabled(state == 0);
            app.processEvents();
            if (window.size() != size || !validateLayout(ui))
            {
                std::cerr << "overlap, alignment or text-fit failed at " << size.width()
                          << "x" << size.height() << " state " << state << "\n";
                return 1;
            }
            const QString name = QString("confirmation-ui-%1-%2x%3-state%4.png")
                .arg(window.devicePixelRatioF()).arg(size.width()).arg(size.height()).arg(state);
            if (!window.grab().save(name)) return 1;
        }
    }
    std::cout << "3 window sizes and 3 confirmation states passed at DPI scale "
              << window.devicePixelRatioF() << "; font: "
              << QFontInfo(ui.lineEditUname->font()).family().toStdString() << "\n";
    return 0;
}
