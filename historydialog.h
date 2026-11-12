#pragma once
#include "history.h"
#include <FluentQt/Windowing.h>
#include <QPointer>

class QStandardItemModel;
class QLabel;
class QShowEvent;
namespace fluent::basicinput {
class Button;
}
namespace fluent::collections {
class ListView;
}
namespace fluent::layout {
class ListDetailView;
}
namespace fluent::textfields {
class Label;
class TextEdit;
}

class HistoryDialog : public fluent::windowing::Window {
    Q_OBJECT
  public:
    explicit HistoryDialog(HistoryStore &store, QWidget *parent = nullptr);
    void reload();

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    void refresh();
    void showEntry(const QString &id);
    void showEmptyDetail();
    void updateBackButton();
    void deleteCurrent();
    void clearAll();

    HistoryStore &m_store;
    fluent::layout::ListDetailView *m_split = nullptr;
    fluent::collections::ListView *m_list = nullptr;
    QStandardItemModel *m_model = nullptr;
    fluent::textfields::Label *m_meta = nullptr;
    fluent::textfields::TextEdit *m_text = nullptr;
    fluent::basicinput::Button *m_delete = nullptr;
    fluent::basicinput::Button *m_copy = nullptr;
    fluent::basicinput::Button *m_back = nullptr;
    QWidget *m_imageCard = nullptr;
    QWidget *m_detailContent = nullptr;
    QWidget *m_emptyDetail = nullptr;
    QLabel *m_image = nullptr;
    QString m_currentId;
};
