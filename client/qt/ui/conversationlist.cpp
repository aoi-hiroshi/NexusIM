#include "ui/conversationlist.hpp"

#include "models/conversationmodel.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace nexusim {
namespace client {
namespace {

class ConversationFilter final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const QModelIndex source = sourceModel()->index(row, 0, parent);
        const QString needle = filterRegExp().pattern().trimmed();
        if (needle.isEmpty()) return true;
        return source.data(ConversationModel::DisplayNameRole).toString()
                       .contains(needle, Qt::CaseInsensitive) ||
               QString::number(source.data(ConversationModel::UserIdRole).toInt())
                       .contains(needle);
    }
};

class ConversationDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(260, 72);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const QRect rect = option.rect.adjusted(6, 3, -6, -3);
        if (option.state & QStyle::State_Selected)
            painter->fillRect(rect, QColor(QStringLiteral("#e8f1ff")));
        else if (option.state & QStyle::State_MouseOver)
            painter->fillRect(rect, QColor(QStringLiteral("#f3f6fa")));

        const QString name = index.data(ConversationModel::DisplayNameRole).toString();
        const QString last = index.data(ConversationModel::LastMessageRole).toString();
        const int unread = index.data(ConversationModel::UnreadRole).toInt();
        const QDateTime timestamp = index.data(ConversationModel::TimestampRole).toDateTime();
        const QRect avatar(rect.left() + 8, rect.top() + 12, 42, 42);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#4f86f7")));
        painter->drawEllipse(avatar);
        painter->setPen(Qt::white);
        QFont avatarFont = option.font;
        avatarFont.setBold(true);
        avatarFont.setPointSize(13);
        painter->setFont(avatarFont);
        painter->drawText(avatar, Qt::AlignCenter,
                          name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());

        QFont nameFont = option.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);
        painter->setPen(QColor(QStringLiteral("#172033")));
        painter->drawText(QRect(60, rect.top() + 11, rect.width() - 120, 22)
                              .translated(rect.left(), 0),
                          Qt::AlignVCenter, name);
        painter->setFont(option.font);
        painter->setPen(QColor(QStringLiteral("#7b8496")));
        painter->drawText(QRect(60, rect.top() + 35, rect.width() - 76, 20)
                              .translated(rect.left(), 0),
                          Qt::AlignVCenter, last);
        if (timestamp.isValid())
        {
            painter->drawText(QRect(rect.right() - 62, rect.top() + 11, 54, 18),
                              Qt::AlignRight | Qt::AlignVCenter,
                              timestamp.toString(QStringLiteral("HH:mm")));
        }
        if (unread > 0)
        {
            const QRect badge(rect.right() - 30, rect.bottom() - 27, 22, 18);
            painter->setBrush(QColor(QStringLiteral("#ef5b5b")));
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(badge, 9, 9);
            painter->setPen(Qt::white);
            painter->drawText(badge, Qt::AlignCenter,
                              unread > 99 ? QStringLiteral("99+") : QString::number(unread));
        }
        painter->restore();
    }
};

} // namespace

ConversationList::ConversationList(ConversationModel *model, QWidget *parent)
    : QWidget(parent), model_(model), proxy_(new ConversationFilter(this)),
      list_(new QListView(this)), searchEdit_(new QLineEdit(this)),
      userIdEdit_(new QLineEdit(this))
{
    setObjectName(QStringLiteral("middlePanel"));
    setMinimumWidth(270);
    setMaximumWidth(330);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(true);
    list_->setModel(proxy_);
    list_->setItemDelegate(new ConversationDelegate(list_));
    list_->setMouseTracking(true);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    searchEdit_->setPlaceholderText(QStringLiteral("搜索会话"));
    userIdEdit_->setPlaceholderText(QStringLiteral("输入用户 ID 发起聊天"));
    userIdEdit_->setValidator(new QIntValidator(1, 2147483647, userIdEdit_));
    auto *startButton = new QPushButton(QStringLiteral("开始"), this);
    startButton->setObjectName(QStringLiteral("compactButton"));

    auto *newRow = new QHBoxLayout;
    newRow->addWidget(userIdEdit_, 1);
    newRow->addWidget(startButton);
    auto *title = new QLabel(QStringLiteral("会话"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 18, 14, 12);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(searchEdit_);
    layout->addLayout(newRow);
    layout->addWidget(list_, 1);

    connect(searchEdit_, &QLineEdit::textChanged, proxy_,
            [this](const QString &text) {
                proxy_->setFilterRegExp(QRegExp(text, Qt::CaseInsensitive,
                                                QRegExp::FixedString));
            });
    connect(startButton, &QPushButton::clicked,
            this, &ConversationList::startConversation);
    connect(userIdEdit_, &QLineEdit::returnPressed,
            this, &ConversationList::startConversation);
    connect(list_, &QListView::clicked,
            this, &ConversationList::onActivated);
}

void ConversationList::selectConversation(int userId)
{
    for (int row = 0; row < model_->rowCount(); ++row)
    {
        const QModelIndex source = model_->index(row, 0);
        if (source.data(ConversationModel::UserIdRole).toInt() != userId) continue;
        const QModelIndex proxyIndex = proxy_->mapFromSource(source);
        list_->setCurrentIndex(proxyIndex);
        return;
    }
}

void ConversationList::startConversation()
{
    const int userId = userIdEdit_->text().toInt();
    if (userId <= 0) return;
    userIdEdit_->clear();
    emit startConversationRequested(userId);
}

void ConversationList::onActivated(const QModelIndex &proxyIndex)
{
    const QModelIndex source = proxy_->mapToSource(proxyIndex);
    emit conversationSelected(
        source.data(ConversationModel::UserIdRole).toInt(),
        source.data(ConversationModel::DisplayNameRole).toString());
}

} // namespace client
} // namespace nexusim
