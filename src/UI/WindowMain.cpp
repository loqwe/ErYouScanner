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
#include <QFrame>
#include <QToolButton>
#include <QButtonGroup>
#include <QToolTip>
#include <QUuid>
#include <QTimer>
#include <QFormLayout>
#include <QPainterPath>

#include "MhyApi.hpp"
#include "ScannerTask.hpp"
#include "WindowLogin.h"
#include "WindowLoginKuro.h"
#include "KuroScanSmsDialog.hpp"
#include "AccountPlatform.hpp"
#include <opencv2/opencv.hpp>

namespace
{
QIcon makeClearButtonIcon()
{
    const int sz = 32;
    QPixmap normalPix(sz, sz);
    normalPix.fill(Qt::transparent);
    {
        QPainter p(&normalPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0, 0, 0, 15));
        p.setPen(Qt::NoPen);
        p.drawEllipse(2, 2, sz - 4, sz - 4);
        p.setPen(QPen(QColor("#94a3b8"), 2.2));
        const int offset = 10;
        p.drawLine(offset, offset, sz - offset, sz - offset);
        p.drawLine(sz - offset, offset, offset, sz - offset);
    }

    QPixmap hoverPix(sz, sz);
    hoverPix.fill(Qt::transparent);
    {
        QPainter p(&hoverPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#fee2e2"));
        p.setPen(QPen(QColor("#fca5a5"), 1.2));
        p.drawEllipse(2, 2, sz - 4, sz - 4);
        p.setPen(QPen(QColor("#dc2626"), 2.4));
        const int offset = 10;
        p.drawLine(offset, offset, sz - offset, sz - offset);
        p.drawLine(sz - offset, offset, offset, sz - offset);
    }

    QIcon icon;
    icon.addPixmap(normalPix, QIcon::Normal);
    icon.addPixmap(hoverPix, QIcon::Active);
    icon.addPixmap(hoverPix, QIcon::Selected);
    return icon;
}

void preloadAvatar(int platform, const QString& roomId, const QString& avatarUrl, std::function<void()> onDone = nullptr)
{
    if (avatarUrl.isEmpty() || roomId.isEmpty()) return;
    const QString cacheKey = QStringLiteral("%1_%2").arg(platform).arg(roomId);
    const QString pngPath = QStringLiteral("Config/cache/avatar/") + cacheKey + QStringLiteral(".png");
    if (QFile::exists(pngPath))
    {
        return;
    }

    QThreadPool::globalInstance()->start([platform, roomId, avatarUrl, cacheKey, pngPath, onDone = std::move(onDone)]() {
        QDir().mkpath(QStringLiteral("Config/cache/avatar/"));
        std::string reqUrl = avatarUrl.trimmed().toStdString();
        if (reqUrl.rfind("//", 0) == 0)
        {
            reqUrl = "https:" + reqUrl;
        }

        std::string referer = "https://live.bilibili.com/";
        if (reqUrl.find("douyin") != std::string::npos || reqUrl.find("byted") != std::string::npos)
        {
            referer = "https://live.douyin.com/";
        }

        auto r = cpr::Get(
            cpr::Url{ reqUrl },
            cpr::VerifySsl{ false },
            cpr::Redirect{ true },
            cpr::Timeout{ 8000 },
            cpr::ConnectTimeout{ 5000 },
            cpr::Header{
                { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36" },
                { "Referer", referer },
                { "Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8" }
            });
        if (!r.error && r.status_code == 200 && !r.text.empty())
        {
            QImage img;
            if (!img.loadFromData(reinterpret_cast<const uchar*>(r.text.data()), static_cast<int>(r.text.size())))
            {
                std::vector<uchar> buf(r.text.begin(), r.text.end());
                cv::Mat mat = cv::imdecode(buf, cv::IMREAD_COLOR);
                if (!mat.empty())
                {
                    img = QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_BGR888).copy();
                }
            }

            if (!img.isNull())
            {
                img.save(pngPath, "PNG");
            }

            if (onDone)
            {
                onDone();
            }
        }
        else
        {
            WriteScannerLog(std::format("preloadAvatar: download failed code={}, err={}, url={}", r.status_code, r.error.message, reqUrl));
        }
    });
}
} // namespace

WindowMain::WindowMain(QWidget* parent) :
    QMainWindow(parent),
    t1(this),
    t2(this)
{
    QApplication::setFont(QFont("微软雅黑", 9));
    ui.setupUi(this);
    initCornerSwitcher();
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
        ShellExecuteW(NULL, L"open", L"https://github.com/loqwe/ErYouScanner/issues", NULL, NULL, SW_SHOWNORMAL);
    });
    connect(ui.action1_5, &QAction::triggered, this, []() {
        ShellExecuteW(NULL, L"open", L"config", NULL, NULL, SW_SHOWDEFAULT);
    });
    connect(ui.pBtstartScreen, &QPushButton::clicked, this, &WindowMain::pBtstartScreen);
    connect(this, &WindowMain::StopScanner, this, &WindowMain::pBtStop);
    connect(this, &WindowMain::StartScanScreen, this, [&]() {
        startupActive = false;
        ui.pBtstartScreen->setText("监视屏幕中");
        ui.pBtstartScreen->setEnabled(true);
    });
    connect(this, &WindowMain::AccountError, this, [&]() {
        failure();
        pBtStop();
    });
    connect(this, &WindowMain::StartScanLive, this, [&]() {
        startupActive = false;
        ui.pBtStream->setText("监视直播中");
        ui.pBtStream->setEnabled(true);
    });
    connect(this, &WindowMain::LiveStreamLinkError, this, [this](LiveStreamStatus status) {
        pBtStop();
        liveIdError(status);
        updateCurrentRoomCard();
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
        QMessageBox::information(this, "提示", "请选择米哈游官服或库街区官服账号。", QMessageBox::Yes);
    });
    connect(ui.checkBoxAutoExit, &QCheckBox::clicked, this, &WindowMain::checkBoxAutoExit);
    connect(ui.checkBoxAutoLogin, &QCheckBox::clicked, this, &WindowMain::checkBoxAutoLogin);
    connect(ui.spinConfirmDelay, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        userinfo["confirm_delay_seconds"] = seconds;
        saveUserInfo();
    });
    connect(ui.spinScanInterval, &QSpinBox::valueChanged, this, [this](int ms) {
        userinfo["scan_interval_ms"] = ms;
        if (!saveUserInfo()) return;
        t1.setScanInterval(ms);
        t2.setScanInterval(ms);
    });

    const QString tipConfirmDelay = QStringLiteral(
        "【自动二次确认延迟】\n"
        "• 作用：扫码成功后，延迟指定的秒数再发起最终登录确认请求。\n"
        "• 用途：留出时间核对账号，倒计时期间可点击取消，防止误登或风控。\n"
        "• 范围：0.0 ~ 60.0 秒（步长 0.1 秒）\n"
        "• 默认值：0.0 秒（即识别成功后立即确认）"
    );
    ui.spinConfirmDelay->setToolTip(tipConfirmDelay);
    ui.labelConfirmDelayHelp->setToolTip(tipConfirmDelay);

    const QString tipScanInterval = QStringLiteral(
        "【识别间隔时间】\n"
        "• 作用：控制屏幕截屏识别与直播流抽帧解码的循环等待间隔。\n"
        "• 用途：数值越小响应越快；数值适中可显著降低 CPU/GPU 占用与发热。\n"
        "• 范围：10 ~ 5000 毫秒（步长 10 毫秒）\n"
        "• 默认值：200 毫秒"
    );
    ui.spinScanInterval->setToolTip(tipScanInterval);
    ui.labelScanIntervalHelp->setToolTip(tipScanInterval);
    ui.labelConfirmDelayHelp->installEventFilter(this);
    ui.labelScanIntervalHelp->installEventFilter(this);

    connect(ui.pBtStream, &QPushButton::clicked, this, &WindowMain::pBtStream);
    connect(ui.tableWidget, &QTableWidget::cellClicked, this, &WindowMain::getInfo);
    const auto result = [this](ScanRet status, quint64 generation) {
        if (!closing.load() && generation == scanGeneration.load()) islogin(status);
    };
    const auto confirm = [this](GameType game, bool screen, quint64 generation) {
        if (!closing.load() && generation == scanGeneration.load()) loginConfirmTip(game, screen);
    };
    connect(&t1, &QRCodeForScreen::loginResults, this, result);
    connect(&t1, &QRCodeForScreen::loginConfirm, this, confirm);
    connect(&t2, &QRCodeForStream::loginResults, this, result);
    connect(&t2, &QRCodeForStream::loginConfirm, this, confirm);
    connect(&t2, &QRCodeForStream::streamReady, this, [this](quint64 generation) {
        if (!closing.load() && generation == scanGeneration.load()) Q_EMIT StartScanLive();
    });
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
    ui.tableWidget->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
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
    liveRoomDelegate = new LiveRoomItemDelegate(ui.lineEditLiveId->view());
    ui.lineEditLiveId->setItemDelegate(liveRoomDelegate);
    connect(liveRoomDelegate, &LiveRoomItemDelegate::deleteItemRequested, this, &WindowMain::removeLiveRoomHistory);

    ui.lineEditLiveId->setMaxVisibleItems(5);
    ui.lineEditLiveId->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    if (ui.lineEditLiveId->view() && ui.lineEditLiveId->view()->viewport())
    {
        ui.lineEditLiveId->view()->viewport()->installEventFilter(this);
    }

    // 下拉视图右键菜单删除
    ui.lineEditLiveId->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.lineEditLiveId->view(), &QAbstractItemView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex index = ui.lineEditLiveId->view()->indexAt(pos);
        if (index.isValid())
        {
            QMenu menu(this);
            QAction* delAction = menu.addAction(QStringLiteral("从历史记录中删除"));
            connect(delAction, &QAction::triggered, this, [this, row = index.row()] {
                removeLiveRoomHistory(row);
            });
            menu.exec(ui.lineEditLiveId->view()->viewport()->mapToGlobal(pos));
        }
    });

    // 选中历史项自动联动切换平台
    connect(ui.lineEditLiveId, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (index >= 0)
        {
            const int platform = ui.lineEditLiveId->itemData(index, LiveRoomPlatformRole).toInt();
            ui.comboBox->setCurrentIndex(livePlatformToComboBox(platform));
            const QString roomId = ui.lineEditLiveId->itemData(index, LiveRoomIdRole).toString();
            if (!roomId.isEmpty())
            {
                ui.lineEditLiveId->setEditText(roomId);
            }
        }
    });

    ui.lineEditLiveId->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (ui.lineEditLiveId->lineEdit())
    {
        auto* edit = ui.lineEditLiveId->lineEdit();
        edit->setPlaceholderText("直播间ID或链接");
        edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        edit->setClearButtonEnabled(false); // 彻底关闭 Windows 原生黑圆圈白叉！
        edit->setStyleSheet("QLineEdit { selection-background-color: #087e72; }");
        auto* clearAction = edit->addAction(makeClearButtonIcon(), QLineEdit::TrailingPosition);
        clearAction->setVisible(!edit->text().trimmed().isEmpty());
        connect(clearAction, &QAction::triggered, edit, [edit]() {
            edit->clear();
        });
        connect(edit, &QLineEdit::textChanged, clearAction, [clearAction](const QString& text) {
            clearAction->setVisible(!text.trimmed().isEmpty());
        });
        connect(edit, &QLineEdit::editingFinished, this, [this]() {
            saveLiveRoomId(ui.lineEditLiveId->currentText());
        });
    }

    initLiveRoomInfoCard();
    connect(ui.lineEditLiveId, &QComboBox::currentTextChanged, this, [this](const QString&) {
        updateCurrentRoomCard();
    });
    connect(ui.comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        refreshLiveRoomIds();
        updateCurrentRoomCard();
    });

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
    if (row >= userinfo["account"].size()) return;
    if (!accountControlsBusy())
    {
        ui.tableWidget->setCurrentCell(row, 0);
        getInfo(row, 0);
    }
    QMenu contextMenu(this);
    contextMenu.setObjectName(QStringLiteral("accountContextMenu"));

    QAction* copyRowAction = contextMenu.addAction(accountPlatform(userinfo["account"][row]) == AccountPlatform::Kuro ? "复制Token" : "复制Cookie");
    connect(copyRowAction, &QAction::triggered, this, [this, row]() {
        copyEntireRow(row);
    });

    contextMenu.addSeparator();
    auto* relogin = contextMenu.addAction(QStringLiteral("重新登录"));
    relogin->setObjectName(QStringLiteral("reauthenticateAccount"));
    relogin->setEnabled(!accountControlsBusy() && accountPlatform(userinfo["account"][row]) != AccountPlatform::Unsupported);
    connect(relogin, &QAction::triggered, this, [this, row] { reauthenticateAccount(row); });
    contextMenu.exec(ui.tableWidget->viewport()->mapToGlobal(pos));
}

void WindowMain::initLiveRoomInfoCard()
{
    liveRoomInfoCard = new QWidget(this);
    liveRoomInfoCard->setObjectName(QStringLiteral("liveRoomInfoCard"));
    liveRoomInfoCard->setFixedHeight(32);
    liveRoomInfoCard->setStyleSheet(
        "#liveRoomInfoCard {"
        "  background: #f8fafc;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 6px;"
        "}"
    );

    auto* cardLayout = new QHBoxLayout(liveRoomInfoCard);
    cardLayout->setContentsMargins(8, 4, 8, 4);
    cardLayout->setSpacing(6);

    labelCardAvatar = new QLabel(liveRoomInfoCard);
    labelCardAvatar->setFixedSize(22, 22);

    labelCardPlatform = new QLabel(liveRoomInfoCard);

    labelCardAnchor = new QLabel(liveRoomInfoCard);
    labelCardAnchor->setStyleSheet("font-weight: bold; font-size: 12px; color: #1e293b;");

    labelCardSep = new QLabel("·", liveRoomInfoCard);
    labelCardSep->setStyleSheet("color: #94a3b8; font-weight: bold; font-size: 12px;");

    labelCardTitle = new QLabel(liveRoomInfoCard);
    labelCardTitle->setStyleSheet("color: #64748b; font-size: 11px;");
    labelCardTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    labelCardStatus = new QLabel(liveRoomInfoCard);

    cardLayout->addWidget(labelCardAvatar);
    cardLayout->addWidget(labelCardPlatform);
    cardLayout->addWidget(labelCardAnchor);
    cardLayout->addWidget(labelCardSep);
    cardLayout->addWidget(labelCardTitle, 1);
    cardLayout->addWidget(labelCardStatus);

    auto* containerLayout = new QVBoxLayout();
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(4);

    ui.settingsLayout->setAlignment(ui.labelLiveName, Qt::AlignTop);
    ui.labelLiveName->setContentsMargins(0, 6, 0, 0);

    ui.settingsLayout->removeWidget(ui.lineEditLiveId);
    containerLayout->addWidget(ui.lineEditLiveId);
    containerLayout->addWidget(liveRoomInfoCard);
    ui.settingsLayout->addLayout(containerLayout, 1, 1, 1, 3);

    liveRoomInfoCard->setVisible(true);
}

void WindowMain::updateCurrentRoomCard()
{
    if (!liveRoomInfoCard) return;

    const QString text = ui.lineEditLiveId->currentText().trimmed();
    const auto curPlatformEnum = comboBoxToLivePlatform(ui.comboBox->currentIndex());
    const int curPlatform = static_cast<int>(curPlatformEnum);
    const std::string rawStr = text.toStdString();
    const std::string cleanId = (curPlatformEnum == LivePlatform::BiliBili)
        ? NormalizeBiliRoomID(rawStr)
        : NormalizeDouyinRoomID(rawStr);
    const std::string queryId = cleanId.empty() ? rawStr : cleanId;

    nlohmann::json matchedItem;
    bool found = false;

    if (!queryId.empty() && userinfo.is_object() &&
        userinfo.contains("live_room_items") && userinfo["live_room_items"].is_array())
    {
        for (const auto& item : userinfo["live_room_items"])
        {
            if (item.is_object() &&
                item.value("room_id", "") == queryId &&
                item.value("platform", 0) == curPlatform)
            {
                matchedItem = item;
                found = true;
                break;
            }
        }
    }

    if (!found)
    {
        // 占位态：卡片常驻占位，保持窗口几何尺寸稳定，绝不上下缩放抖动
        labelCardPlatform->setVisible(false);
        labelCardSep->setVisible(false);
        labelCardTitle->setVisible(false);
        labelCardStatus->setVisible(false);

        // 占位圆底图标
        const int avatarSize = 22;
        QPixmap ph(avatarSize * 2, avatarSize * 2);
        ph.fill(Qt::transparent);
        {
            QPainter p(&ph);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor("#e2e8f0"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(0, 0, avatarSize * 2, avatarSize * 2);
        }
        labelCardAvatar->setPixmap(ph.scaled(avatarSize, avatarSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        labelCardAvatar->setVisible(true);

        labelCardAnchor->setText(text.isEmpty() ? QStringLiteral("未选择直播间") : QStringLiteral("暂无该直播间信息"));
        labelCardAnchor->setStyleSheet("color: #94a3b8; font-size: 11px; font-weight: normal;");
        labelCardAnchor->setVisible(true);

        if (!t2.isRunning() && !ui.pBtStream->isChecked())
        {
            ui.pBtStream->setEnabled(true);
            ui.pBtStream->setToolTip(QString());
        }
        liveRoomInfoCard->setVisible(true);
        return;
    }

    const QString anchorName = QString::fromStdString(matchedItem.value("anchor_name", ""));
    const QString avatarUrl = QString::fromStdString(matchedItem.value("avatar_url", ""));
    const QString title = QString::fromStdString(matchedItem.value("title", ""));
    const bool isLive = matchedItem.value("is_live", false);
    const int platform = matchedItem.value("platform", 0);

    // 平台徽标
    const bool isBili = (platform == static_cast<int>(LivePlatform::BiliBili));
    if (isBili)
    {
        labelCardPlatform->setText(QStringLiteral("bilibili"));
        labelCardPlatform->setStyleSheet("background: #e5f4fb; color: #00aeec; padding: 1px 5px; border-radius: 3px; font-size: 11px; font-weight: bold;");
    }
    else
    {
        labelCardPlatform->setText(QStringLiteral("抖音"));
        labelCardPlatform->setStyleSheet("background: #fee7eb; color: #fe2c55; padding: 1px 5px; border-radius: 3px; font-size: 11px; font-weight: bold;");
    }
    labelCardPlatform->setVisible(true);

    // 主播名称
    labelCardAnchor->setText(anchorName.isEmpty() ? QStringLiteral("直播间 %1").arg(QString::fromStdString(matchedItem.value("room_id", ""))) : anchorName);
    labelCardAnchor->setStyleSheet("font-weight: bold; font-size: 12px; color: #1e293b;");
    labelCardAnchor->setVisible(true);

    // 分隔符与标题
    labelCardSep->setVisible(true);
    QFontMetrics fmTitle(labelCardTitle->font());
    labelCardTitle->setText(title.isEmpty() ? QStringLiteral("暂无标题") : fmTitle.elidedText(title, Qt::ElideRight, 260));
    labelCardTitle->setVisible(true);

    // 开播状态
    if (isLive)
    {
        labelCardStatus->setText(QStringLiteral("● 直播中"));
        labelCardStatus->setStyleSheet("background: #dcfce7; color: #16a34a; font-weight: bold; padding: 1px 6px; border-radius: 3px; font-size: 11px;");
    }
    else
    {
        labelCardStatus->setText(QStringLiteral("○ 未开播"));
        labelCardStatus->setStyleSheet("background: #f1f5f9; color: #64748b; padding: 1px 6px; border-radius: 3px; font-size: 11px;");
    }
    labelCardStatus->setVisible(true);

    // 根据开播状态控制“监视直播间”按钮启用状态
    if (!t2.isRunning() && !ui.pBtStream->isChecked())
    {
        if (!isLive)
        {
            ui.pBtStream->setEnabled(false);
            ui.pBtStream->setToolTip(QStringLiteral("当前直播间未开播，无法开启监视"));
        }
        else
        {
            ui.pBtStream->setEnabled(true);
            ui.pBtStream->setToolTip(QString());
        }
    }

    // 头像处理（圆形微缩头像）
    const int avatarSize = 22;
    QPixmap avatarPix(avatarSize * 2, avatarSize * 2);
    avatarPix.fill(Qt::transparent);

    bool hasDiskImage = false;
    const QString cacheKey = QStringLiteral("%1_%2").arg(platform).arg(QString::fromStdString(queryId));
    const QString pngPath = QStringLiteral("Config/cache/avatar/") + cacheKey + QStringLiteral(".png");

    if (QFile::exists(pngPath))
    {
        QImage diskImg;
        if (!diskImg.load(pngPath))
        {
            cv::Mat mat = cv::imread(pngPath.toLocal8Bit().toStdString(), cv::IMREAD_COLOR);
            if (!mat.empty())
            {
                diskImg = QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_BGR888).copy();
            }
        }
        if (!diskImg.isNull())
        {
            avatarPix = QPixmap::fromImage(diskImg.scaled(avatarSize * 2, avatarSize * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            hasDiskImage = true;
        }
    }
    else if (!avatarUrl.isEmpty())
    {
        preloadAvatar(platform, QString::fromStdString(queryId), avatarUrl, [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                updateCurrentRoomCard();
            }, Qt::QueuedConnection);
        });
    }

    QPixmap rounded(avatarSize * 2, avatarSize * 2);
    rounded.fill(Qt::transparent);
    {
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addEllipse(0, 0, avatarSize * 2, avatarSize * 2);
        p.setClipPath(path);

        if (hasDiskImage)
        {
            p.drawPixmap(0, 0, avatarPix);
        }
        else
        {
            const bool isBili = (platform == static_cast<int>(LivePlatform::BiliBili));
            QColor bg = isBili ? QColor("#fb7299") : QColor("#242630");
            p.setBrush(bg);
            p.setPen(Qt::NoPen);
            p.drawRect(0, 0, avatarSize * 2, avatarSize * 2);
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setBold(true);
            f.setPixelSize(avatarSize);
            p.setFont(f);
            const QString ch = isBili ? QStringLiteral("B") : QStringLiteral("抖");
            p.drawText(rounded.rect(), Qt::AlignCenter, ch);
        }
    }
    labelCardAvatar->setPixmap(rounded.scaled(avatarSize, avatarSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    liveRoomInfoCard->setVisible(true);
}

void WindowMain::refreshLiveRoomIds()
{
    const QString currentText = ui.lineEditLiveId->currentText().trimmed();
    QString savedText;
    const int selectedPlatform = static_cast<int>(comboBoxToLivePlatform(ui.comboBox->currentIndex()));

    ui.lineEditLiveId->blockSignals(true);
    ui.lineEditLiveId->clear();

    if (!userinfo.is_object())
    {
        try { userinfo = nlohmann::json::parse(m_config->getConfig()); } catch (...) {}
    }


    // 兼容迁移老版本数据：仅在不存在 live_room_items 字段时执行一次性迁移
    bool configModified = false;
    if (!userinfo.contains("live_room_items") || !userinfo["live_room_items"].is_array())
    {
        userinfo["live_room_items"] = nlohmann::json::array();
        configModified = true;
        const int defaultPlatform = (ui.comboBox->currentIndex() == 1)
            ? static_cast<int>(LivePlatform::BiliBili)
            : static_cast<int>(LivePlatform::Douyin);

        std::vector<std::pair<std::string, int>> seedList;

        if (userinfo.contains("live_room_ids") && userinfo["live_room_ids"].is_array())
        {
            for (const auto& item : userinfo["live_room_ids"])
            {
                if (item.is_string())
                {
                    std::string raw = item.get<std::string>();
                    int p = defaultPlatform;
                    if (raw.find("bilibili.com") != std::string::npos) p = static_cast<int>(LivePlatform::BiliBili);
                    else if (raw.find("douyin.com") != std::string::npos) p = static_cast<int>(LivePlatform::Douyin);

                    std::string clean = (p == static_cast<int>(LivePlatform::BiliBili))
                        ? NormalizeBiliRoomID(raw)
                        : NormalizeDouyinRoomID(raw);
                    if (!clean.empty())
                    {
                        seedList.push_back({ clean, p });
                    }
                }
            }
        }

        if (seedList.empty() && userinfo.contains("live_room_id") && userinfo["live_room_id"].is_string())
        {
            std::string raw = userinfo["live_room_id"].get<std::string>();
            int p = defaultPlatform;
            if (raw.find("bilibili.com") != std::string::npos) p = static_cast<int>(LivePlatform::BiliBili);
            else if (raw.find("douyin.com") != std::string::npos) p = static_cast<int>(LivePlatform::Douyin);

            std::string clean = (p == static_cast<int>(LivePlatform::BiliBili))
                ? NormalizeBiliRoomID(raw)
                : NormalizeDouyinRoomID(raw);
            if (!clean.empty())
            {
                seedList.push_back({ clean, p });
            }
        }

        for (const auto& [cleanId, p] : seedList)
        {
            userinfo["live_room_items"].push_back({
                { "room_id", cleanId },
                { "platform", p },
                { "anchor_name", "" },
                { "avatar_url", "" },
                { "title", "" },
                { "is_live", false }
            });
        }
    }

    // 彻底清除旧版本遗留的 live_room_ids 字段，杜绝后续重复初始化
    if (userinfo.contains("live_room_ids"))
    {
        userinfo.erase("live_room_ids");
        configModified = true;
    }

    if (configModified)
    {
        m_config->updateConfig(userinfo.dump());
    }

    if (userinfo.contains("live_room_items") && userinfo["live_room_items"].is_array())
    {
        for (const auto& item : userinfo["live_room_items"])
        {
            if (!item.is_object())
            {
                continue;
            }
            const QString id = QString::fromStdString(item.value("room_id", "")).trimmed();
            if (id.isEmpty())
            {
                continue;
            }
            const int platform = item.value("platform", 0);
            if (platform != selectedPlatform)
            {
                continue;
            }
            const QString anchorName = QString::fromStdString(item.value("anchor_name", ""));
            const QString avatarUrl = QString::fromStdString(item.value("avatar_url", ""));
            const QString title = QString::fromStdString(item.value("title", ""));
            const bool isLive = item.value("is_live", false);

            ui.lineEditLiveId->addItem(id);
            const int newIndex = ui.lineEditLiveId->count() - 1;
            ui.lineEditLiveId->setItemData(newIndex, id, LiveRoomIdRole);
            ui.lineEditLiveId->setItemData(newIndex, platform, LiveRoomPlatformRole);
            ui.lineEditLiveId->setItemData(newIndex, anchorName, LiveRoomAnchorNameRole);
            ui.lineEditLiveId->setItemData(newIndex, avatarUrl, LiveRoomAvatarUrlRole);
            ui.lineEditLiveId->setItemData(newIndex, title, LiveRoomTitleRole);
            ui.lineEditLiveId->setItemData(newIndex, isLive, LiveRoomIsLiveRole);

            if (!avatarUrl.isEmpty())
            {
                preloadAvatar(platform, id, avatarUrl, [this]() {
                    QMetaObject::invokeMethod(this, [this]() {
                        if (ui.lineEditLiveId->view() && ui.lineEditLiveId->view()->viewport())
                        {
                            ui.lineEditLiveId->view()->viewport()->update();
                        }
                        updateCurrentRoomCard();
                    }, Qt::QueuedConnection);
                });
            }
        }
    }

    if (userinfo.contains("live_room_id") && userinfo["live_room_id"].is_string() &&
        userinfo.value("live_room_platform", selectedPlatform) == selectedPlatform)
    {
        savedText = QString::fromStdString(userinfo["live_room_id"].get<std::string>()).trimmed();
    }

    bool currentTextIsVisible = false;
    for (int i = 0; i < ui.lineEditLiveId->count(); ++i)
    {
        if (ui.lineEditLiveId->itemData(i, LiveRoomIdRole).toString() == currentText)
        {
            currentTextIsVisible = true;
            break;
        }
    }

    if (currentTextIsVisible)
    {
        ui.lineEditLiveId->setCurrentText(currentText);
    }
    else if (!savedText.isEmpty())
    {
        for (int i = 0; i < ui.lineEditLiveId->count(); ++i)
        {
            if (ui.lineEditLiveId->itemData(i, LiveRoomIdRole).toString() == savedText)
            {
                ui.lineEditLiveId->setCurrentText(savedText);
                break;
            }
        }
    }

    ui.lineEditLiveId->blockSignals(false);
    updateCurrentRoomCard();
}

void WindowMain::saveLiveRoomId(const QString& roomId, const LiveStreamInfo* fetchedInfo)
{
    const QString text = roomId.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    try
    {
        const auto platformEnum = comboBoxToLivePlatform(ui.comboBox->currentIndex());
        const int platform = static_cast<int>(platformEnum);
        const std::string rawStr = text.toStdString();
        const std::string cleanId = (platformEnum == LivePlatform::BiliBili)
            ? NormalizeBiliRoomID(rawStr)
            : NormalizeDouyinRoomID(rawStr);

        // 如果清洗出的房间号为空，说明是无效链接/无房间号链接，坚决不保存
        if (cleanId.empty())
        {
            return;
        }

        if (!userinfo.is_object())
        {
            userinfo = nlohmann::json::parse(m_config->getConfig());
        }

        userinfo["live_room_id"] = cleanId;
        userinfo["live_room_platform"] = platform;

        if (!userinfo.contains("live_room_items") || !userinfo["live_room_items"].is_array())
        {
            userinfo["live_room_items"] = nlohmann::json::array();
        }

        auto& items = userinfo["live_room_items"];
        nlohmann::json targetItem;
        bool found = false;

        for (auto it = items.begin(); it != items.end();)
        {
            if (it->is_object() && it->value("room_id", "") == cleanId && it->value("platform", 0) == platform)
            {
                targetItem = *it;
                found = true;
                it = items.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // 手动输入的新直播间（历史记录中原本不存在）：
        // 若检测到没有开播（或未经过开播检测），不要记录到历史列表中；
        // 如果是下拉选择的（found == true），则不受影响，正常保留并更新状态。
        if (!found)
        {
            if (!fetchedInfo || fetchedInfo->status != LiveStreamStatus::Normal)
            {
                m_config->updateConfig(userinfo.dump());
                return;
            }

            targetItem["room_id"] = cleanId;
            targetItem["platform"] = platform;
            targetItem["anchor_name"] = "";
            targetItem["avatar_url"] = "";
            targetItem["title"] = "";
            targetItem["is_live"] = true;
        }

        if (fetchedInfo)
        {
            if (!fetchedInfo->anchorName.empty())
                targetItem["anchor_name"] = fetchedInfo->anchorName;
            if (!fetchedInfo->avatarUrl.empty())
                targetItem["avatar_url"] = fetchedInfo->avatarUrl;
            if (!fetchedInfo->title.empty())
                targetItem["title"] = fetchedInfo->title;
            targetItem["is_live"] = (fetchedInfo->status == LiveStreamStatus::Normal);
        }

        items.insert(items.begin(), targetItem);

        m_config->updateConfig(userinfo.dump());
        refreshLiveRoomIds();

        // 若无完整主播信息，触发后台异步刷新
        if (!fetchedInfo || fetchedInfo->anchorName.empty())
        {
            startLiveRoomStatusCheck();
        }
    }
    catch (const std::exception&)
    {
        QMessageBox::information(this, "错误", "保存直播间失败：配置文件异常", QMessageBox::Yes);
    }
}

void WindowMain::removeLiveRoomHistory(int row)
{
    if (!userinfo.is_object() || !userinfo.contains("live_room_items") || !userinfo["live_room_items"].is_array())
    {
        return;
    }
    auto& items = userinfo["live_room_items"];
    if (row >= 0 && row < ui.lineEditLiveId->count())
    {
        const QString removedId = ui.lineEditLiveId->itemData(row, LiveRoomIdRole).toString();
        const int removedPlatform = ui.lineEditLiveId->itemData(row, LiveRoomPlatformRole).toInt();
        auto it = std::find_if(items.begin(), items.end(), [&](const nlohmann::json& item) {
            return item.is_object() &&
                QString::fromStdString(item.value("room_id", "")) == removedId &&
                item.value("platform", 0) == removedPlatform;
        });
        if (it == items.end())
        {
            return;
        }
        items.erase(it);
        m_config->updateConfig(userinfo.dump());

        const QString curText = ui.lineEditLiveId->currentText().trimmed();
        if (curText == removedId)
        {
            ui.lineEditLiveId->clearEditText();
        }

        refreshLiveRoomIds();
        if (ui.lineEditLiveId->view() && ui.lineEditLiveId->view()->viewport())
        {
            ui.lineEditLiveId->view()->viewport()->update();
        }
    }
}

void WindowMain::startLiveRoomStatusCheck()
{
    if (!userinfo.is_object() || !userinfo.contains("live_room_items") || !userinfo["live_room_items"].is_array())
    {
        return;
    }

    struct CheckTarget {
        std::string roomId;
        LivePlatform platform;
    };
    std::vector<CheckTarget> targets;
    for (const auto& item : userinfo["live_room_items"])
    {
        if (item.is_object())
        {
            std::string rid = item.value("room_id", "");
            int p = item.value("platform", 0);
            if (!rid.empty() && (p == static_cast<int>(LivePlatform::Douyin) || p == static_cast<int>(LivePlatform::BiliBili)))
            {
                targets.push_back({ rid, static_cast<LivePlatform>(p) });
                if (targets.size() >= 20) break;
            }
        }
    }

    if (targets.empty())
    {
        return;
    }

    backgroundTasks.start(MakeScannerTask("history: check room status", [this, targets = std::move(targets)]() {
        std::vector<std::pair<CheckTarget, LiveStreamInfo>> results;

        for (const auto& target : targets)
        {
            if (closing.load()) return;
            auto detail = FetchLiveRoomDetail(target.platform, target.roomId);
            results.push_back({ target, detail });
        }

        if (closing.load()) return;

        QMetaObject::invokeMethod(this, [this, results = std::move(results)]() {
            if (closing.load() || !userinfo.contains("live_room_items") || !userinfo["live_room_items"].is_array())
            {
                return;
            }
            auto& items = userinfo["live_room_items"];
            bool anyUpdate = false;
            for (const auto& [target, info] : results)
            {
                for (auto& item : items)
                {
                    if (item.is_object() &&
                        item.value("room_id", "") == target.roomId &&
                        item.value("platform", 0) == static_cast<int>(target.platform))
                    {
                        if (!info.anchorName.empty() && item.value("anchor_name", "") != info.anchorName)
                        {
                            item["anchor_name"] = info.anchorName;
                            anyUpdate = true;
                        }
                        if (!info.avatarUrl.empty() && item.value("avatar_url", "") != info.avatarUrl)
                        {
                            item["avatar_url"] = info.avatarUrl;
                            anyUpdate = true;
                        }
                        if (!info.title.empty() && item.value("title", "") != info.title)
                        {
                            item["title"] = info.title;
                            anyUpdate = true;
                        }
                        bool newLive = (info.status == LiveStreamStatus::Normal);
                        if (item.value("is_live", false) != newLive)
                        {
                            item["is_live"] = newLive;
                            anyUpdate = true;
                        }
                        break;
                    }
                }
            }
            if (anyUpdate)
            {
                m_config->updateConfig(userinfo.dump());
                refreshLiveRoomIds();
            }
        }, Qt::QueuedConnection);
    }));
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
            std::string(accountProviderId(accountPlatform(account))),
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
                if (type == "kuro")
                {
                    const auto response = KuroApi(token).checkToken();
                    valid = response.success();
                    status = valid ? "有效" : (response.code == 220 ? "失效" : "检测失败");
                }
                else if (type != "mhy")
                {
                    status = "不支持";
                    WriteScannerLog(phase + " unsupported account type; skipped");
                }
                else if (!uidLooksValid)
                {
                    status = "UID异常";
                }
                else if (type == "mhy")
                {
                    valid = !token.empty() && !mid.empty() && CheckStokenValid(token, mid);
                    status = valid ? "有效" : "失效";
                }
            });
            WriteScannerLog(phase + " complete checked=" + std::to_string(checked) + " valid=" + std::to_string(valid));

            QMetaObject::invokeMethod(self.data(), [self, row, valid, status, token]() {
                if (self && !self->closing.load())
                {
                    if (row >= self->userinfo["account"].size() ||
                        self->userinfo["account"][row].value("access_key", std::string{}) != token) return;
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
    item[0]->setTextAlignment(Qt::AlignCenter);
    ui.tableWidget->setItem(nCount, 0, item[0]);

    item[1] = new QTableWidgetItem(uid.startsWith("kuro:") ? QStringLiteral("未返回社区UID") : uid);
    QFont fontMono("Consolas", 9);
    item[1]->setFont(fontMono);
    ui.tableWidget->setItem(nCount, 1, item[1]);

    item[2] = new QTableWidgetItem(userName);
    QFont fontName = item[2]->font();
    fontName.setBold(true);
    item[2]->setFont(fontName);
    ui.tableWidget->setItem(nCount, 2, item[2]);

    item[3] = new QTableWidgetItem(type);
    item[3]->setTextAlignment(Qt::AlignCenter);
    if (accountPlatform({}, type.toStdString()) == AccountPlatform::Kuro)
    {
        item[3]->setForeground(QColor(2, 132, 199));
    }
    else
    {
        item[3]->setForeground(QColor(8, 126, 114));
    }
    ui.tableWidget->setItem(nCount, 3, item[3]);

    item[4] = new QTableWidgetItem("未检测");
    item[4]->setTextAlignment(Qt::AlignCenter);
    ui.tableWidget->setItem(nCount, 4, item[4]);

    item[5] = new QTableWidgetItem(notes);
    ui.tableWidget->setItem(nCount, 5, item[5]);

    for (int i = 0; i < 6; i++)
    {
        if (item[i])
            item[i]->setTextAlignment(Qt::AlignCenter);
    }

    for (int i = 0; i < 5; i++)
    {
        QTableWidgetItem* item1 = ui.tableWidget->item(nCount, i);
        item1->setFlags(item1->flags() & ~Qt::ItemIsEditable);
    }
}

void WindowMain::AddAccount()
{
    if (accountControlsBusy())
    {
        QMessageBox::information(this, "错误", "请先停止识别！", QMessageBox::Yes);
        return;
    }
    if (accountLoginDialog)
    {
        accountLoginDialog->show();
        accountLoginDialog->raise();
        accountLoginDialog->activateWindow();
        return;
    }
    if (currentPlatformFilter == "kuro")
    {
        windowLoginKuro = new WindowLoginKuro(this);
        accountLoginDialog = windowLoginKuro;
        connect(windowLoginKuro, &WindowLoginKuro::AddUserInfo, this, &WindowMain::onAccountAdded);
        windowLoginKuro->show();
    }
    else
    {
        windowLogin = new WindowLogin(this);
        accountLoginDialog = windowLogin;
        windowLogin->setAttribute(Qt::WA_DeleteOnClose);
        connect(windowLogin, &WindowLogin::AddUserInfo, this, &WindowMain::onAccountAdded);
        windowLogin->show();
    }
}

void WindowMain::reauthenticateAccount(int row)
{
    if (closing.load() || row < 0 || row >= userinfo["account"].size()) return;
    if (accountControlsBusy())
    {
        QMessageBox::information(this, "提示", "请先停止扫描或等待当前请求结束。");
        return;
    }
    if (accountLoginDialog)
    {
        accountLoginDialog->raise();
        accountLoginDialog->activateWindow();
        return;
    }
    const auto& account = userinfo["account"][row];
    const auto provider = accountPlatform(account);
    const auto uid = account.value("uid", std::string{});
    if (provider == AccountPlatform::Unsupported) return;
    if (uid.empty() || uid.starts_with("kuro:"))
    {
        QMessageBox::information(this, "提示", "原账号缺少可核对的 UID，请通过添加账号重新登录。");
        return;
    }
    // Resolve the saved identity again on completion; row indexes can change while login is pending.
    const nlohmann::json identity = {{"account_id", account.value("account_id", std::string{})},
        {"uid", uid}, {"provider", accountProviderId(provider)}};
    const auto complete = [this, identity](const std::string& name, const std::string& token,
        const std::string& uid, const std::string& mid, const std::string& type) {
        if (!accountLoginDialog || sender() != accountLoginDialog.data() || !accountLoginDialog->isVisible()) return;
        onAccountReauthenticated(identity, name, token, uid, mid, type);
    };
    if (provider == AccountPlatform::Kuro)
    {
        auto* login = new WindowLoginKuro(this);
        login->setReauthentication(QString::fromStdString(uid), QString::fromStdString(account.value("name", std::string{})));
        connect(login, &WindowLoginKuro::AddUserInfo, this, complete);
        accountLoginDialog = login;
    }
    else
    {
        auto* login = new WindowLogin(this);
        login->setAttribute(Qt::WA_DeleteOnClose);
        login->setWindowTitle(QStringLiteral("重新登录米哈游账号 · UID %1").arg(QString::fromStdString(uid)));
        connect(login, &WindowLogin::AddUserInfo, this, complete);
        accountLoginDialog = login;
    }
    accountLoginDialog->show();
}

void WindowMain::onAccountReauthenticated(const nlohmann::json& identity, const std::string& name,
    const std::string& token, const std::string& uid, const std::string& mid, const std::string& type)
{
    if (closing.load()) return;
    if (accountControlsBusy())
    {
        QMessageBox::information(this, "提示", "扫描期间无法更新账号，请停止扫描后重新登录。");
        return;
    }
    const auto provider = accountPlatform(identity);
    if (accountPlatform({}, type) != provider || uid != identity.value("uid", std::string{}) || token.empty())
    {
        WriteScannerLog("account-reauth: identity mismatch; original account retained");
        QMessageBox::information(this, "提示", "登录账号与原账号不一致，未更新原账号。请使用原账号重新登录。");
        return;
    }
    auto& accounts = userinfo["account"];
    const auto id = identity.value("account_id", std::string{});
    const auto found = std::find_if(accounts.begin(), accounts.end(), [&](const auto& account) {
        return accountPlatform(account) == provider && account.value("uid", std::string{}) == uid &&
            (id.empty() || account.value("account_id", std::string{}) == id);
    });
    if (found == accounts.end())
    {
        QMessageBox::information(this, "提示", "原账号已移除，未保存本次登录结果。");
        return;
    }
    const auto row = static_cast<int>(std::distance(accounts.begin(), found));
    (*found)["access_key"] = token;
    (*found)["mid"] = mid;
    (*found)["name"] = name;
    (*found)["type"] = type;
    if (!saveUserInfo()) return;
    {
        const QSignalBlocker blocker(ui.tableWidget);
        ui.tableWidget->item(row, 2)->setText(QString::fromStdString(name));
        ui.tableWidget->item(row, 3)->setText(QString::fromStdString(type));
        setAccountCheckStatus(row, true, QStringLiteral("有效"));
    }
    applyPlatformFilter();
    WriteScannerLog("account-reauth: credentials updated");
    QMessageBox::information(this, "提示", "重新登录成功，已更新原账号。");
}

void WindowMain::onAccountAdded(const std::string& name, const std::string& token, const std::string& uid, const std::string& mid, const std::string& type)
{
    const auto provider = accountPlatform({}, type);
    const auto duplicate = std::any_of(userinfo["account"].begin(), userinfo["account"].end(), [&](const auto& account) {
        return accountPlatform(account) == provider &&
            (account.value("access_key", std::string{}) == token || account.value("uid", std::string{}) == uid);
    });
    if (duplicate)
    {
        QMessageBox::information(this, "提示", "该账号已添加，无需重复添加", QMessageBox::Yes);
        return;
    }
    const bool saved = RunScannerTask("account-save", [&] {
        int num{ userinfo["num"] };
        userinfo["account"][num]["access_key"] = token;
        userinfo["account"][num]["uid"] = uid;
        userinfo["account"][num]["name"] = name;
        userinfo["account"][num]["type"] = type;
        userinfo["account"][num]["note"] = "";
        userinfo["account"][num]["mid"] = mid;
        userinfo["account"][num]["provider"] = accountProviderId(provider);
        userinfo["account"][num]["account_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        if (provider == AccountPlatform::Kuro && !uid.starts_with("kuro:"))
            userinfo["account"][num]["community_uid"] = uid;
        userinfo["num"] = num + 1;
        m_config->updateConfig(userinfo.dump());
    });
    if (saved)
    {
        insertTableItems(QString::fromStdString(uid), QString::fromStdString(name), QString::fromStdString(type), "");
        applyPlatformFilter();
        countA = ui.tableWidget->rowCount() - 1;
        ui.tableWidget->setCurrentCell(countA, 0);
        ui.lineEditUname->setText(QString::fromStdString(name));
    }
    else userinfo = nlohmann::json::parse(m_config->getConfig());
    QMessageBox::information(this, "提示", saved ? "添加成功" : "账号保存失败，请查看日志", QMessageBox::Yes);
}

void WindowMain::pBtstartScreen(bool clicked)
{
    if (closing.load()) return;
    if (confirmationJob.isRunning()) return;
    if (!clicked) { pBtStop(); return; }
    if (confirmationDelay.pending())
    {
        pBtStop();
        return;
    }
    ui.pBtstartScreen->setEnabled(false);
    ui.pBtStream->setEnabled(false);
    ui.pBtstartScreen->setText("加载中。。。");
    const auto generation = ++scanGeneration;
    startupActive = clicked;
    const int selectedRow = countA;
    nlohmann::json account;
    if (!RunScannerTask("screen: account snapshot", [&] {
        if (clicked && selectedRow >= 0) account = userinfo.at("account").at(selectedRow);
    }, [this] { Q_EMIT BackgroundError(); })) return;
    backgroundTasks.start(MakeScannerTask("screen: startup", [this, clicked, selectedRow, generation, account = std::move(account)]() {
        WriteScannerLog("screen: startup begin");
        //FIXME 没有及时更新
        if (selectedRow == -1)
        {
            dispatchStartup(generation, [this] { emit AccountNotSelected(); });
            return;
        }
        const auto provider = accountPlatform(account);
        if (provider == AccountPlatform::Kuro)
        {
            const std::string token = account.at("access_key");
            if (!KuroApi(token).checkToken().success()) { dispatchStartup(generation, [this] { emit AccountError(); }); return; }
            if (closing.load() || generation != scanGeneration.load()) return;
            t1.wait();
        }
        else if (provider == AccountPlatform::Mhy)
        {
            std::string stoken = account.at("access_key");
            std::string uid = account.at("uid");
            std::string mid = account.at("mid");
            if (!CheckStokenValid(stoken, mid))
            {
                dispatchStartup(generation, [this] { emit AccountError(); });
                return;
            }
            if (closing.load() || generation != scanGeneration.load()) return;
            t1.wait();
        }
        else
        {
            WriteScannerLog("screen: unsupported account type; startup rejected");
            dispatchStartup(generation, [this] { emit AccountUnsupported(); });
            return;
        }
        if (closing.load() || generation != scanGeneration.load()) return;
        dispatchStartup(generation, [this, generation, account, provider]() {
            t1.resultGeneration = generation;
            if (provider == AccountPlatform::Kuro)
                t1.setKuroLoginInfo(account.value("uid", std::string{}), account.at("access_key"));
            else t1.setLoginInfo1(account.at("uid"), account.at("access_key"), account.at("mid"));
            t1.start();
            emit StartScanScreen();
        });
    }, [this, generation] { dispatchStartup(generation, [this] { Q_EMIT BackgroundError(); }); }));
}

void WindowMain::pBtStream(bool clicked)
{
    if (closing.load()) return;
    if (confirmationJob.isRunning()) return;
    if (!clicked) { pBtStop(); return; }
    if (confirmationDelay.pending())
    {
        pBtStop();
        return;
    }

    const auto platform = comboBoxToLivePlatform(ui.comboBox->currentIndex());
    const QString roomIdText = ui.lineEditLiveId->currentText().trimmed();
    if (roomIdText.isEmpty())
    {
        ui.pBtStream->setChecked(false);
        QMessageBox::information(this, "提示", "请输入或选择直播间！", QMessageBox::Yes);
        return;
    }

    // 校验若当前直播间在记录中且状态为未开播，禁止开启监视
    const int curPlatform = static_cast<int>(platform);
    const std::string rawStr = roomIdText.toStdString();
    const std::string cleanId = (platform == LivePlatform::BiliBili)
        ? NormalizeBiliRoomID(rawStr)
        : NormalizeDouyinRoomID(rawStr);
    const std::string queryId = cleanId.empty() ? rawStr : cleanId;

    if (userinfo.is_object() && userinfo.contains("live_room_items") && userinfo["live_room_items"].is_array())
    {
        for (const auto& item : userinfo["live_room_items"])
        {
            if (item.is_object() &&
                item.value("room_id", "") == queryId &&
                item.value("platform", 0) == curPlatform)
            {
                if (!item.value("is_live", false))
                {
                    ui.pBtStream->setChecked(false);
                    ui.pBtStream->setEnabled(false);
                    ui.pBtStream->setToolTip(QStringLiteral("当前直播间未开播，无法开启监视"));
                    QMessageBox::information(this, "提示", "当前直播间处于未开播状态，无法开启监视！", QMessageBox::Yes);
                    return;
                }
                break;
            }
        }
    }

    ui.pBtstartScreen->setEnabled(false);
    ui.pBtStream->setEnabled(false);
    ui.pBtStream->setText("加载中。。。");
    const auto generation = ++scanGeneration;
    startupActive = clicked;
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
    backgroundTasks.start(MakeScannerTask("stream: startup", [this, clicked, platform, roomId, selectedRow, generation, account = std::move(account)]() {
        WriteScannerLog("stream: startup begin");
        if (selectedRow == -1)
        {
            dispatchStartup(generation, [this] { emit AccountNotSelected(); });
            return;
        }
        const auto provider = accountPlatform(account);
        if (provider == AccountPlatform::Unsupported)
        {
            WriteScannerLog("stream: unsupported account type; startup rejected");
            dispatchStartup(generation, [this] { emit AccountUnsupported(); });
            return;
        }
        std::string stream_link;
        std::map<std::string, std::string> heards;
        //检查直播间状态
        if (!GetStreamLink(platform, roomId, stream_link, heards, generation))
        {
            dispatchStartup(generation, [this] { emit StopScanner(); });
            return;
        }
        else
        {
            t2.wait();
            if (closing.load() || generation != scanGeneration.load()) return;
        }
        {
            std::string stoken = account.at("access_key");
            std::string uid = account.at("uid");
            std::string mid = account.at("mid");
            if (!(provider == AccountPlatform::Kuro ? KuroApi(stoken).checkToken().success() : CheckStokenValid(stoken, mid)))
            {
                dispatchStartup(generation, [this] { emit AccountError(); });
                return;
            }
            if (closing.load() || generation != scanGeneration.load()) return;
        }
        if (closing.load() || generation != scanGeneration.load()) return;
        dispatchStartup(generation, [this, generation, account, provider, stream_link, heards]() {
            t2.resultGeneration = generation;
            t2.setUrl(stream_link, heards);
            if (provider == AccountPlatform::Kuro)
                t2.setKuroLoginInfo(account.value("uid", std::string{}), account.at("access_key"));
            else t2.setLoginInfo1(account.at("uid"), account.at("access_key"), account.at("mid"));
            t2.start();
        });
    }, [this, generation] { dispatchStartup(generation, [this] { Q_EMIT BackgroundError(); }); }));
}

void WindowMain::closeEvent(QCloseEvent* event)
{
    closing.store(true);
    ++scanGeneration;
    t1.authorizationCancelled.store(true);
    t2.authorizationCancelled.store(true);
    t1.kuroSession.cancel();
    t2.kuroSession.cancel();
    backgroundTasks.clear();
    confirmationDelay.cancel();
    saveLiveRoomId(ui.lineEditLiveId->currentText());
    t1.stop();
    t2.stop();
}

void WindowMain::showEvent(QShowEvent* event)
{
}

bool WindowMain::eventFilter(QObject* watched, QEvent* event)
{
    if (ui.lineEditLiveId && ui.lineEditLiveId->view() &&
        watched == ui.lineEditLiveId->view()->viewport())
    {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
            {
                auto* view = ui.lineEditLiveId->view();
                const QModelIndex index = view->indexAt(me->pos());
                if (index.isValid())
                {
                    const QRect rect = view->visualRect(index);
                    const int deleteBtnSize = 22;
                    const int deleteBtnRightMargin = 8;
                    const QRect deleteBtnRect(rect.right() - deleteBtnRightMargin - deleteBtnSize,
                                              rect.top() + (rect.height() - deleteBtnSize) / 2,
                                              deleteBtnSize, deleteBtnSize);
                    if (deleteBtnRect.contains(me->pos()))
                    {
                        if (event->type() == QEvent::MouseButtonRelease)
                        {
                            removeLiveRoomHistory(index.row());
                        }
                        return true; // 彻底拦截按压与释放事件，防止 QComboBox 选中并填入输入框！
                    }
                }
            }
        }
    }

    if (watched == ui.labelConfirmDelayHelp || watched == ui.labelScanIntervalHelp)
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseButtonPress)
        {
            auto* label = qobject_cast<QLabel*>(watched);
            if (label && !label->toolTip().isEmpty())
            {
                const QPoint pos = label->mapToGlobal(QPoint(label->width() / 2, label->height() + 4));
                QToolTip::showText(pos, label->toolTip(), label);
                return true;
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            QToolTip::hideText();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void WindowMain::islogin(const ScanRet ret)
{
    if (closing.load()) return;
    if (ret == ScanRet::KURO_SMS_REQUIRED)
    {
        showKuroSms(confirmationScreen);
        return;
    }
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
        Show_QMessageBox("提示", "扫码授权请求已接受，请查看游戏登录结果。");
        break;
    case ScanRet::ACCOUNT_EXPIRED:
        Show_QMessageBox("提示", "库街区登录凭据已失效，请重新添加账号。");
        break;
    case ScanRet::QR_EXPIRED:
        Show_QMessageBox("提示", "游戏二维码已过期，请刷新游戏二维码后重新扫描。");
        break;
    case ScanRet::REQUEST_UNKNOWN:
        Show_QMessageBox("提示", "请求结果未能确认，请先查看游戏状态，避免重复授权。详情见 Config/scanner.log。");
        break;
    case ScanRet::REQUEST_CANCELLED:
        Show_QMessageBox("提示", "当前授权已取消，请重新扫描。");
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
    case GameType::WutheringWaves:
        info += "\n登录鸣潮官服\n";
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
    if (messageBox->clickedButton() != yesButton)
    {
        pBtStop();
        return;
    }
    scheduleConfirmation(b);
}

void WindowMain::scheduleConfirmation(bool screen)
{
    // Stop capture without cancelling this QR's authorization generation.
    confirmationDelay.cancel();
    startupActive = false;
    t1.stop();
    t2.stop();
    confirmationScreen = screen;
    const auto generation = scanGeneration.load();
    ui.spinConfirmDelay->setEnabled(false);
    auto* button = screen ? ui.pBtstartScreen : ui.pBtStream;
    auto* other = screen ? ui.pBtStream : ui.pBtstartScreen;
    button->setText("取消确认");
    button->setChecked(true);
    other->setEnabled(false);
    const double seconds = ui.spinConfirmDelay->value();
    confirmationDelay.schedule(seconds, [this, screen, generation]() {
        if (closing.load() || generation != scanGeneration.load()) return;
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

bool WindowMain::accountControlsBusy() const
{
    return startupActive || t1.isRunning() || t2.isRunning() || confirmationJob.isRunning() ||
        confirmationDelay.pending() || !kuroSmsDialog.isNull();
}

bool WindowMain::saveUserInfo()
{
    try { m_config->updateConfig(userinfo.dump()); return true; }
    catch (...)
    {
        LogScannerException("config: save");
        userinfo = nlohmann::json::parse(m_config->getConfig());
        const QSignalBlocker delayBlock(ui.spinConfirmDelay), intervalBlock(ui.spinScanInterval);
        ui.spinConfirmDelay->setValue(userinfo.value("confirm_delay_seconds", 0.0));
        ui.spinScanInterval->setValue(userinfo.value("scan_interval_ms", 200));
        ui.checkBoxAutoExit->setChecked(userinfo.value("auto_exit", false));
        ui.checkBoxAutoLogin->setChecked(userinfo.value("auto_login", false));
        ui.checkBoxAutoScreen->setChecked(userinfo.value("auto_start", false));
        QMessageBox::information(this, "保存失败", "配置未能保存，原文件已保留。请检查磁盘空间及程序目录写入权限。详情见 Config/scanner.log。");
        return false;
    }
}

void WindowMain::dispatchStartup(quint64 generation, std::function<void()> action)
{
    QMetaObject::invokeMethod(this, [this, generation, action = std::move(action)] {
        if (closing.load() || generation != scanGeneration.load()) return;
        RunScannerTask("startup: UI completion", action, [this] { Q_EMIT BackgroundError(); });
    }, Qt::QueuedConnection);
}

void WindowMain::showKuroSms(bool screen)
{
    if (kuroSmsDialog || closing.load()) return;
    const auto generation = scanGeneration.load();
    const auto send = [this, screen, generation](std::string proof, std::function<void(KuroResponse)> complete) {
        if (closing.load() || generation != scanGeneration.load()) return;
        const auto deliver = [this, generation, complete](KuroResponse response) {
            QMetaObject::invokeMethod(this, [this, generation, complete, response = std::move(response)] {
                if (!closing.load() && generation == scanGeneration.load()) complete(response);
            }, Qt::QueuedConnection);
        };
        backgroundTasks.start(MakeScannerTask("kuro: scan SMS worker", [this, screen, generation, proof = std::move(proof), deliver] {
            if (closing.load() || generation != scanGeneration.load()) return;
            deliver((screen ? t1.kuroSession : t2.kuroSession).sendSms(proof, generation));
        }, [deliver] {
            KuroResponse response;
            response.error = KuroError::Network;
            deliver(response);
        }));
    };
    const auto confirm = [this, screen, generation](std::string sms, std::function<void(ScanRet)> complete) {
        if (closing.load() || generation != scanGeneration.load()) return;
        const auto deliver = [this, generation, complete](ScanRet response) {
            QMetaObject::invokeMethod(this, [this, generation, complete, response] {
                if (!closing.load() && generation == scanGeneration.load()) complete(response);
            }, Qt::QueuedConnection);
        };
        backgroundTasks.start(MakeScannerTask("kuro: SMS confirmation worker", [this, screen, generation, sms = std::move(sms), deliver] {
            if (closing.load() || generation != scanGeneration.load()) return;
            deliver((screen ? t1.kuroSession : t2.kuroSession).confirm(sms, generation));
        }, [deliver] { deliver(ScanRet::REQUEST_UNKNOWN); }));
    };
    auto* dialog = new KuroScanSmsDialog(send, confirm, [this, generation](ScanRet result) {
        if (closing.load() || generation != scanGeneration.load()) return;
        kuroSmsDialog = nullptr;
        islogin(result);
    }, this);
    kuroSmsDialog = dialog;
    connect(dialog, &QDialog::rejected, this, [this, guard = QPointer<QDialog>(dialog)] {
        if (kuroSmsDialog == guard) { kuroSmsDialog = nullptr; pBtStop(); }
    });
    dialog->open();
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
    saveUserInfo();
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
    saveUserInfo();
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
    saveUserInfo();
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

bool WindowMain::GetStreamLink(LivePlatform platform, const std::string& roomid, std::string& url, std::map<std::string, std::string>& heards, quint64 generation)
{
    auto info = GetLiveInfo(platform, roomid);
    QMetaObject::invokeMethod(this, [this, roomid, info]() {
        saveLiveRoomId(QString::fromStdString(roomid), &info);
    }, Qt::QueuedConnection);
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
        else if (platform == LivePlatform::Douyin)
        {
            heards["user_agent"] =
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                "Chrome/120.0.0.0 Safari/537.36";
            heards["headers"] =
                "Referer: https://live.douyin.com/\r\n";
        }
        return true;
    }
    else
    {
        dispatchStartup(generation, [this, status = info.status] { Q_EMIT LiveStreamLinkError(status); });
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
    messageBox->setText("账号校验未通过。\n请检查网络和 Config/scanner.log；凭据失效时可右键账号选择“重新登录”。");
    messageBox->setWindowTitle("提示");
    messageBox->setIcon(QMessageBox::Information);
    messageBox->show();
}

void WindowMain::getInfo(int x, int y)
{
    if (confirmationJob.isRunning()) { ui.tableWidget->setCurrentCell(countA, 0); return; }
    if (x != countA && accountControlsBusy()) pBtStop();
    QTableWidgetItem* item = ui.tableWidget->item(x, 2);
    if (!item) return;
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
        if (!saveUserInfo()) return;
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
    if (accountControlsBusy())
    {
        QMessageBox::information(this, "提示", "请先停止扫描或等待当前请求结束。");
        return;
    }
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

    if (!saveUserInfo()) return;
    //ui.tableWidget->setCurrentCell(nCurrentRow, QItemSelectionModel::Current);
    ui.tableWidget->removeRow(nCurrentRow);
    ui.tableWidget->clearSelection();
    ui.lineEditUname->setText("未选中");
    countA = -1;

    applyPlatformFilter();
}

void WindowMain::pBtStop()
{
    ++scanGeneration;
    startupActive = false;
    if (kuroSmsDialog)
    {
        auto* dialog = kuroSmsDialog.data();
        kuroSmsDialog = nullptr;
        dialog->reject();
    }
    confirmationDelay.cancel();
    ui.spinConfirmDelay->setEnabled(true);
    t1.stop();
    t2.stop();
    t1.kuroSession.cancel();
    t2.kuroSession.cancel();
    t1.authorizationCancelled.store(true);
    t2.authorizationCancelled.store(true);
    ui.pBtstartScreen->setText("监视屏幕");
    ui.pBtStream->setText("监视直播间");
    ui.pBtstartScreen->setChecked(false);
    ui.pBtStream->setChecked(false);
    ui.pBtstartScreen->setEnabled(true);
    ui.pBtStream->setEnabled(true);
    updateCurrentRoomCard();
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
        int scanInterval = userinfo.value("scan_interval_ms", 200);
        {
            const QSignalBlocker blocker(ui.spinScanInterval);
            ui.spinScanInterval->setValue(scanInterval);
        }
        t1.setScanInterval(scanInterval);
        t2.setScanInterval(scanInterval);
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
            if (accountPlatform(userinfo["account"].at(countA)) == AccountPlatform::Kuro)
            {
                currentPlatformFilter = "kuro";
                btnKuro->setChecked(true);
            }
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
        startLiveRoomStatusCheck();
        applyPlatformFilter();
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
    saveUserInfo();
}

void WindowMain::copyEntireRow(int row)
{
    QString rowData;
    if (accountPlatform(userinfo["account"][row]) == AccountPlatform::Kuro)
    {
        QApplication::clipboard()->setText(QString::fromStdString(userinfo["account"][row]["access_key"]));
        return;
    }
    if (accountPlatform(userinfo["account"][row]) != AccountPlatform::Mhy)
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

void WindowMain::initCornerSwitcher()
{
    auto* cornerWidget = new QWidget(this);
    cornerWidget->setObjectName("cornerWidget");
    cornerWidget->setStyleSheet("background: transparent;");

    auto* cornerLayout = new QHBoxLayout(cornerWidget);
    cornerLayout->setContentsMargins(0, 2, 12, 2);
    cornerLayout->setSpacing(8);

    pillContainer = new QFrame(cornerWidget);
    pillContainer->setObjectName("pillContainer");
    pillContainer->setStyleSheet(
        "#pillContainer { background: #e9ecef; border-radius: 13px; }"
        "QToolButton { border: none; border-radius: 11px; padding: 3px 12px; font-size: 12px; font-weight: bold; color: #5c6370; background: transparent; }"
        "QToolButton:hover { color: #1e2228; background: rgba(255, 255, 255, 0.45); }"
        "QToolButton:checked { background: #ffffff; color: #087e72; font-weight: bold; border: 1px solid rgba(0, 0, 0, 0.08); }"
    );

    auto* layout = new QHBoxLayout(pillContainer);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    btnMhy = new QToolButton(pillContainer);
    btnMhy->setText(QStringLiteral("米哈游"));
    btnMhy->setCheckable(true);
    btnMhy->setChecked(true);
    btnMhy->setCursor(Qt::PointingHandCursor);

    btnKuro = new QToolButton(pillContainer);
    btnKuro->setText(QStringLiteral("库街区"));
    btnKuro->setCheckable(true);
    btnKuro->setChecked(false);
    btnKuro->setCursor(Qt::PointingHandCursor);

    platformGroup = new QButtonGroup(this);
    platformGroup->setExclusive(true);
    platformGroup->addButton(btnMhy);
    platformGroup->addButton(btnKuro);

    layout->addWidget(btnMhy);
    layout->addWidget(btnKuro);

    btnAddAccount = new QToolButton(cornerWidget);
    btnAddAccount->setObjectName("btnAddAccount");
    btnAddAccount->setText("+");
    btnAddAccount->setToolTip(QStringLiteral("添加账号"));
    btnAddAccount->setCursor(Qt::PointingHandCursor);
    btnAddAccount->setStyleSheet(
        "QToolButton#btnAddAccount {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #1a73e8, stop:1 #009688);"
        "  color: #ffffff; border: none; border-radius: 7px;"
        "  font-size: 16px; font-weight: bold;"
        "  min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; padding: 0px;"
        "}"
        "QToolButton#btnAddAccount:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2b82f6, stop:1 #0db4a4);"
        "}"
        "QToolButton#btnAddAccount:pressed {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #125bc4, stop:1 #007368);"
        "}"
    );

    cornerLayout->addWidget(pillContainer);
    cornerLayout->addWidget(btnAddAccount);

    connect(btnAddAccount, &QToolButton::clicked, this, &WindowMain::AddAccount);

    connect(btnMhy, &QToolButton::clicked, this, [this]() {
        onPlatformChanged("mhy");
    });
    connect(btnKuro, &QToolButton::clicked, this, [this]() {
        onPlatformChanged("kuro");
    });

    ui.menuBar->setCornerWidget(cornerWidget, Qt::TopRightCorner);
}

void WindowMain::onPlatformChanged(const QString& platform)
{
    if (confirmationJob.isRunning())
    {
        (currentPlatformFilter == "kuro" ? btnKuro : btnMhy)->setChecked(true);
        return;
    }
    if (platform != currentPlatformFilter) pBtStop();
    currentPlatformFilter = platform;
    applyPlatformFilter();
}

void WindowMain::applyPlatformFilter()
{
    const QSignalBlocker blocker(ui.tableWidget);
    const bool isKuroMode = (currentPlatformFilter == "kuro");
    int firstVisibleRow = -1;
    int defaultRow = -1;
    int visibleNumber = 0;

    for (int i = 0; i < ui.tableWidget->rowCount(); ++i)
    {
        const bool isKuroAccount = i < userinfo["account"].size() &&
            accountPlatform(userinfo["account"][i]) == AccountPlatform::Kuro;
        bool shouldHide = isKuroMode ? !isKuroAccount : isKuroAccount;

        ui.tableWidget->setRowHidden(i, shouldHide);

        if (!shouldHide)
        {
            ui.tableWidget->item(i, 0)->setText(QString::number(++visibleNumber));
            if (firstVisibleRow == -1)
            {
                firstVisibleRow = i;
            }
            if (i == countA)
            {
                defaultRow = i;
            }
        }
    }

    int targetRow = (defaultRow != -1) ? defaultRow : firstVisibleRow;
    if (targetRow != -1)
    {
        countA = targetRow;
        ui.tableWidget->setCurrentCell(targetRow, 0);
        if (QTableWidgetItem* nameItem = ui.tableWidget->item(targetRow, 2))
        {
            ui.lineEditUname->setText(nameItem->text());
        }
    }
    else
    {
        countA = -1;
        ui.tableWidget->clearSelection();
        ui.lineEditUname->setText("未选中");
    }
}
