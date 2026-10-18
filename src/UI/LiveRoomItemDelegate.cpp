#include "LiveRoomItemDelegate.h"
#include "LiveStreamLink.h"
#include "ScannerLog.hpp"
#include <QPainterPath>
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QThreadPool>
#include <cpr/cpr.h>
#include <opencv2/opencv.hpp>
#include <format>

namespace {
const QString AvatarCacheDir = QStringLiteral("Config/cache/avatar/");
}

LiveRoomItemDelegate::LiveRoomItemDelegate(QObject* parent) :
    QStyledItemDelegate(parent)
{
    QDir().mkpath(AvatarCacheDir);
}

void LiveRoomItemDelegate::clearHover()
{
    m_hoverRow = -1;
    m_hoverDeleteBtn = false;
}

QSize LiveRoomItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(option.rect.width(), 54);
}

QPixmap LiveRoomItemDelegate::getAvatarPixmap(int platform, const QString& roomId, const QString& avatarUrl, const QString& fallbackChar) const
{
    const int avatarSize = 38;
    const QString cacheKey = QStringLiteral("%1_%2").arg(platform).arg(roomId);
    if (!roomId.isEmpty() && m_avatarCache.contains(cacheKey))
    {
        return m_avatarCache.value(cacheKey);
    }

    if (!roomId.isEmpty())
    {
        const QString pngPath = AvatarCacheDir + cacheKey + QStringLiteral(".png");

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
                QPixmap diskPix = QPixmap::fromImage(diskImg.scaled(avatarSize * 2, avatarSize * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                m_avatarCache.insert(cacheKey, diskPix);
                return diskPix;
            }
        }
        else if (!avatarUrl.isEmpty())
        {
            requestAvatarDownload(platform, roomId, avatarUrl);
        }
    }

    // 默认圆形占位头像
    QPixmap placeholder(avatarSize * 2, avatarSize * 2);
    placeholder.fill(Qt::transparent);
    {
        QPainter p(&placeholder);
        p.setRenderHint(QPainter::Antialiasing);
        const bool isBili = (platform == static_cast<int>(LivePlatform::BiliBili));
        QColor bg = isBili ? QColor("#fb7299") : QColor("#242630");
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, avatarSize * 2, avatarSize * 2);

        p.setPen(Qt::white);
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(avatarSize);
        p.setFont(f);
        QString letter = fallbackChar.trimmed();
        if (letter.isEmpty())
        {
            letter = isBili ? QStringLiteral("B") : QStringLiteral("抖");
        }
        else
        {
            letter = letter.left(1).toUpper();
        }
        p.drawText(placeholder.rect(), Qt::AlignCenter, letter);
    }
    return placeholder;
}

void LiveRoomItemDelegate::requestAvatarDownload(int platform, const QString& roomId, const QString& avatarUrl) const
{
    if (avatarUrl.isEmpty() || roomId.isEmpty())
    {
        return;
    }
    const QString cacheKey = QStringLiteral("%1_%2").arg(platform).arg(roomId);
    if (m_pendingDownloads.contains(cacheKey))
    {
        return;
    }
    m_pendingDownloads.insert(cacheKey);

    QPointer<LiveRoomItemDelegate> self(const_cast<LiveRoomItemDelegate*>(this));
    QThreadPool::globalInstance()->start([self, platform, roomId, avatarUrl, cacheKey]() {
        QDir().mkpath(AvatarCacheDir);
        const QString pngPath = AvatarCacheDir + cacheKey + QStringLiteral(".png");

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
            cpr::Timeout{ 8000 },
            cpr::ConnectTimeout{ 5000 },
            cpr::Header{
                { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36" },
                { "Referer", referer }
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
            else
            {
                WriteScannerLog(std::format("avatar: decode failed for url={}", reqUrl));
            }

            if (self)
            {
                QMetaObject::invokeMethod(self.data(), [self, cacheKey, pngPath]() {
                    if (!self) return;
                    self->m_pendingDownloads.remove(cacheKey);

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
                            const int avatarSize = 38;
                            QPixmap pix = QPixmap::fromImage(diskImg.scaled(avatarSize * 2, avatarSize * 2, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                            self->m_avatarCache.insert(cacheKey, pix);
                        }
                    }

                    if (auto* view = qobject_cast<QAbstractItemView*>(self->parent()))
                    {
                        if (view->viewport()) view->viewport()->update();
                    }
                    else if (auto* combo = qobject_cast<QComboBox*>(self->parent()))
                    {
                        if (combo->view() && combo->view()->viewport()) combo->view()->viewport()->update();
                    }
                }, Qt::QueuedConnection);
                return;
            }
        }
        else
        {
            WriteScannerLog(std::format("avatar: download failed code={}, err={}, url={}", r.status_code, r.error.message, reqUrl));
        }

        if (self)
        {
            QMetaObject::invokeMethod(self.data(), [self, cacheKey]() {
                if (!self) return;
                self->m_pendingDownloads.remove(cacheKey);
            }, Qt::QueuedConnection);
        }
    });
}

void LiveRoomItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    const QRect rect = option.rect;
    const bool isSelected = (option.state & QStyle::State_Selected);
    const bool isHovered = (index.row() == m_hoverRow);

    // 背景绘制
    QColor bgColor = Qt::white;
    if (isSelected)
    {
        bgColor = QColor("#e8f4f2");
    }
    else if (isHovered)
    {
        bgColor = QColor("#f4f7f6");
    }
    painter->fillRect(rect, bgColor);

    // 底部细分隔线
    painter->setPen(QColor("#edf0f2"));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());

    // 数据读取
    const QString roomId = index.data(LiveRoomIdRole).toString();
    const int platform = index.data(LiveRoomPlatformRole).toInt();
    const QString anchorName = index.data(LiveRoomAnchorNameRole).toString();
    const QString avatarUrl = index.data(LiveRoomAvatarUrlRole).toString();
    const QString title = index.data(LiveRoomTitleRole).toString();
    const bool isLive = index.data(LiveRoomIsLiveRole).toBool();

    const int avatarSize = 36;
    const int avatarLeft = rect.left() + 8;
    const int avatarTop = rect.top() + (rect.height() - avatarSize) / 2;
    const QRect avatarRect(avatarLeft, avatarTop, avatarSize, avatarSize);

    // 绘制圆形头像
    QPixmap avatarPix = getAvatarPixmap(platform, roomId, avatarUrl, anchorName.isEmpty() ? roomId : anchorName);
    QPainterPath avatarPath;
    avatarPath.addEllipse(avatarRect);
    painter->save();
    painter->setClipPath(avatarPath);
    painter->drawPixmap(avatarRect, avatarPix);
    painter->restore();

    // 头像边框
    painter->setPen(QPen(QColor(0, 0, 0, 15), 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(avatarRect);

    // 测量右侧删除按钮预留区
    const int deleteBtnSize = 22;
    const int deleteBtnRightMargin = 8;
    const QRect deleteBtnRect(rect.right() - deleteBtnRightMargin - deleteBtnSize,
                              rect.top() + (rect.height() - deleteBtnSize) / 2,
                              deleteBtnSize, deleteBtnSize);

    // 文本区域
    const int textLeft = avatarRect.right() + 10;
    const int textRight = deleteBtnRect.left() - 6;
    const int textWidth = qMax(20, textRight - textLeft);

    int currentX = textLeft;
    const int topRowY = rect.top() + 8;

    // 1. 平台徽章（bilibili / 抖音）
    const bool isBili = (platform == static_cast<int>(LivePlatform::BiliBili));
    const QString platformStr = isBili ? QStringLiteral("bilibili") : QStringLiteral("抖音");
    const QColor tagBg = isBili ? QColor("#e5f4fb") : QColor("#fee7eb");
    const QColor tagText = isBili ? QColor("#00aeec") : QColor("#fe2c55");

    QFont tagFont = painter->font();
    tagFont.setPixelSize(11);
    tagFont.setBold(true);
    painter->setFont(tagFont);

    QFontMetrics fmTag(tagFont);
    const int tagWidth = fmTag.horizontalAdvance(platformStr) + 8;
    const int tagHeight = 16;
    const QRect tagRect(currentX, topRowY + 1, tagWidth, tagHeight);

    painter->setBrush(tagBg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(tagRect, 3, 3);

    painter->setPen(tagText);
    painter->drawText(tagRect, Qt::AlignCenter, platformStr);
    currentX += tagWidth + 6;

    // 2. 主播名称
    QFont nameFont = painter->font();
    nameFont.setPixelSize(13);
    nameFont.setBold(true);
    painter->setFont(nameFont);

    QString displayName = anchorName.isEmpty() ? QStringLiteral("直播间 %1").arg(roomId) : anchorName;
    QFontMetrics fmName(nameFont);

    // 3. 开播状态标签预留计算
    const QString statusText = isLive ? QStringLiteral("● 直播中") : QStringLiteral("○ 未开播");
    const QColor statusBg = isLive ? QColor("#dcfce7") : QColor("#f1f5f9");
    const QColor statusTextColor = isLive ? QColor("#16a34a") : QColor("#64748b");

    QFont statusFont = painter->font();
    statusFont.setPixelSize(11);
    statusFont.setBold(false);
    QFontMetrics fmStatus(statusFont);
    const int statusWidth = fmStatus.horizontalAdvance(statusText) + 8;
    const int statusHeight = 16;

    // 主播名称最大可用宽度
    const int maxNameWidth = textRight - currentX - statusWidth - 8;
    const QString elidedName = fmName.elidedText(displayName, Qt::ElideRight, qMax(30, maxNameWidth));

    painter->setPen(QColor("#1f2937"));
    painter->drawText(QRect(currentX, topRowY, fmName.horizontalAdvance(elidedName), 18), Qt::AlignVCenter | Qt::AlignLeft, elidedName);
    currentX += fmName.horizontalAdvance(elidedName) + 6;

    // 绘制开播状态标签
    painter->setFont(statusFont);
    const QRect statusRect(currentX, topRowY + 1, statusWidth, statusHeight);
    painter->setBrush(statusBg);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(statusRect, 3, 3);
    painter->setPen(statusTextColor);
    painter->drawText(statusRect, Qt::AlignCenter, statusText);

    // 4. 第二行：房间号 · 直播间标题
    const int bottomRowY = rect.top() + 30;
    QFont subFont = painter->font();
    subFont.setPixelSize(11);
    subFont.setBold(false);
    painter->setFont(subFont);
    QFontMetrics fmSub(subFont);

    QString subText = QStringLiteral("ID: ") + roomId;
    if (!title.isEmpty())
    {
        subText += QStringLiteral(" · ") + title;
    }
    const QString elidedSubText = fmSub.elidedText(subText, Qt::ElideRight, textWidth);

    painter->setPen(QColor("#6b7280"));
    painter->drawText(QRect(textLeft, bottomRowY, textWidth, 16), Qt::AlignVCenter | Qt::AlignLeft, elidedSubText);

    // 5. 右侧删除按钮
    if (isHovered)
    {
        painter->setRenderHint(QPainter::Antialiasing);
        if (m_hoverDeleteBtn)
        {
            painter->setBrush(QColor("#fee2e2"));
            painter->setPen(QPen(QColor("#fca5a5"), 1));
            painter->drawEllipse(deleteBtnRect);

            painter->setPen(QPen(QColor("#dc2626"), 1.8));
        }
        else
        {
            painter->setBrush(QColor(0, 0, 0, 10));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(deleteBtnRect);

            painter->setPen(QPen(QColor("#94a3b8"), 1.4));
        }

        const int offset = 6;
        painter->drawLine(deleteBtnRect.left() + offset, deleteBtnRect.top() + offset,
                          deleteBtnRect.right() - offset, deleteBtnRect.bottom() - offset);
        painter->drawLine(deleteBtnRect.right() - offset, deleteBtnRect.top() + offset,
                          deleteBtnRect.left() + offset, deleteBtnRect.bottom() - offset);
    }

    painter->restore();
}

bool LiveRoomItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const int deleteBtnSize = 22;
    const int deleteBtnRightMargin = 8;
    const QRect deleteBtnRect(option.rect.right() - deleteBtnRightMargin - deleteBtnSize,
                              option.rect.top() + (option.rect.height() - deleteBtnSize) / 2,
                              deleteBtnSize, deleteBtnSize);

    if (event->type() == QEvent::MouseMove)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        const bool insideBtn = deleteBtnRect.contains(me->pos());
        if (m_hoverRow != index.row() || m_hoverDeleteBtn != insideBtn)
        {
            m_hoverRow = index.row();
            m_hoverDeleteBtn = insideBtn;
            if (auto* view = qobject_cast<QAbstractItemView*>(parent()))
            {
                view->viewport()->update();
            }
        }
    }
    else if (event->type() == QEvent::Leave)
    {
        clearHover();
        if (auto* view = qobject_cast<QAbstractItemView*>(parent()))
        {
            view->viewport()->update();
        }
    }
    else if (event->type() == QEvent::MouseButtonPress)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && deleteBtnRect.contains(me->pos()))
        {
            return true; // 拦截 Press 事件，避免触发 QComboBox 选中
        }
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton && deleteBtnRect.contains(me->pos()))
        {
            Q_EMIT deleteItemRequested(index.row());
            return true; // 拦截 Release 事件，避免触发 QComboBox 选中
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}
