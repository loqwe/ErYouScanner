#include "WindowMain.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <tuple>

#include <QMessageBox>
#include <QWindow>
#include <QStringList>
#include <QClipboard>
#include <QColor>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>

#include "MhyApi.hpp"
#include "ScannerTask.hpp"

WindowMain::WindowMain(QWidget* parent) :
    QMainWindow(parent),
    t1(this),
    t2(this)
{
    QApplication::setFont(QFont("微软雅黑", 9));
    ui.setupUi(this);
    WriteScannerLog("startup: version=" MHY_Scanner_VERSION);
    ui.spinConfirmDelay->setEnabled(false);
    connect(ui.action1_3, &QAction::triggered, this, &WindowMain::AddAccount);
    connect(ui.action1_4, &QAction::triggered, this, &WindowMain::SetDefaultAccount);
    connect(ui.action2_3, &QAction::triggered, this, &WindowMain::DeleteAccount);
    connect(ui.action1_2, &QAction::triggered, this, [this]() {
        WindowAbout WindowAbout(this);
        WindowAbout.exec();
    });
    connect(ui.action2_2, &QAction::triggered, this, []() {
        ShellExecuteW(NULL, L"open", L"https://github.com/loqwe/MHY_Scanner2/issues", NULL, NULL, SW_SHOWNORMAL);
    });
    connect(ui.action1_5, &QAction::triggered, this, []() {
        ShellExecuteW(NULL, L"open", L"config", NULL, NULL, SW_SHOWDEFAULT);
    });
    connect(ui.pBtstartScreen, &QPushButton::clicked, this, &WindowMain::pBtstartScreen);
    connect(this, &WindowMain::StopScanner, this, &WindowMain::pBtStop);
    connect(this, &WindowMain::StartScanScreen, this, [&]() {
        ui.pBtstartScreen->setText("监视屏幕中");
        ui.pBtstartScreen->setEnabled(true);
    });
    connect(this, &WindowMain::AccountError, this, [&]() {
        failure();
        pBtStop();
    });
    connect(this, &WindowMain::StartScanLive, this, [&]() {
        ui.pBtStream->setText("监视直播中");
        ui.pBtStream->setEnabled(true);
    });
    connect(this, &WindowMain::LiveStreamLinkError, this, [&](LiveStreamStatus status) {
        liveIdError(status);
    });
    connect(this, &WindowMain::AccountNotSelected, this, [&]() {
        QMessageBox::information(this, "提示", "没有选择任何账号", QMessageBox::Yes);
        pBtStop();
    });
    connect(ui.checkBoxAutoScreen, &QCheckBox::clicked, this, &WindowMain::checkBoxAutoScreen);
    connect(this, &WindowMain::BackgroundError, this, [this]() {
        if (closing.load()) return;
        pBtStop();
        QMessageBox::information(this, "提示", "后台请求失败，请查看 Config/scanner.log 中的异常记录。", QMessageBox::Yes);
    });
    connect(this, &WindowMain::AccountUnsupported, this, [this]() {
        pBtStop();
        QMessageBox::information(this, "提示", "该账号类型已停止支持，请选择官服账号。旧记录可以从账号管理中手动删除。", QMessageBox::Yes);
    });
    connect(ui.checkBoxAutoExit, &QCheckBox::clicked, this, &WindowMain::checkBoxAutoExit);
    connect(ui.checkBoxAutoLogin, &QCheckBox::clicked, this, &WindowMain::checkBoxAutoLogin);
    connect(ui.spinConfirmDelay, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        userinfo["confirm_delay_seconds"] = seconds;
        m_config->updateConfig(userinfo.dump());
    });
    connect(ui.pBtStream, &QPushButton::clicked, this, &WindowMain::pBtStream);
    connect(ui.tableWidget, &QTableWidget::cellClicked, this, &WindowMain::getInfo);
    connect(&t1, &QRCodeForScreen::loginResults, this, &WindowMain::islogin);
    connect(&t1, &QRCodeForScreen::loginConfirm, this, &WindowMain::loginConfirmTip);
    connect(&t2, &QRCodeForStream::loginResults, this, &WindowMain::islogin);
    connect(&t2, &QRCodeForStream::loginConfirm, this, &WindowMain::loginConfirmTip);
    connect(&t2, &QRCodeForStream::streamReady, this, &WindowMain::StartScanLive);
    connect(&configinitload, &configInitLoad::userinfoTrue, this, &WindowMain::configInitUpdate);
    connect(ui.tableWidget, &QTableWidget::itemChanged, this, &WindowMain::updateNote);

    QThreadPool::globalInstance()->setMaxThreadCount(QThread::idealThreadCount());
    backgroundTasks.setMaxThreadCount(2);
    ui.tableWidget->setColumnCount(6);
    QStringList header;
    header << "序号"
           << "UID"
           << "用户名"
           << "类型"
           << "状态"
           << "备注";
    ui.tableWidget->setHorizontalHeaderLabels(header);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui.tableWidget->horizontalHeader()->setMinimumSectionSize(44);
    ui.tableWidget->setColumnWidth(0, 44);
    ui.tableWidget->setColumnWidth(1, 104);
    ui.tableWidget->setColumnWidth(2, 110);
    ui.tableWidget->setColumnWidth(3, 72);
    ui.tableWidget->setColumnWidth(4, 72);
    ui.tableWidget->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    ui.tableWidget->verticalHeader()->setVisible(false);
    ui.tableWidget->setAlternatingRowColors(true);
    ui.label_3->setText(MHY_Scanner_VERSION);

    ui.tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui.tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked);

    ui.lineEditLiveId->setEditable(true);
    if (ui.lineEditLiveId->lineEdit())
    {
        ui.lineEditLiveId->lineEdit()->setPlaceholderText("直播间ID或链接");
        ui.lineEditLiveId->lineEdit()->setClearButtonEnabled(true);
        connect(ui.lineEditLiveId->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            saveLiveRoomId(ui.lineEditLiveId->currentText());
        });
    }

    ui.tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.tableWidget, &QTableWidget::customContextMenuRequested, this, &WindowMain::onTableRightClicked);

    configinitload.start();
}

WindowMain::~WindowMain()
{
    closing.store(true);
    backgroundTasks.clear();
    confirmationDelay.cancel();
    confirmationJob.waitForFinished();
    backgroundTasks.waitForDone();
    t1.stop();
    t2.stop();
}

void WindowMain::onTableRightClicked(const QPoint& pos)
{
    QTableWidgetItem* item = ui.tableWidget->itemAt(pos);
    if (!item)
    {
        return;
    }
    int row = ui.tableWidget->row(item);
    QMenu contextMenu(this);

    QAction* copyRowAction = contextMenu.addAction("复制Cookie");
    connect(copyRowAction, &QAction::triggered, this, [this, row]() {
        copyEntireRow(row);
    });

    contextMenu.addSeparator();
    contextMenu.exec(ui.tableWidget->mapToGlobal(pos) + QPoint(0, 25));
}

void WindowMain::refreshLiveRoomIds()
{
    const QString currentText = ui.lineEditLiveId->currentText().trimmed();
    QString savedText;

    ui.lineEditLiveId->blockSignals(true);
    ui.lineEditLiveId->clear();

    if (userinfo.contains("live_room_ids") && userinfo["live_room_ids"].is_array())
    {
        for (const auto& item : userinfo["live_room_ids"])
        {
            if (!item.is_string())
            {
                continue;
            }
            const QString id = QString::fromStdString(item.get<std::string>()).trimmed();
            if (!id.isEmpty() && ui.lineEditLiveId->findText(id) < 0)
            {
                ui.lineEditLiveId->addItem(id);
            }
        }
    }

    if (userinfo.contains("live_room_id") && userinfo["live_room_id"].is_string())
    {
        savedText = QString::fromStdString(userinfo["live_room_id"].get<std::string>()).trimmed();
    }

    if (!currentText.isEmpty())
    {
        ui.lineEditLiveId->setCurrentText(currentText);
    }
    else if (!savedText.isEmpty())
    {
        ui.lineEditLiveId->setCurrentText(savedText);
    }

    ui.lineEditLiveId->blockSignals(false);
}

void WindowMain::saveLiveRoomId(const QString& roomId)
{
    const QString text = roomId.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    try
    {
        if (!userinfo.is_object())
        {
            userinfo = nlohmann::json::parse(m_config->getConfig());
        }

        const std::string id = text.toStdString();
        userinfo["live_room_id"] = id;
        if (!userinfo.contains("live_room_ids") || !userinfo["live_room_ids"].is_array())
        {
            userinfo["live_room_ids"] = nlohmann::json::array();
        }

        auto& ids = userinfo["live_room_ids"];
        for (auto it = ids.begin(); it != ids.end();)
        {
            if (it->is_string() && it->get<std::string>() == id)
            {
                it = ids.erase(it);
            }
            else
            {
                ++it;
            }
        }
        ids.insert(ids.begin(), id);
        while (ids.size() > 5)
        {
            ids.erase(ids.end() - 1);
        }

        m_config->updateConfig(userinfo.dump());
        refreshLiveRoomIds();
    }
    catch (const std::exception&)
    {
        QMessageBox::information(this, "错误", "保存直播间ID失败：配置文件异常", QMessageBox::Yes);
    }
}

void WindowMain::setAccountCheckStatus(int row, bool valid, const QString& status)
{
    if (row < 0 || row >= ui.tableWidget->rowCount())
    {
        return;
    }
    QTableWidgetItem* statusItem = ui.tableWidget->item(row, 4);
    if (!statusItem)
    {
        return;
    }
    statusItem->setText(status);
    statusItem->setForeground(valid ? QColor(0, 130, 0) : QColor(220, 0, 0));
}

void WindowMain::startAccountSelfCheck()
{
    if (!userinfo.contains("account") || !userinfo["account"].is_array())
    {
        return;
    }

    std::vector<std::tuple<int, std::string, std::string, std::string, std::string>> accounts;
    const int accountCount = std::max(0, std::min(userinfo.value("num", 0), static_cast<int>(userinfo["account"].size())));
    accounts.reserve(accountCount);

    for (int i = 0; i < accountCount; ++i)
    {
        QTableWidgetItem* statusItem = ui.tableWidget->item(i, 4);
        if (statusItem)
        {
            statusItem->setText("检测中");
            statusItem->setForeground(QColor(80, 80, 80));
        }

        const auto& account = userinfo["account"][i];
        accounts.emplace_back(
            i,
            account.value("type", std::string{}),
            account.value("access_key", std::string{}),
            account.value("uid", std::string{}),
            account.value("mid", std::string{}));
    }

    QPointer<WindowMain> self(this);
    backgroundTasks.start(MakeScannerTask("account-check: worker", [self, accounts = std::move(accounts)] {
        for (const auto& [row, type, token, uid, mid] : accounts)
        {
            if (!self || self->closing.load())
            {
                return;
            }

            const bool uidLooksValid = !uid.empty() &&
                std::all_of(uid.begin(), uid.end(), [](unsigned char ch) {
                    return std::isdigit(ch) != 0;
                });

            bool valid = false;
            QString status = "检测失败";
            const std::string phase = "account-check: row=" + std::to_string(row + 1);
            WriteScannerLog(phase + " begin");
            const bool checked = RunScannerTask(phase.c_str(), [&] {
                if (type != "官服")
                {
                    status = "不支持";
                    WriteScannerLog(phase + " unsupported account type; skipped");
                }
                else if (!uidLooksValid)
                {
                    status = "UID异常";
                }
                else if (type == "官服")
                {
                    valid = !token.empty() && !mid.empty() && CheckStokenValid(token, mid);
                    status = valid ? "有效" : "失效";
                }
            });
            WriteScannerLog(phase + " complete checked=" + std::to_string(checked) + " valid=" + std::to_string(valid));

            QMetaObject::invokeMethod(self.data(), [self, row, valid, status]() {
                if (self && !self->closing.load())
                {
                    self->setAccountCheckStatus(row, valid, status);
                }
            }, Qt::QueuedConnection);
        }
    }));
}

void WindowMain::insertTableItems(QString uid, QString userName, QString type, QString notes)
{
    QTableWidgetItem* item[6]{};
    int nCount = ui.tableWidget->rowCount();
    ui.tableWidget->insertRow(nCount);
    item[0] = new QTableWidgetItem(QString("%1").arg(nCount + 1));
    ui.tableWidget->setItem(nCount, 0, item[0]);
    item[1] = new QTableWidgetItem(uid);
    ui.tableWidget->setItem(nCount, 1, item[1]);
    item[2] = new QTableWidgetItem(userName);
    ui.tableWidget->setItem(nCount, 2, item[2]);
    item[3] = new QTableWidgetItem(type);
    ui.tableWidget->setItem(nCount, 3, item[3]);
    item[4] = new QTableWidgetItem("未检测");
    ui.tableWidget->setItem(nCount, 4, item[4]);
    item[5] = new QTableWidgetItem(notes);
    ui.tableWidget->setItem(nCount, 5, item[5]);

    for (int i = 0; i < 5; i++)
    {
        QTableWidgetItem* item1 = ui.tableWidget->item(nCount, i);
        item1->setFlags(item1->flags() & ~Qt::ItemIsEditable);
    }
}

void WindowMain::AddAccount()
{
    if (t1.isRunning() || t2.isRunning())
    {
        QMessageBox::information(this, "错误", "请先停止识别！", QMessageBox::Yes);
        return;
    }
    windowLogin = new WindowLogin(this);
    connect(windowLogin, &WindowLogin::AddUserInfo, this, [this](const std::string name, const std::string token, const std::string uid, const std::string mid, const std::string type) {
        if (checkDuplicates(uid.data()))
        {
            QMessageBox::information(this, "提示", "该账号已添加，无需重复添加", QMessageBox::Yes);
            return;
        }
        // Account state belongs to the UI thread, not an untracked pool worker.
        const bool saved = RunScannerTask("account-save", [&] {
            int num{ userinfo["num"] };
            userinfo["account"][num]["access_key"] = token;
            userinfo["account"][num]["uid"] = uid;
            userinfo["account"][num]["name"] = name;
            userinfo["account"][num]["type"] = type;
            userinfo["account"][num]["note"] = "";
            userinfo["account"][num]["mid"] = mid;
            userinfo["num"] = num + 1;
            m_config->updateConfig(userinfo.dump());
        });
        if (saved)
        {
            insertTableItems(QString::fromStdString(uid), QString::fromStdString(name), QString::fromStdString(type), "");
            countA = ui.tableWidget->rowCount() - 1;
            ui.tableWidget->setCurrentCell(countA, 0);
            ui.lineEditUname->setText(QString::fromStdString(name));
        }
        QMessageBox::information(this, "提示", saved ? "添加成功" : "账号保存失败，请查看日志", QMessageBox::Yes);
    });
    windowLogin->show();
}

void WindowMain::pBtstartScreen(bool clicked)
{
    if (closing.load()) return;
    if (confirmationDelay.pending())
    {
        pBtStop();
        return;
    }
    ui.pBtstartScreen->setEnabled(false);
    ui.pBtStream->setEnabled(false);
    ui.pBtstartScreen->setText("加载中。。。");
    const int selectedRow = countA;
    nlohmann::json account;
    if (!RunScannerTask("screen: account snapshot", [&] {
        if (clicked && selectedRow >= 0) account = userinfo.at("account").at(selectedRow);
    }, [this] { Q_EMIT BackgroundError(); })) return;
    backgroundTasks.start(MakeScannerTask("screen: startup", [this, clicked, selectedRow, account = std::move(account)]() {
        WriteScannerLog("screen: startup begin");
        if (!clicked)
        {
            emit StopScanner();
            return;
        }
        //FIXME 没有及时更新
        if (selectedRow == -1)
        {
            emit AccountNotSelected();
            return;
        }
        if (std::string type = account.at("type"); type == "官服")
        {
            std::string stoken = account.at("access_key");
            std::string uid = account.at("uid");
            std::string mid = account.at("mid");
            if (!CheckStokenValid(stoken, mid))
            {
                emit AccountError();
                return;
            }
            t1.setLoginInfo1(uid, stoken, mid);
        }
        else
        {
            WriteScannerLog("screen: unsupported account type; startup rejected");
            emit AccountUnsupported();
            return;
        }
        if (closing.load()) return;
        t1.start();
        emit StartScanScreen();
    }, [this] { Q_EMIT BackgroundError(); }));
}

void WindowMain::pBtStream(bool clicked)
{
    if (closing.load()) return;
    if (confirmationDelay.pending())
    {
        pBtStop();
        return;
    }
    ui.pBtstartScreen->setEnabled(false);
    ui.pBtStream->setEnabled(false);
    ui.pBtStream->setText("加载中。。。");
    const auto platform = static_cast<LivePlatform>(ui.comboBox->currentIndex());
    const QString roomIdText = ui.lineEditLiveId->currentText().trimmed();
    if (clicked && !roomIdText.isEmpty())
    {
        saveLiveRoomId(roomIdText);
    }
    const std::string roomId = roomIdText.toStdString();

    const int selectedRow = countA;
    nlohmann::json account;
    if (!RunScannerTask("stream: account snapshot", [&] {
        if (clicked && selectedRow >= 0) account = userinfo.at("account").at(selectedRow);
    }, [this] { Q_EMIT BackgroundError(); })) return;
    backgroundTasks.start(MakeScannerTask("stream: startup", [this, clicked, platform, roomId, selectedRow, account = std::move(account)]() {
        WriteScannerLog("stream: startup begin");
        if (!clicked)
        {
            emit StopScanner();
            return;
        }
        if (selectedRow == -1)
        {
            emit AccountNotSelected();
            return;
        }
        if (account.at("type") != "官服")
        {
            WriteScannerLog("stream: unsupported account type; startup rejected");
            emit AccountUnsupported();
            return;
        }
        std::string stream_link;
        std::map<std::string, std::string> heards;
        //检查直播间状态
        if (!GetStreamLink(platform, roomId, stream_link, heards))
        {
            emit StopScanner();
            return;
        }
        else
        {
            t2.stop();
            t2.wait();
            t2.setUrl(stream_link, heards);
        }
        {
            std::string stoken = account.at("access_key");
            std::string uid = account.at("uid");
            std::string mid = account.at("mid");
            if (!CheckStokenValid(stoken, mid))
            {
                emit AccountError();
                return;
            }
            t2.setLoginInfo1(uid, stoken, mid);
        }
        if (closing.load()) return;
        t2.start();
    }, [this] { Q_EMIT BackgroundError(); }));
}

void WindowMain::closeEvent(QCloseEvent* event)
{
    closing.store(true);
    backgroundTasks.clear();
    confirmationDelay.cancel();
    saveLiveRoomId(ui.lineEditLiveId->currentText());
    t1.stop();
    t2.stop();
}

void WindowMain::showEvent(QShowEvent* event)
{
}

void WindowMain::islogin(const ScanRet ret)
{
    const bool screenMonitoring = ui.pBtstartScreen->isChecked();
    if (ret == ScanRet::SUCCESS && (bool)userinfo["auto_exit"] == true)
    {
        exit(0);
    }
    pBtStop();
    SetWindowToFront();
    QMessageBox* messageBox = new QMessageBox(this);
    auto Show_QMessageBox = [&](const QString& title, const QString& text) {
        messageBox->setIcon(QMessageBox::Information);
        messageBox->setWindowTitle(title);
        messageBox->setText(text);
        messageBox->addButton(QMessageBox::Yes);
        messageBox->show();
    };
    switch (ret)
    {
    case ScanRet::UNKNOW:
        break;
    case ScanRet::FAILURE_1:
        Show_QMessageBox("提示", "扫码失败!");
        break;
    case ScanRet::FAILURE_2:
        Show_QMessageBox("提示", "扫码二次确认失败!");
        break;
    case ScanRet::LIVESTOP:
        Show_QMessageBox("提示", "直播中断!");
        break;
    case ScanRet::STREAMERROR:
        Show_QMessageBox("提示", screenMonitoring ? "屏幕捕获初始化或识别失败!"
                                                   : "直播流初始化或解码失败!");
        break;
    case ScanRet::SUCCESS:
        Show_QMessageBox("提示", "扫码成功!");
        break;
    default:
        break;
    }
}

void WindowMain::loginConfirmTip(const GameType gameType, bool b)
{
    if (confirmationDelay.pending() || confirmationJob.isRunning() ||
        !(b ? ui.pBtstartScreen : ui.pBtStream)->isChecked())
    {
        return;
    }
    if (userinfo.value("auto_login", false))
    {
        scheduleConfirmation(b);
        return;
    }
    QString info("正在使用账号" + ui.lineEditUname->text());
    switch (gameType)
    {
    case GameType::Honkai3:
        info += "\n登录崩坏3\n";
        break;
    case GameType::Genshin:
        info += "\n登录原神\n";
        break;
    case GameType::HonkaiStarRail:
        info += "\n登录星穹铁道\n";
        break;
    case GameType::ZenlessZoneZero:
        info += "\n登录绝区零\n";
        break;
    default:
        break;
    }
    SetWindowToFront();
    QMessageBox* messageBox = new QMessageBox(this);
    messageBox->setWindowTitle("登录确认");
    messageBox->setText(info + "确认登录？");
    QAbstractButton* yesButton = messageBox->addButton("登录", QMessageBox::YesRole);
    QAbstractButton* noButton = messageBox->addButton("取消", QMessageBox::NoRole);
    messageBox->exec();
    pBtStop();
    if (messageBox->clickedButton() != yesButton)
    {
        return;
    }
    scheduleConfirmation(b);
}

void WindowMain::scheduleConfirmation(bool screen)
{
    pBtStop();
    ui.spinConfirmDelay->setEnabled(false);
    auto* button = screen ? ui.pBtstartScreen : ui.pBtStream;
    auto* other = screen ? ui.pBtStream : ui.pBtstartScreen;
    button->setText("取消确认");
    button->setChecked(true);
    other->setEnabled(false);
    const double seconds = ui.spinConfirmDelay->value();
    confirmationDelay.schedule(seconds, [this, screen]() {
        ui.pBtstartScreen->setEnabled(false);
        ui.pBtStream->setEnabled(false);
        auto* active = screen ? ui.pBtstartScreen : ui.pBtStream;
        active->setText("确认登录中");
        confirmationJob = QtConcurrent::run([this, screen]() {
            // The QR worker must finish publishing its state before confirmation.
            try
            {
                if (screen)
                {
                    t1.wait();
                    t1.continueLastLogin();
                }
                else
                {
                    t2.wait();
                    t2.continueLastLogin();
                }
            }
            catch (...)
            {
                LogScannerException("confirmation: login request");
                QMetaObject::invokeMethod(this, [this]() { islogin(ScanRet::FAILURE_2); }, Qt::QueuedConnection);
            }
        });
    });
}

void WindowMain::checkBoxAutoScreen(bool clicked)
{
    int state = ui.checkBoxAutoScreen->checkState();
    if ((int)userinfo["last_account"] == 0)
    {
        ui.checkBoxAutoScreen->setChecked(false);
        QMessageBox::information(this, "提示", "你没有选择默认账号!", QMessageBox::Yes);
        return;
    }
    if (state == Qt::Checked)
    {
        ui.checkBoxAutoScreen->setChecked(true);
        userinfo["auto_start"] = true;
    }
    else if (state == Qt::Unchecked)
    {
        ui.checkBoxAutoScreen->setChecked(false);
        userinfo["auto_start"] = false;
    }
    m_config->updateConfig(userinfo.dump());
}

void WindowMain::checkBoxAutoExit(bool clicked)
{
    int state = ui.checkBoxAutoExit->checkState();
    if (state == Qt::Checked)
    {
        userinfo["auto_exit"] = true;
    }
    else if (state == Qt::Unchecked)
    {
        userinfo["auto_exit"] = false;
    }
    m_config->updateConfig(userinfo.dump());
}

void WindowMain::checkBoxAutoLogin(bool clicked)
{
    int state = ui.checkBoxAutoLogin->checkState();
    if (state == Qt::Checked)
    {
        userinfo["auto_login"] = true;
    }
    else if (state == Qt::Unchecked)
    {
        userinfo["auto_login"] = false;
    }
    m_config->updateConfig(userinfo.dump());
}

void WindowMain::liveIdError(const LiveStreamStatus status)
{
    switch (status)
    {
        using enum LiveStreamStatus;
    case Absent:
    {
        QMessageBox::information(this, "提示", "直播间不存在!", QMessageBox::Yes);
        return;
    }
    case NotLive:
    {
        QMessageBox::information(this, "提示", "直播间未开播！", QMessageBox::Yes);
        return;
    }
    case Error:
    {
        QMessageBox::information(this, "提示", "直播间未知错误!", QMessageBox::Yes);
        return;
    }
    default:
        return;
    }
    return;
}

int WindowMain::getSelectedRowIndex()
{
    QList<QTableWidgetItem*> item = ui.tableWidget->selectedItems();
    if (item.count() == 0)
    {
        return -1;
    }
    return ui.tableWidget->row(item.at(0));
}

bool WindowMain::checkDuplicates(const std::string uid)
{
    for (int i = 0; i < (int)userinfo["num"]; i++)
    {
        std::string m_uid = userinfo["account"][i]["uid"];
        if (uid == m_uid)
        {
            return true;
        }
    }
    return false;
}

bool WindowMain::GetStreamLink(LivePlatform platform, const std::string& roomid, std::string& url, std::map<std::string, std::string>& heards)
{
    auto info = GetLiveInfo(platform, roomid);
    if (info.status == LiveStreamStatus::Normal)
    {
        url = info.link;
        if (platform == LivePlatform::BiliBili)
        {
            heards["user_agent"] =
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "Chrome/126.0.0.0 Safari/537.36";
            heards["headers"] =
                "Referer: https://live.bilibili.com/\r\n"
                "Origin: https://live.bilibili.com\r\n";
        }
        return true;
    }
    else
    {
        Q_EMIT LiveStreamLinkError(info.status);
        return false;
    }
}

void WindowMain::SetWindowToFront() const
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (GetForegroundWindow() == hwnd)
        return;
    ShowWindow(hwnd, SW_MINIMIZE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    SetForegroundWindow(hwnd);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
}

void WindowMain::failure()
{
    QMessageBox* messageBox = new QMessageBox(this);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->setText("登录状态失效，\n请重新添加账号！");
    messageBox->setWindowTitle("提示");
    messageBox->setIcon(QMessageBox::Information);
    messageBox->show();
}

void WindowMain::getInfo(int x, int y)
{
    QTableWidgetItem* item = ui.tableWidget->item(x, 2);
    QString cellText = item->text();
    ui.lineEditUname->setText(cellText);
    countA = x;

    //trrlog::Log_debug("{}", std::format(R"(row = {} , user_name = {})", x, cellText.toStdString()));
}

void WindowMain::SetDefaultAccount()
{
    int nCurrentRow = getSelectedRowIndex();
    if (nCurrentRow != -1)
    {
        //ui.tableWidget->setCurrentCell(nCurrentRow, QItemSelectionModel::Current);
        userinfo["last_account"] = nCurrentRow + 1;
        m_config->updateConfig(userinfo.dump());
        QMessageBox::information(this, "设置成功！", "勾选下方\"启动时自动监视屏幕\"\n将在下次启动时自动扫描并使用该账号登录", QMessageBox::Yes);
        return;
    }
    else
    {
        QMessageBox::information(this, "提示", "没有选择任何账号", QMessageBox::Yes);
        return;
    }
}

void WindowMain::DeleteAccount()
{
    int nCurrentRow = getSelectedRowIndex();
    if (nCurrentRow == -1)
    {
        QMessageBox::information(this, "提示", "没有选择任何账号", QMessageBox::Yes);
        return;
    }
    if (int re = QMessageBox::information(this, "删除确认", QString::fromStdString(std::format("你正在删除账号\n{}", (std::string)userinfo["account"][countA]["name"])), QMessageBox::Yes | QMessageBox::No);
        re != QMessageBox::Yes)
    {
        return;
    }
    userinfo["num"] = (int)userinfo["num"] - 1;
    //判断删除的账号是否为启动默认账号
    if (static_cast<int>(userinfo["last_account"]) == countA + 1)
    {
        userinfo["last_account"] = 0;
    }
    else if (static_cast<int>(userinfo["last_account"]) > countA + 1)
    {
        userinfo["last_account"] = static_cast<int>(userinfo["last_account"]) - 1;
    }
    userinfo["account"].erase(countA);

    //trrlog::Log_debug("{}", userinfo.str());

    m_config->updateConfig(userinfo.dump());
    //ui.tableWidget->setCurrentCell(nCurrentRow, QItemSelectionModel::Current);
    ui.tableWidget->removeRow(nCurrentRow);
    ui.tableWidget->clearSelection();
    ui.lineEditUname->setText("未选中");
    countA = -1;

    disconnect(ui.tableWidget, &QTableWidget::itemChanged, this, &WindowMain::updateNote);
    for (int i = 0; i < (int)userinfo["num"]; i++)
    {
        QTableWidgetItem* item = new QTableWidgetItem(QString("%1").arg(i + 1));
        ui.tableWidget->setItem(i, 0, item);
    }
    connect(ui.tableWidget, &QTableWidget::itemChanged, this, &WindowMain::updateNote);
}

void WindowMain::pBtStop()
{
    confirmationDelay.cancel();
    ui.spinConfirmDelay->setEnabled(true);
    t1.stop();
    t2.stop();
    ui.pBtstartScreen->setText("监视屏幕");
    ui.pBtStream->setText("监视直播间");
    ui.pBtstartScreen->setChecked(false);
    ui.pBtStream->setChecked(false);
    ui.pBtstartScreen->setEnabled(true);
    ui.pBtStream->setEnabled(true);
}

void WindowMain::configInitUpdate()
{
    ui.tableWidget->blockSignals(true);
    try
    {
        userinfo = nlohmann::json::parse(m_config->getConfig());
        {
            const QSignalBlocker blocker(ui.spinConfirmDelay);
            ui.spinConfirmDelay->setValue(ConfirmationDelayMs(userinfo.value("confirm_delay_seconds", 0.0)) / 1000.0);
        }
        ui.spinConfirmDelay->setEnabled(true);
        for (int i = 0; i < userinfo["num"].get<int>(); i++)
        {
            insertTableItems(
                QString::fromStdString(userinfo["account"][i]["uid"]),
                QString::fromStdString(userinfo["account"][i]["name"]),
                QString::fromStdString(userinfo["account"][i]["type"]),
                QString::fromStdString(userinfo["account"][i]["note"]));
        }
        if (userinfo["num"].get<int>() > 0 && countA == -1)
        {
            countA = static_cast<int>(userinfo["last_account"]) > 0 ? static_cast<int>(userinfo["last_account"]) - 1 : 0;
            ui.lineEditUname->setText(QString::fromStdString(userinfo["account"][countA]["name"]));
            ui.tableWidget->setCurrentCell(countA, 0);
        }
        if (userinfo["auto_start"] && static_cast<int>(userinfo["last_account"]) != 0)
        {
            countA = static_cast<int>(userinfo["last_account"]) - 1;
            ui.pBtstartScreen->clicked(true);
            ui.pBtstartScreen->setChecked(true);
            ui.checkBoxAutoScreen->setChecked(true);
            ui.lineEditUname->setText(QString::fromStdString(userinfo["account"][countA]["name"]));
            ui.tableWidget->setCurrentCell(countA, QItemSelectionModel::Select);
        }
        if (userinfo["auto_exit"])
        {
            ui.checkBoxAutoExit->setChecked(true);
        }
        if (userinfo["auto_login"])
        {
            ui.checkBoxAutoLogin->setChecked(true);
        }
        refreshLiveRoomIds();
        startAccountSelfCheck();
    }
    catch (...)
    {
        LogScannerException("startup: configuration load");
        int result = QMessageBox::information(this, "错误", "配置文件错误！\n重置配置文件为空？", QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes)
        {
            m_config->defaultConfig();
        }
        else
        {
            QMessageBox::information(this, "错误", "配置文件错误！\n无法继续运行！", QMessageBox::Yes);
            exit(1);
        }
    }
    ui.tableWidget->blockSignals(false);
}

void WindowMain::updateNote(QTableWidgetItem* item)
{
    if (!item || item->column() != 5)
    {
        return;
    }
    QString text = item->text();
    userinfo["account"][item->row()]["note"] = text.toStdString();
    m_config->updateConfig(userinfo.dump());
}

void WindowMain::copyEntireRow(int row)
{
    QString rowData;
    if (userinfo["account"][row]["type"] != "官服")
    {
        QMessageBox::information(this, "提示", "该账号类型已停止支持，请选择官服账号。", QMessageBox::Yes);
        return;
    }
    std::string stoken = userinfo["account"][row]["access_key"];
    std::string stuid = userinfo["account"][row]["uid"];
    std::string mid = userinfo["account"][row]["mid"];
    rowData = QString::fromStdString(
        "stoken=" + stoken + "; " +
        "stuid=" + stuid + "; " +
        "mid=" + mid);
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(rowData);
}


void configInitLoad::run()
{
    Q_EMIT userinfoTrue();
}

configInitLoad::~configInitLoad()
{
    wait();
}
