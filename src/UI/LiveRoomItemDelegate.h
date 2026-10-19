#pragma once

#include <QStyledItemDelegate>
#include <QPainter>
#include <QMouseEvent>
#include <QMap>
#include <QSet>
#include <QPixmap>
#include <QPointer>

enum LiveRoomDataRole
{
    LiveRoomIdRole = Qt::UserRole,
    LiveRoomPlatformRole = Qt::UserRole + 1,
    LiveRoomAnchorNameRole = Qt::UserRole + 2,
    LiveRoomAvatarUrlRole = Qt::UserRole + 3,
    LiveRoomTitleRole = Qt::UserRole + 4,
    LiveRoomIsLiveRole = Qt::UserRole + 5
};

class LiveRoomItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit LiveRoomItemDelegate(QObject* parent = nullptr);
    ~LiveRoomItemDelegate() override = default;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    void clearHover();

Q_SIGNALS:
    void deleteItemRequested(int row);

private:
    QPixmap getAvatarPixmap(int platform, const QString& roomId, const QString& avatarUrl, const QString& fallbackChar) const;
    void requestAvatarDownload(int platform, const QString& roomId, const QString& avatarUrl) const;

    mutable QMap<QString, QPixmap> m_avatarCache;
    mutable QSet<QString> m_pendingDownloads;

    mutable int m_hoverRow{ -1 };
    mutable bool m_hoverDeleteBtn{ false };
};
