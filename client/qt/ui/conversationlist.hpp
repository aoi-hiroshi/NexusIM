#ifndef NEXUSIM_QT_CONVERSATIONLIST_HPP
#define NEXUSIM_QT_CONVERSATIONLIST_HPP

#include <QWidget>

class QLineEdit;
class QListView;
class QSortFilterProxyModel;

namespace nexusim {
namespace client {

class ConversationModel;

class ConversationList : public QWidget
{
    Q_OBJECT
public:
    explicit ConversationList(ConversationModel *model,
                              QWidget *parent = nullptr);
    void selectConversation(int userId);

signals:
    void conversationSelected(int userId, const QString &displayName);
    void startConversationRequested(int userId);

private slots:
    void startConversation();
    void onActivated(const QModelIndex &proxyIndex);

private:
    ConversationModel *model_;
    QSortFilterProxyModel *proxy_;
    QListView *list_;
    QLineEdit *searchEdit_;
    QLineEdit *userIdEdit_;
};

} // namespace client
} // namespace nexusim

#endif
