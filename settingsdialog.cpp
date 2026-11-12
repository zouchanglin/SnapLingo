#include "settingsdialog.h"
#include "history.h"
#include "theme.h"
#include <FluentQt/BasicInput.h>
#include <FluentQt/Collections.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/Navigation.h>
#include <FluentQt/StatusInfo.h>
#include <FluentQt/TextFields.h>
#include <FluentQt/Windowing.h>
#include <QApplication>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QUrl>
#include <QVBoxLayout>
#include <windows.h>

using fluent::basicinput::Button;
using fluent::basicinput::CheckBox;
using fluent::basicinput::ComboBox;
using fluent::basicinput::HyperlinkButton;
using fluent::basicinput::RadioButton;
using fluent::collections::ListView;
using fluent::layout::Card;
using fluent::navigation::NavigationView;
using fluent::textfields::Label;
using fluent::textfields::LineEdit;
using fluent::textfields::PasswordBox;
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
QLineEdit *secretEdit() {
    auto *edit = new PasswordBox;
    edit->setClearButtonEnabled(false);
    return edit;
}
int listRow(ListView *list) {
    return list->currentIndex().row();
}
void setListRow(ListView *list, QStandardItemModel *model, int row) {
    if (row >= 0 && row < model->rowCount())
        list->setCurrentIndex(model->index(row, 0));
    else
        list->clearSelection();
}
void appendListRow(QStandardItemModel *model, const QString &text) {
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    model->appendRow(item);
}

// Nav rows carry their Fluent glyph so the icon can be re-rendered in the new theme color.
// zh_CN: 导航行保存自己的 Fluent 字形，换主题时按新配色重绘图标。
constexpr int NavGlyphRole = Qt::UserRole + 1;

QIcon navIcon(const QString &glyph, const QColor &color) {
    // Render above the common device pixel ratios so the icon stays crisp on scaled displays.
    // zh_CN: 按高于常见 DPI 的倍率绘制，保证缩放显示器上依然清晰。
    constexpr qreal kRenderScale = 3.0;
    constexpr int kIconExtent = 18;
    QPixmap pixmap(QSize(kIconExtent, kIconExtent) * kRenderScale);
    pixmap.setDevicePixelRatio(kRenderScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(color);
    Typography::Icons::paintGlyph(painter, QRectF(0, 0, kIconExtent, kIconExtent), glyph, kIconExtent);
    painter.end();
    return QIcon(pixmap);
}
} // namespace

QString hotkeyText(unsigned modifiers, unsigned key) {
    QStringList parts;
    if (modifiers & 2)
        parts << "Ctrl";
    if (modifiers & 1)
        parts << "Alt";
    if (modifiers & 4)
        parts << "Shift";
    if (modifiers & 8)
        parts << "Win";
    if (key) {
        wchar_t name[80]{};
        auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
        GetKeyNameTextW(LONG(scan << 16), name, 80);
        parts << QString::fromWCharArray(name);
    }
    return parts.join('+');
}

HotkeyEdit::HotkeyEdit(QWidget *p) : fluent::textfields::LineEdit(p) {
    setReadOnly(true);
    refresh();
}
void HotkeyEdit::refresh() {
    setText(hotkeyText(modifiers, key));
}
void HotkeyEdit::focusInEvent(QFocusEvent *e) {
    fluent::textfields::LineEdit::focusInEvent(e);
    emit recording(true);
    setText("请按下快捷键组合…");
}
void HotkeyEdit::focusOutEvent(QFocusEvent *e) {
    fluent::textfields::LineEdit::focusOutEvent(e);
    refresh();
    emit recording(false);
}
void HotkeyEdit::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Control || e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt ||
        e->key() == Qt::Key_Meta || e->key() == Qt::Key_AltGr)
        return;
    if (e->key() == Qt::Key_Escape) {
        clearFocus();
        return;
    }
    auto vk = e->nativeVirtualKey();
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || !vk)
        return;
    auto mods = e->modifiers();
    unsigned m = 0;
    if (mods & Qt::ControlModifier)
        m |= 2;
    if (mods & Qt::AltModifier)
        m |= 1;
    if (mods & Qt::ShiftModifier)
        m |= 4;
    if (mods & Qt::MetaModifier)
        m |= 8;
    if (!m)
        return;
    modifiers = m;
    key = unsigned(vk);
    refresh();
    clearFocus();
}

SettingsDialog::SettingsDialog(const Settings &settings, QWidget *parent, HistoryStore *historyStore)
    : fluent::windowing::Window(parent), draft(settings), history(historyStore) {
    setWindowTitle("SnapLingo 设置");
    setWindowIcon(QIcon(":/app.ico"));
    setCaptionButtonToolTips("最小化", "最大化", "关闭", "还原");
    resize(1020, 720);
    setMinimumSize(860, 620);

    auto *titleContent = new QWidget;
    auto *titleLayout = new QHBoxLayout(titleContent);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->addWidget(makeLabel("SnapLingo 设置", Typography::FontRole::Caption,
                                     Label::TextColorRole::Secondary));
    titleLayout->addStretch();
    titleBar()->setContentWidget(titleContent);

    auto *root = new QWidget;
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *nav = new NavigationView;
    nav->setDisplayMode(NavigationView::DisplayMode::Left);
    nav->setExpandedModeThresholdWidth(0);
    nav->setCompactModeThresholdWidth(0);
    nav->setExpandedPaneWidth(200);
    nav->setAnimationEnabled(true);

    auto *navHeader = new QWidget;
    auto *navHeaderLayout = new QVBoxLayout(navHeader);
    navHeaderLayout->setContentsMargins(18, 18, 18, 12);
    navHeaderLayout->setSpacing(2);
    navHeaderLayout->addWidget(makeLabel("设置", Typography::FontRole::BodyLargeStrong));
    navHeaderLayout->addWidget(makeLabel("SnapLingo", Typography::FontRole::Caption,
                                         Label::TextColorRole::Secondary));
    nav->setHeaderChromeWidget(navHeader);

    navList = new ListView;
    navList->setObjectName("navList");
    navList->setSelectionMode(ListView::SelectionMode::Single);
    navList->setBorderVisible(false);
    navList->setBackgroundVisible(false);
    navList->setFontRole(Typography::FontRole::Body);
    navList->setSelectionIndicatorVisible(true);
    navList->setIconSize(QSize(18, 18));
    auto *navModel = new QStandardItemModel(navList);
    // Navigation order: feature pages first, general settings next, About always last.
    // zh_CN: 导航顺序：功能页在前，通用设置居中，关于始终最后。
    struct NavSpec {
        const char *title;
        const QString &glyph;
    };
    const NavSpec navSpecs[] = {
        {"翻译模型", Typography::Icons::World},
        {"OCR 服务", Typography::Icons::Camera},
        {"提示词", Typography::Icons::Edit},
        {"历史记录", Typography::Icons::History},
        {"快捷键", Typography::Icons::Keyboard},
        {"外观", Typography::Icons::Color},
        {"关于", Typography::Icons::Info},
    };
    for (const auto &spec : navSpecs) {
        auto *item = new QStandardItem(QString::fromUtf8(spec.title));
        item->setEditable(false);
        item->setData(spec.glyph, NavGlyphRole);
        navModel->appendRow(item);
    }
    navList->setModel(navModel);
    refreshNavIcons();
    nav->setMainChromeWidget(navList);

    auto *pages = nav->contentHost();
    auto addPage = [pages](QWidget *page) {
        auto *host = new QWidget;
        auto *hostLayout = new QVBoxLayout(host);
        hostLayout->setContentsMargins(20, 18, 20, 18);
        hostLayout->addWidget(page);
        pages->insertPage(pages->count(), host);
    };
    rootLayout->addWidget(nav, 1);

    auto *modelPage = new QWidget;
    auto *ml = new QHBoxLayout(modelPage);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(16);
    auto *leftCard = new Card;
    auto *left = new QVBoxLayout(leftCard);
    left->setContentsMargins(14, 14, 14, 14);
    left->setSpacing(10);
    left->addWidget(makeLabel("模型列表", Typography::FontRole::BodyStrong));
    models = new ListView;
    models->setObjectName("models");
    models->setMinimumWidth(180);
    models->setMaximumWidth(220);
    models->setSelectionMode(ListView::SelectionMode::Single);
    models->setBorderVisible(false);
    models->setBackgroundVisible(false);
    models->setFontRole(Typography::FontRole::Body);
    modelsModel = new QStandardItemModel(models);
    models->setModel(modelsModel);
    left->addWidget(models, 1);
    auto *buttons = new QHBoxLayout;
    auto *add = makeButton("添加");
    add->setFluentStyle(Button::Subtle);
    add->setIconGlyph(Typography::Icons::Add, 14);
    auto *remove = makeButton("删除");
    remove->setFluentStyle(Button::Subtle);
    remove->setIconGlyph(Typography::Icons::Delete, 14);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    left->addLayout(buttons);
    ml->addWidget(leftCard);

    auto *formCard = new Card;
    auto *formOuter = new QVBoxLayout(formCard);
    formOuter->setContentsMargins(20, 18, 20, 18);
    formOuter->setSpacing(4);
    formOuter->addWidget(makeLabel("模型详情", Typography::FontRole::BodyStrong));
    formOuter->addWidget(makeLabel("配置 OpenAI 兼容接口，密钥只保存在本机。",
                                   Typography::FontRole::Caption, Label::TextColorRole::Secondary));
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 12, 0, 0);
    form->setVerticalSpacing(12);
    form->setHorizontalSpacing(14);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    modelName = new LineEdit;
    modelName->setObjectName("modelName");
    modelName->setPlaceholderText("例如 DeepSeek");
    endpoint = new LineEdit;
    endpoint->setPlaceholderText("https://api.deepseek.com/v1");
    apiKey = secretEdit();
    modelId = new LineEdit;
    modelId->setPlaceholderText("例如 deepseek-chat");
    thinking = new CheckBox("启用思考模式");
    thinking->setObjectName("thinking");
    form->addRow(makeLabel("显示名称"), modelName);
    form->addRow(makeLabel("API 地址"), endpoint);
    form->addRow(makeLabel("API Key"), apiKey);
    form->addRow(makeLabel("模型名称"), modelId);
    form->addRow(thinking);
    auto *modelHint = makeLabel("关闭思考模式可更快开始翻译。支持 DeepSeek 和 enable_thinking 兼容接口。"
                                "API 地址可以填写完整路径，或服务根地址。",
                                Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    modelHint->setWordWrap(true);
    form->addRow(modelHint);
    formOuter->addLayout(form);
    formOuter->addStretch();
    ml->addWidget(formCard, 1);
    addPage(modelPage);

    auto *ocrPage = new QWidget;
    auto *ol = new QHBoxLayout(ocrPage);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(16);
    auto *ocrLeftCard = new Card;
    auto *ocrLeft = new QVBoxLayout(ocrLeftCard);
    ocrLeft->setContentsMargins(14, 14, 14, 14);
    ocrLeft->setSpacing(10);
    ocrLeft->addWidget(makeLabel("OCR 服务", Typography::FontRole::BodyStrong));
    ocrs = new ListView;
    ocrs->setObjectName("ocrs");
    ocrs->setMinimumWidth(180);
    ocrs->setMaximumWidth(220);
    ocrs->setSelectionMode(ListView::SelectionMode::Single);
    ocrs->setBorderVisible(false);
    ocrs->setBackgroundVisible(false);
    ocrs->setFontRole(Typography::FontRole::Body);
    ocrsModel = new QStandardItemModel(ocrs);
    ocrs->setModel(ocrsModel);
    ocrLeft->addWidget(ocrs, 1);
    auto *ob = new QHBoxLayout;
    auto *oa = makeButton("添加");
    oa->setFluentStyle(Button::Subtle);
    oa->setIconGlyph(Typography::Icons::Add, 14);
    auto *od = makeButton("删除");
    od->setFluentStyle(Button::Subtle);
    od->setIconGlyph(Typography::Icons::Delete, 14);
    ob->addWidget(oa);
    ob->addWidget(od);
    ob->addStretch();
    ocrLeft->addLayout(ob);
    auto *active = makeButton("设为当前 OCR");
    active->setFluentStyle(Button::Subtle);
    active->setIconGlyph(Typography::Icons::CheckMark, 14);
    ocrLeft->addWidget(active);
    ol->addWidget(ocrLeftCard);

    auto *ocrFormCard = new Card;
    auto *ocrOuter = new QVBoxLayout(ocrFormCard);
    ocrOuter->setContentsMargins(20, 18, 20, 18);
    ocrOuter->setSpacing(4);
    ocrOuter->addWidget(makeLabel("OCR 详情", Typography::FontRole::BodyStrong));
    ocrOuter->addWidget(makeLabel("Windows 本地 OCR 开箱即用，云服务需要自备密钥。",
                                  Typography::FontRole::Caption, Label::TextColorRole::Secondary));
    auto *of = new QFormLayout;
    of->setContentsMargins(0, 12, 0, 0);
    of->setVerticalSpacing(12);
    of->setHorizontalSpacing(14);
    of->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ocrName = new LineEdit;
    ocrName->setObjectName("ocrName");
    ocrName->setPlaceholderText("例如 系统 OCR");
    provider = new ComboBox;
    provider->addItem("Windows 本地 OCR", "Windows");
    provider->addItem("火山引擎", "Volcengine");
    provider->addItem("百度 OCR", "Baidu");
    provider->addItem("腾讯云 OCR", "Tencent");
    of->addRow(makeLabel("名称"), ocrName);
    of->addRow(makeLabel("服务"), provider);
    credentials = new QStackedWidget;
    auto *localPage = new QWidget;
    auto *localLayout = new QVBoxLayout(localPage);
    localLayout->setContentsMargins(0, 0, 0, 0);
    auto *local = makeLabel("使用 Windows 系统 OCR，无需下载识别模型。请在系统中安装需要识别的语言包。",
                            Typography::FontRole::Body, Label::TextColorRole::Secondary);
    local->setWordWrap(true);
    localLayout->addWidget(local);
    localLayout->addStretch();
    credentials->addWidget(localPage);
    const QStringList labels = {"Access Key ID", "Secret Access Key", "API Key",
                                "Secret Key",    "Secret ID",         "Secret Key"};
    for (int i = 0; i < 3; ++i) {
        auto *page = new QWidget;
        auto *f = new QFormLayout(page);
        f->setContentsMargins(0, 0, 0, 0);
        f->setVerticalSpacing(12);
        for (int j = 0; j < 2; ++j) {
            auto *e = secretEdit();
            secrets.append(e);
            f->addRow(makeLabel(labels[i * 2 + j]), e);
            connect(e, &QLineEdit::textEdited, this, [this] { syncOcr(); });
        }
        credentials->addWidget(page);
    }
    of->addRow(credentials);
    ocrOuter->addLayout(of);
    ocrOuter->addStretch();
    ol->addWidget(ocrFormCard, 1);
    addPage(ocrPage);

    auto *promptPage = new QWidget;
    auto *pl = new QVBoxLayout(promptPage);
    pl->setContentsMargins(0, 0, 0, 0);
    auto *promptCard = new Card;
    auto *promptLayout = new QVBoxLayout(promptCard);
    promptLayout->setContentsMargins(20, 18, 20, 18);
    promptLayout->setSpacing(8);
    promptLayout->addWidget(makeLabel("翻译提示词", Typography::FontRole::BodyStrong));
    auto *promptHint = makeLabel("使用 {text} 表示识别结果；未填写占位符时，原文将追加到提示词末尾。",
                                 Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    promptHint->setWordWrap(true);
    promptLayout->addWidget(promptHint);
    prompt = new TextEdit;
    prompt->setPlainText(draft.prompt);
    prompt->setAutoResizeEnabled(false);
    prompt->setMinimumHeight(220);
    promptLayout->addWidget(prompt, 1);
    auto *reset = makeButton("恢复默认提示词");
    reset->setFluentStyle(Button::Subtle);
    reset->setIconGlyph(Typography::Icons::Refresh, 14);
    promptLayout->addWidget(reset, 0, Qt::AlignLeft);
    pl->addWidget(promptCard);
    addPage(promptPage);

    auto *historyPage = new QWidget;
    auto *hl = new QVBoxLayout(historyPage);
    hl->setContentsMargins(0, 0, 0, 0);
    auto *historyCard = new Card;
    auto *historyLayout = new QVBoxLayout(historyCard);
    historyLayout->setContentsMargins(20, 18, 20, 18);
    historyLayout->setSpacing(6);
    historyLayout->addWidget(makeLabel("截图与识别历史", Typography::FontRole::BodyStrong));
    auto *historyHint =
        makeLabel("成功识别后，在本机保存截图与识别文字，不会上传。可随时在历史窗口中查看。",
                  Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    historyHint->setWordWrap(true);
    historyLayout->addWidget(historyHint);
    historyLayout->addSpacing(10);
    historyEnabled = new CheckBox("保存截图与识别文字");
    historyEnabled->setChecked(draft.historyEnabled);
    historyLayout->addWidget(historyEnabled);
    auto *limitRow = new QHBoxLayout;
    limitRow->setContentsMargins(0, 8, 0, 0);
    limitRow->addWidget(makeLabel("最多保留"));
    historyLimit = new ComboBox;
    for (int n : {50, 100, 200, 500})
        historyLimit->addItem(QString("%1 条记录").arg(n), n);
    int limitIndex = historyLimit->findData(draft.historyLimit);
    if (limitIndex < 0) {
        historyLimit->addItem(QString("%1 条记录").arg(draft.historyLimit), draft.historyLimit);
        limitIndex = historyLimit->count() - 1;
    }
    historyLimit->setCurrentIndex(limitIndex);
    historyLimit->setEnabled(draft.historyEnabled);
    limitRow->addWidget(historyLimit);
    limitRow->addStretch();
    historyLayout->addLayout(limitRow);
    connect(historyEnabled, &QCheckBox::toggled, historyLimit, &QWidget::setEnabled);
    auto *historyActions = new QHBoxLayout;
    auto *openHistoryFolder = makeButton("打开历史文件夹");
    openHistoryFolder->setFluentStyle(Button::Subtle);
    openHistoryFolder->setIconGlyph(Typography::Icons::Folder, 14);
    auto *clearHistory = makeButton("清空历史");
    clearHistory->setFluentStyle(Button::Subtle);
    clearHistory->setIconGlyph(Typography::Icons::Delete, 14);
    historyActions->addWidget(openHistoryFolder);
    historyActions->addWidget(clearHistory);
    historyActions->addStretch();
    historyLayout->addLayout(historyActions);
    historyLayout->addStretch();
    hl->addWidget(historyCard);
    addPage(historyPage);

    connect(openHistoryFolder, &QPushButton::clicked, this, [this] {
        if (history)
            QDesktopServices::openUrl(QUrl::fromLocalFile(history->directory()));
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] {
        if (!history)
            return;
        if (QMessageBox::question(this, "清空历史", "确定删除全部截图与识别记录吗？") !=
            QMessageBox::Yes)
            return;
        history->clear();
    });
    if (!history) {
        openHistoryFolder->setEnabled(false);
        clearHistory->setEnabled(false);
    }

    auto *general = new QWidget;
    auto *gl = new QVBoxLayout(general);
    gl->setContentsMargins(0, 0, 0, 0);
    auto *generalCard = new Card;
    auto *generalLayout = new QVBoxLayout(generalCard);
    generalLayout->setContentsMargins(20, 18, 20, 18);
    generalLayout->setSpacing(4);
    generalLayout->addWidget(makeLabel("快捷键与启动", Typography::FontRole::BodyStrong));
    auto *generalForm = new QFormLayout;
    generalForm->setContentsMargins(0, 12, 0, 0);
    generalForm->setVerticalSpacing(12);
    generalForm->setHorizontalSpacing(14);
    generalForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hotkey = new HotkeyEdit;
    hotkey->modifiers = draft.modifiers;
    hotkey->key = draft.key;
    hotkey->refresh();
    connect(hotkey, &HotkeyEdit::recording, this, &SettingsDialog::recordingHotkey);
    generalForm->addRow(makeLabel("截图快捷键"), hotkey);
    autoStart = new CheckBox("登录 Windows 后启动");
    autoStart->setChecked(draft.autoStart);
    generalForm->addRow(autoStart);
    auto *generalHint = makeLabel("关闭主窗口后保留在系统托盘；从托盘菜单退出。配置沿用 "
                                  "%APPDATA%\\SnapLingo\\settings.json。",
                                  Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    generalHint->setWordWrap(true);
    generalForm->addRow(generalHint);
    generalLayout->addLayout(generalForm);
    generalLayout->addStretch();
    gl->addWidget(generalCard);
    addPage(general);

    auto *appearance = new QWidget;
    auto *al = new QVBoxLayout(appearance);
    al->setContentsMargins(0, 0, 0, 0);
    auto *appearanceCard = new Card;
    auto *appearanceLayout = new QVBoxLayout(appearanceCard);
    appearanceLayout->setContentsMargins(24, 20, 24, 20);
    appearanceLayout->setSpacing(4);
    appearanceLayout->addWidget(makeLabel("主题颜色", Typography::FontRole::BodyStrong));
    auto *appearanceHint =
        makeLabel("选择界面配色。切换后立即预览，点击“保存”写入配置，下次启动继续生效。",
                  Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    appearanceHint->setWordWrap(true);
    appearanceLayout->addWidget(appearanceHint);
    appearanceLayout->addSpacing(10);
    themeGroup = new QButtonGroup(this);
    themeGroup->setExclusive(true);
    const auto currentMode = themeModeFromKey(draft.theme);
    struct ThemeOption {
        ThemeMode mode;
        const char *description;
        const char *objectName;
    };
    const ThemeOption themeOptions[] = {
        {ThemeMode::System, "随 Windows“个性化 → 颜色”自动切换亮色或暗色。", "themeSystem"},
        {ThemeMode::Light, "始终使用浅色界面。", "themeLight"},
        {ThemeMode::Dark, "始终使用深色界面。", "themeDark"},
    };
    for (const auto &option : themeOptions) {
        auto *section = new QVBoxLayout;
        section->setContentsMargins(0, 6, 0, 6);
        section->setSpacing(2);
        auto *radio = new RadioButton(themeModeLabel(option.mode));
        radio->setObjectName(QString::fromLatin1(option.objectName));
        radio->setChecked(option.mode == currentMode);
        radio->setCursor(Qt::PointingHandCursor);
        themeGroup->addButton(radio, int(option.mode));
        auto *descriptionRow = new QHBoxLayout;
        descriptionRow->setContentsMargins(26, 0, 0, 0);
        auto *description = makeLabel(QString::fromUtf8(option.description),
                                      Typography::FontRole::Caption, Label::TextColorRole::Secondary);
        description->setWordWrap(true);
        descriptionRow->addWidget(description, 1);
        section->addWidget(radio);
        section->addLayout(descriptionRow);
        appearanceLayout->addLayout(section);
    }
    appearanceLayout->addSpacing(8);
    auto *appearanceNote =
        makeLabel("「跟随系统」会实时响应 Windows 的亮暗切换；固定模式则始终使用所选配色。",
                  Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    appearanceNote->setWordWrap(true);
    appearanceLayout->addWidget(appearanceNote);
    appearanceLayout->addStretch();
    al->addWidget(appearanceCard);
    addPage(appearance);

    auto *about = new QWidget;
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(0, 0, 0, 0);
    auto *aboutCard = new Card;
    auto *aboutInner = new QVBoxLayout(aboutCard);
    aboutInner->setContentsMargins(36, 32, 36, 32);
    aboutInner->setSpacing(8);
    aboutInner->setAlignment(Qt::AlignCenter);
    auto *appIcon = new QLabel;
    appIcon->setPixmap(QIcon(":/app.ico").pixmap(72, 72));
    appIcon->setAlignment(Qt::AlignCenter);
    auto *appTitle = makeLabel("SnapLingo", Typography::FontRole::Title);
    appTitle->setAlignment(Qt::AlignCenter);
    auto *version = makeLabel("版本 0.0.1 · C++20 / Qt 6 / Fluent-Qt", Typography::FontRole::Body,
                              Label::TextColorRole::Secondary);
    version->setAlignment(Qt::AlignCenter);
    auto *tagline = makeLabel("截图 → OCR → 多模型流式翻译", Typography::FontRole::Body,
                              Label::TextColorRole::Secondary);
    tagline->setAlignment(Qt::AlignCenter);
    auto *link = new HyperlinkButton("GitHub · zouchanglin/SnapLingo",
                                     QUrl("https://github.com/zouchanglin/SnapLingo"));
    auto *license = makeLabel("Qt 采用动态链接；许可与第三方声明见软件目录 licenses。",
                              Typography::FontRole::Caption, Label::TextColorRole::Secondary);
    license->setAlignment(Qt::AlignCenter);
    license->setWordWrap(true);
    aboutInner->addStretch();
    aboutInner->addWidget(appIcon);
    aboutInner->addWidget(appTitle);
    aboutInner->addWidget(version);
    aboutInner->addWidget(tagline);
    aboutInner->addSpacing(6);
    aboutInner->addWidget(link, 0, Qt::AlignCenter);
    aboutInner->addWidget(license);
    aboutInner->addStretch();
    aboutLayout->addWidget(aboutCard);
    addPage(about);

    auto *navSelection = navList->selectionModel();
    connect(navSelection, &QItemSelectionModel::currentChanged, this,
            [pages](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid())
                    pages->setCurrentIndex(current.row(), 1, true);
            });
    if (navModel->rowCount() > 0)
        navList->setCurrentIndex(navModel->index(0, 0));
    pages->setCurrentIndex(0, 0, false);

    auto *footerBar = new QWidget;
    auto *footer = new QHBoxLayout(footerBar);
    footer->setContentsMargins(18, 10, 18, 14);
    footer->addStretch();
    auto *closeButton = makeButton("关闭");
    auto *saveButton = makeButton("保存", true);
    saveButton->setIconGlyph(Typography::Icons::Save, 16);
    footer->addWidget(closeButton);
    footer->addWidget(saveButton);
    rootLayout->addWidget(footerBar);
    setContentWidget(root);

    for (const auto &m : draft.models)
        appendListRow(modelsModel, m.name);
    connect(models->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid())
                    loadModel();
            });
    for (auto *field : {modelName, endpoint, apiKey, modelId})
        connect(field, &QLineEdit::textEdited, this, [this] { syncModel(); });
    connect(thinking, &QCheckBox::toggled, this, [this] { syncModel(); });
    connect(add, &QPushButton::clicked, this, [this] {
        Model m;
        m.name = QString("模型 %1").arg(draft.models.size() + 1);
        draft.models.append(m);
        appendListRow(modelsModel, m.name);
        setListRow(models, modelsModel, int(draft.models.size() - 1));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        int i = listRow(models);
        if (i < 0)
            return;
        draft.models.removeAt(i);
        {
            QSignalBlocker block(models->selectionModel());
            modelsModel->removeRow(i);
            setListRow(models, modelsModel, qMin(i, int(draft.models.size() - 1)));
        }
        loadModel();
    });
    setListRow(models, modelsModel, draft.models.isEmpty() ? -1 : 0);
    loadModel();

    connect(ocrs->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid())
                    loadOcr();
            });
    connect(ocrName, &QLineEdit::textEdited, this, [this] { syncOcr(); });
    connect(provider, &QComboBox::currentIndexChanged, this, [this](int i) {
        credentials->setCurrentIndex(i);
        syncOcr();
    });
    connect(oa, &QPushButton::clicked, this, [this] {
        draft.ocrs.append(OcrConfig{});
        refreshOcrList();
        setListRow(ocrs, ocrsModel, int(draft.ocrs.size() - 1));
        loadOcr();
    });
    connect(od, &QPushButton::clicked, this, [this] {
        int i = listRow(ocrs);
        if (i < 0)
            return;
        draft.ocrs.removeAt(i);
        if (i < draft.activeOcr)
            --draft.activeOcr;
        draft.activeOcr = qBound(0, draft.activeOcr, qMax(0, int(draft.ocrs.size() - 1)));
        refreshOcrList();
        setListRow(ocrs, ocrsModel, qMin(i, int(draft.ocrs.size() - 1)));
        loadOcr();
    });
    connect(active, &QPushButton::clicked, this, [this] {
        const int row = listRow(ocrs);
        if (row < 0)
            return;
        draft.activeOcr = row;
        refreshOcrList();
        const auto name = row < draft.ocrs.size() ? draft.ocrs[row].name : QString{};
        showToast(name.isEmpty() ? QStringLiteral("已设为当前 OCR")
                                 : QStringLiteral("已设为当前 OCR：%1").arg(name));
    });
    connect(reset, &QPushButton::clicked, this,
            [this] { prompt->setPlainText(Settings::defaultPrompt()); });
    connect(themeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        const auto mode = ThemeMode(id);
        draft.theme = themeModeKey(mode);
        // Preview immediately; Window restores the saved mode if the dialog is closed without saving.
        // zh_CN: 立即预览；若未保存就关闭对话框，主窗口会恢复已保存的模式。
        applyAppTheme(mode);
        showToast(mode == ThemeMode::System
                      ? QStringLiteral("已切换为跟随系统")
                      : QStringLiteral("已切换到%1主题").arg(themeModeLabel(mode)));
    });
    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(closeButton, &QPushButton::clicked, this, [this] { close(); });

    refreshOcrList();
    setListRow(ocrs, ocrsModel, draft.activeOcr);
    loadOcr();
}
void SettingsDialog::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    fluent::windowing::Window::keyPressEvent(e);
}
void SettingsDialog::setPage(int index) {
    if (!navList || !navList->model() || navList->model()->rowCount() == 0)
        return;
    const int row = qBound(0, index, navList->model()->rowCount() - 1);
    navList->setCurrentIndex(navList->model()->index(row, 0));
}
void SettingsDialog::loadModel() {
    loading = true;
    int i = listRow(models);
    bool valid = i >= 0 && i < draft.models.size();
    Model m = valid ? draft.models[i] : Model{};
    modelName->setText(valid ? m.name : QString{});
    endpoint->setText(valid ? m.endpoint : QString{});
    apiKey->setText(m.key);
    modelId->setText(valid ? m.model : QString{});
    for (auto *e : {modelName, endpoint, apiKey, modelId})
        e->setCursorPosition(0);
    thinking->setChecked(valid && m.thinking);
    for (auto *e : {modelName, endpoint, apiKey, modelId})
        e->setEnabled(valid);
    thinking->setEnabled(valid);
    loading = false;
}
void SettingsDialog::syncModel() {
    if (loading)
        return;
    int i = listRow(models);
    if (i < 0 || i >= draft.models.size())
        return;
    auto &m = draft.models[i];
    m.name = modelName->text();
    m.endpoint = endpoint->text().trimmed();
    m.key = apiKey->text().trimmed();
    m.model = modelId->text().trimmed();
    m.thinking = thinking->isChecked();
    modelsModel->item(i)->setText(m.name);
}
void SettingsDialog::refreshOcrList() {
    const int i = listRow(ocrs);
    QSignalBlocker block(ocrs->selectionModel());
    ocrsModel->clear();
    for (int n = 0; n < draft.ocrs.size(); ++n)
        appendListRow(ocrsModel, (n == draft.activeOcr ? "★ " : "") + draft.ocrs[n].name);
    setListRow(ocrs, ocrsModel, qMin(i, int(draft.ocrs.size() - 1)));
}
void SettingsDialog::loadOcr() {
    loading = true;
    int i = listRow(ocrs);
    bool valid = i >= 0 && i < draft.ocrs.size();
    OcrConfig o = valid ? draft.ocrs[i] : OcrConfig{};
    ocrName->setText(valid ? o.name : QString{});
    provider->setCurrentIndex(qMax(0, provider->findData(o.provider)));
    credentials->setCurrentIndex(provider->currentIndex());
    QStringList values = {o.access, o.secret, o.baiduKey, o.baiduSecret, o.tencentId, o.tencentKey};
    for (int n = 0; n < 6; ++n) {
        secrets[n]->setText(values[n]);
        secrets[n]->setEnabled(valid);
    }
    ocrName->setEnabled(valid);
    provider->setEnabled(valid);
    loading = false;
}
void SettingsDialog::syncOcr() {
    if (loading)
        return;
    int i = listRow(ocrs);
    if (i < 0 || i >= draft.ocrs.size())
        return;
    auto &o = draft.ocrs[i];
    o.name = ocrName->text();
    o.provider = provider->currentData().toString();
    o.access = secrets[0]->text().trimmed();
    o.secret = secrets[1]->text().trimmed();
    o.baiduKey = secrets[2]->text().trimmed();
    o.baiduSecret = secrets[3]->text().trimmed();
    o.tencentId = secrets[4]->text().trimmed();
    o.tencentKey = secrets[5]->text().trimmed();
    ocrsModel->item(i)->setText((i == draft.activeOcr ? "★ " : "") + o.name);
}
void SettingsDialog::save() {
    syncModel();
    syncOcr();
    draft.prompt = prompt->toPlainText();
    draft.modifiers = hotkey->modifiers;
    draft.key = hotkey->key;
    draft.autoStart = autoStart->isChecked();
    draft.historyEnabled = historyEnabled->isChecked();
    draft.historyLimit = historyLimit->currentData().toInt();
    if (auto *checked = themeGroup->checkedButton())
        draft.theme = themeModeKey(ThemeMode(themeGroup->id(checked)));
    if (validate) {
        auto error = validate(draft);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, "无法保存", error);
            return;
        }
    }
    try {
        draft.save();
        QSettings registry("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                           QSettings::NativeFormat);
        if (draft.autoStart)
            registry.setValue("SnapLingo",
                              '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + '"');
        else
            registry.remove("SnapLingo");
        registry.sync();
        emit saved(draft);
        if (registry.status() != QSettings::NoError)
            QMessageBox::warning(this, "设置已保存", "开机启动设置失败，请检查注册表权限。");
        else
            showToast(QStringLiteral("设置已保存"));
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "保存失败", QString::fromUtf8(e.what()));
    }
}
void SettingsDialog::showToast(const QString &message, bool success) {
    fluent::status_info::Toast::showToast(
        this, message,
        success ? fluent::status_info::Toast::Success : fluent::status_info::Toast::Informational, 2600,
        fluent::status_info::Toast::Bottom);
}
void SettingsDialog::refreshNavIcons() {
    if (!navList)
        return;
    auto *model = qobject_cast<QStandardItemModel *>(navList->model());
    if (!model)
        return;
    const QColor color = themeColorsRef().textSecondary;
    for (int row = 0; row < model->rowCount(); ++row) {
        auto *item = model->item(row);
        if (!item)
            continue;
        const auto glyph = item->data(NavGlyphRole).toString();
        if (!glyph.isEmpty())
            item->setIcon(navIcon(glyph, color));
    }
}
void SettingsDialog::onThemeUpdated() {
    fluent::windowing::Window::onThemeUpdated();
    refreshNavIcons();
}
