#include "historydialog.h"
#include <FluentQt/BasicInput.h>
#include <FluentQt/Collections.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QVBoxLayout>

using fluent::basicinput::Button;
using fluent::collections::ListView;
using fluent::layout::Card;
using fluent::layout::ListDetailView;
using fluent::textfields::Label;
using fluent::textfields::TextEdit;

namespace {
Label *makeLabel(const QString &text, Typography::FontRole role = Typography::FontRole::Body,
                 Label::TextColorRole color = Label::TextColorRole::Default) {
    auto *label = new Label(text);
    label->setFluentTypography(role);
    label->setTextColorRole(color);
    return label;
}
Button *makeButton(const QString &text, bool accent = false) {
    auto *button = new Button(text);
    button->setFluentStyle(accent ? Button::Accent : Button::Standard);
    return button;
}
class ScaledImageLabel : public QLabel {
  public:
    void setImage(const QImage &image) {
        m_image = image;
        updatePixmap();
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

  private:
    void updatePixmap() {
        if (m_image.isNull() || width() <= 0 || height() <= 0) {
            clear();
            return;
        }
        setPixmap(QPixmap::fromImage(m_image).scaled(size(), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
    }
    QImage m_image;
};
} // namespace

HistoryDialog::HistoryDialog(HistoryStore &store, QWidget *parent)
    : fluent::windowing::Window(parent), m_store(store) {
    setWindowTitle("截图与识别历史");
    setWindowIcon(QIcon(":/app.ico"));
    setCaptionButtonToolTips("最小化", "最大化", "关闭", "还原");
    resize(1040, 720);
    setMinimumSize(760, 560);

    auto *titleContent = new QWidget;
    auto *titleLayout = new QHBoxLayout(titleContent);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->addWidget(makeLabel("SnapLingo 历史", Typography::FontRole::Caption,
                                     Label::TextColorRole::Secondary));
    titleLayout->addStretch();
    titleBar()->setContentWidget(titleContent);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    m_split = new ListDetailView;
    m_split->setListPaneWidth(330);
    m_split->setCompactThreshold(820);
    layout->addWidget(m_split);
    setContentWidget(content);

    auto *listLayout = new QVBoxLayout(m_split->listPane());
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(10);
    auto *listHeader = new QHBoxLayout;
    listHeader->addWidget(makeLabel("截图与识别历史", Typography::FontRole::BodyLargeStrong));
    listHeader->addStretch();
    auto *clearButton = makeButton("清空");
    clearButton->setFluentStyle(Button::Subtle);
    clearButton->setIconGlyph(Typography::Icons::Delete, 14);
    listHeader->addWidget(clearButton);
    listLayout->addLayout(listHeader);

    m_list = new ListView;
    m_list->setObjectName("historyList");
    m_model = new QStandardItemModel(m_list);
    m_list->setModel(m_model);
    m_list->setSelectionMode(ListView::SelectionMode::Single);
    m_list->setSelectionIndicatorVisible(true);
    m_list->setBorderVisible(false);
    m_list->setBackgroundVisible(false);
    m_list->setFontRole(Typography::FontRole::Body);
    m_list->setTrailingTextRole(Qt::UserRole + 2);
    m_list->setPlaceholderText("还没有历史记录");
    listLayout->addWidget(m_list, 1);

    auto *detailLayout = new QVBoxLayout(m_split->detailPane());
    detailLayout->setContentsMargins(10, 0, 0, 0);
    detailLayout->setSpacing(10);
    m_detailContent = new QWidget;
    auto *detailContentLayout = new QVBoxLayout(m_detailContent);
    detailContentLayout->setContentsMargins(0, 0, 0, 0);
    detailContentLayout->setSpacing(10);
    auto *detailHeader = new QHBoxLayout;
    m_back = makeButton("");
    m_back->setFluentStyle(Button::Subtle);
    m_back->setIconGlyph(Typography::Icons::Back, 14);
    m_back->setFluentSize(Button::Small);
    m_back->hide();
    detailHeader->addWidget(m_back);
    m_meta = makeLabel(QString{}, Typography::FontRole::BodyStrong);
    detailHeader->addWidget(m_meta);
    detailHeader->addStretch();
    m_copy = makeButton("复制文字");
    m_copy->setFluentStyle(Button::Subtle);
    m_copy->setIconGlyph(Typography::Icons::Copy, 14);
    m_delete = makeButton("删除");
    m_delete->setFluentStyle(Button::Subtle);
    m_delete->setIconGlyph(Typography::Icons::Delete, 14);
    m_delete->setCriticalOnHover(true);
    detailHeader->addWidget(m_copy);
    detailHeader->addWidget(m_delete);
    detailContentLayout->addLayout(detailHeader);

    m_imageCard = new Card;
    m_imageCard->setMaximumHeight(310);
    auto *imageLayout = new QVBoxLayout(m_imageCard);
    imageLayout->setContentsMargins(8, 8, 8, 8);
    auto *image = new ScaledImageLabel;
    image->setMinimumHeight(180);
    image->setMaximumHeight(280);
    image->setAlignment(Qt::AlignCenter);
    m_image = image;
    imageLayout->addWidget(image);
    detailContentLayout->addWidget(m_imageCard);

    auto *textCard = new Card;
    auto *textLayout = new QVBoxLayout(textCard);
    textLayout->setContentsMargins(10, 8, 10, 10);
    textLayout->setSpacing(6);
    textLayout->addWidget(makeLabel("识别文字", Typography::FontRole::BodyStrong));
    m_text = new TextEdit;
    m_text->setReadOnly(true);
    m_text->setFrameVisible(false);
    m_text->setAutoResizeEnabled(false);
    m_text->setMinimumHeight(180);
    m_text->setPlaceholderText("未识别到文字");
    textLayout->addWidget(m_text, 1);
    detailContentLayout->addWidget(textCard, 1);
    detailLayout->addWidget(m_detailContent, 1);

    m_emptyDetail = new QWidget;
    auto *emptyLayout = new QVBoxLayout(m_emptyDetail);
    emptyLayout->addStretch();
    auto *emptyTitle = makeLabel("选择一条记录", Typography::FontRole::BodyLargeStrong);
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyHint = makeLabel("左侧会显示截图、识别时间与文字摘要。",
                                Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyHint);
    emptyLayout->addStretch();
    detailLayout->addWidget(m_emptyDetail, 1);

    connect(clearButton, &Button::clicked, this, &HistoryDialog::clearAll);
    connect(m_copy, &Button::clicked, this, [this] {
        QApplication::clipboard()->setText(m_text->toPlainText());
    });
    connect(m_delete, &Button::clicked, this, &HistoryDialog::deleteCurrent);
    connect(m_back, &Button::clicked, this, [this] { m_split->showList(); });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (!current.isValid())
                    return;
                const auto id = m_model->item(current.row())->data(Qt::UserRole).toString();
                showEntry(id);
                if (m_split->isCompact())
                    m_split->showDetail();
            });
    connect(m_split, &ListDetailView::compactChanged, this, [this] { updateBackButton(); });
    connect(m_split, &ListDetailView::currentPaneChanged, this, [this] { updateBackButton(); });

    refresh();
}

void HistoryDialog::reload() {
    refresh();
}

void HistoryDialog::showEvent(QShowEvent *event) {
    fluent::windowing::Window::showEvent(event);
    refresh();
}

void HistoryDialog::refresh() {
    m_store.reload();
    m_model->clear();
    for (const auto &entry : m_store.entries()) {
        auto *item = new QStandardItem(entry.created.toString("MM-dd HH:mm"));
        item->setData(entry.id, Qt::UserRole);
        item->setData(entry.text.simplified().left(80), Qt::UserRole + 2);
        item->setToolTip(entry.text.left(400));
        const auto image = m_store.image(entry);
        if (!image.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(image.scaled(96, 64, Qt::KeepAspectRatio,
                                                                Qt::SmoothTransformation))));
        m_model->appendRow(item);
    }
    if (m_model->rowCount() == 0) {
        m_currentId.clear();
        m_list->clearSelection();
        showEmptyDetail();
        return;
    }
    if (m_currentId.isEmpty()) {
        m_list->setCurrentIndex(m_model->index(0, 0));
    } else {
        int row = -1;
        for (int i = 0; i < m_model->rowCount(); ++i)
            if (m_model->item(i)->data(Qt::UserRole).toString() == m_currentId) {
                row = i;
                break;
            }
        if (row < 0)
            row = 0;
        m_list->setCurrentIndex(m_model->index(row, 0));
        showEntry(m_model->item(row)->data(Qt::UserRole).toString());
    }
}

void HistoryDialog::showEntry(const QString &id) {
    const HistoryEntry *found = nullptr;
    for (const auto &entry : m_store.entries())
        if (entry.id == id) {
            found = &entry;
            break;
        }
    if (!found) {
        showEmptyDetail();
        return;
    }
    m_currentId = id;
    auto meta = found->created.toString("yyyy-MM-dd HH:mm:ss");
    if (!found->ocrName.isEmpty())
        meta += " · " + found->ocrName;
    m_meta->setText(meta);
    const auto image = m_store.image(*found);
    static_cast<ScaledImageLabel *>(m_image)->setImage(image);
    m_imageCard->setVisible(!image.isNull());
    m_text->setPlainText(found->text);
    m_detailContent->show();
    m_emptyDetail->hide();
    m_copy->setEnabled(!found->text.isEmpty());
    m_delete->setEnabled(true);
    updateBackButton();
}

void HistoryDialog::showEmptyDetail() {
    m_meta->clear();
    m_text->clear();
    m_detailContent->hide();
    m_emptyDetail->show();
    m_copy->setEnabled(false);
    m_delete->setEnabled(false);
    updateBackButton();
}

void HistoryDialog::updateBackButton() {
    m_back->setVisible(m_split->isCompact() && m_split->currentPane() == ListDetailView::Detail);
}

void HistoryDialog::deleteCurrent() {
    if (m_currentId.isEmpty())
        return;
    m_store.remove(m_currentId);
    m_currentId.clear();
    refresh();
}

void HistoryDialog::clearAll() {
    if (m_store.entries().isEmpty())
        return;
    if (QMessageBox::question(this, "清空历史", "确定删除全部截图与识别记录吗？") != QMessageBox::Yes)
        return;
    m_store.clear();
    m_currentId.clear();
    refresh();
}
