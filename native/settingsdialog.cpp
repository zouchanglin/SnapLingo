#include "settingsdialog.h"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <windows.h>

HotkeyEdit::HotkeyEdit(QWidget *p) : QLineEdit(p) {
    setReadOnly(true);
    refresh();
}
void HotkeyEdit::refresh() {
    QStringList parts;
    if (modifiers & 2)
        parts << "Ctrl";
    if (modifiers & 1)
        parts << "Alt";
    if (modifiers & 4)
        parts << "Shift";
    if (modifiers & 8)
        parts << "Win";
    wchar_t name[80]{};
    auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    GetKeyNameTextW(LONG(scan << 16), name, 80);
    parts << QString::fromWCharArray(name);
    setText(parts.join('+'));
}
void HotkeyEdit::focusInEvent(QFocusEvent *e) {
    QLineEdit::focusInEvent(e);
    emit recording(true);
    setText("请按下快捷键组合…");
}
void HotkeyEdit::focusOutEvent(QFocusEvent *e) {
    QLineEdit::focusOutEvent(e);
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
static QLineEdit *secretEdit() {
    auto *e = new QLineEdit;
    e->setEchoMode(QLineEdit::Password);
    auto *a =
        new QAction(QApplication::style()->standardIcon(QStyle::SP_DialogYesButton), "显示/隐藏密钥", e);
    e->addAction(a, QLineEdit::TrailingPosition);
    QObject::connect(a, &QAction::triggered, e, [=] {
        bool hidden = e->echoMode() == QLineEdit::Password;
        e->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        a->setText(hidden ? "隐藏密钥" : "显示密钥");
    });
    return e;
}
SettingsDialog::SettingsDialog(const Settings &settings, QWidget *parent) : QDialog(parent), draft(settings) {
    setWindowTitle("SnapLingo 设置");
    resize(800, 590);
    setMinimumSize(680, 520);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(14);
    auto *heading = new QLabel("偏好设置");
    QFont headingFont = heading->font();
    headingFont.setPointSize(15);
    headingFont.setWeight(QFont::DemiBold);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto *modelPage = new QWidget;
    auto *ml = new QHBoxLayout(modelPage);
    ml->setContentsMargins(14, 18, 14, 16);
    ml->setSpacing(22);
    auto *left = new QVBoxLayout;
    models = new QListWidget;
    models->setObjectName("models");
    models->setMaximumWidth(200);
    left->addWidget(models);
    auto *buttons = new QHBoxLayout;
    auto *add = new QPushButton("添加");
    auto *remove = new QPushButton("删除");
    buttons->addWidget(add);
    buttons->addWidget(remove);
    left->addLayout(buttons);
    ml->addLayout(left);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(14);
    form->setHorizontalSpacing(12);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    modelName = new QLineEdit;
    modelName->setObjectName("modelName");
    endpoint = new QLineEdit;
    apiKey = secretEdit();
    modelId = new QLineEdit;
    thinking = new QCheckBox("启用思考模式");
    thinking->setObjectName("thinking");
    form->addRow("显示名称", modelName);
    form->addRow("API 地址", endpoint);
    form->addRow("API Key", apiKey);
    form->addRow("模型名称", modelId);
    form->addRow("", thinking);
    auto *hint = new QLabel("关闭思考模式可更快开始翻译。\n支持 DeepSeek 和 enable_thinking 兼容接口。\nAPI "
                            "地址可以填写完整路径，或服务根地址。");
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    form->addRow(hint);
    ml->addLayout(form, 1);
    tabs->addTab(modelPage, "翻译模型");
    for (const auto &m : draft.models)
        models->addItem(m.name);
    connect(models, &QListWidget::currentRowChanged, this, [this] { loadModel(); });
    for (auto *field : {modelName, endpoint, apiKey, modelId})
        connect(field, &QLineEdit::textEdited, this, [this] { syncModel(); });
    connect(thinking, &QCheckBox::toggled, this, [this] { syncModel(); });
    connect(add, &QPushButton::clicked, this, [this] {
        Model m;
        m.name = QString("模型 %1").arg(draft.models.size() + 1);
        draft.models.append(m);
        models->addItem(m.name);
        models->setCurrentRow(int(draft.models.size() - 1));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        int i = models->currentRow();
        if (i < 0)
            return;
        draft.models.removeAt(i);
        {
            QSignalBlocker block(models);
            delete models->takeItem(i);
            models->setCurrentRow(qMin(i, int(draft.models.size() - 1)));
        }
        loadModel();
    });
    models->setCurrentRow(draft.models.isEmpty() ? -1 : 0);
    loadModel();

    auto *ocrPage = new QWidget;
    auto *ol = new QHBoxLayout(ocrPage);
    ol->setContentsMargins(14, 18, 14, 16);
    ol->setSpacing(22);
    auto *ocrLeft = new QVBoxLayout;
    ocrs = new QListWidget;
    ocrs->setObjectName("ocrs");
    ocrs->setMaximumWidth(200);
    ocrLeft->addWidget(ocrs);
    auto *ob = new QHBoxLayout;
    auto *oa = new QPushButton("添加");
    auto *od = new QPushButton("删除");
    ob->addWidget(oa);
    ob->addWidget(od);
    ocrLeft->addLayout(ob);
    auto *active = new QPushButton("设为当前 OCR");
    ocrLeft->addWidget(active);
    ol->addLayout(ocrLeft);
    auto *of = new QFormLayout;
    of->setVerticalSpacing(14);
    of->setHorizontalSpacing(12);
    of->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ocrName = new QLineEdit;
    ocrName->setObjectName("ocrName");
    provider = new QComboBox;
    provider->addItem("Windows 本地 OCR", "Windows");
    provider->addItem("火山引擎", "Volcengine");
    provider->addItem("百度 OCR", "Baidu");
    provider->addItem("腾讯云 OCR", "Tencent");
    of->addRow("名称", ocrName);
    of->addRow("服务", provider);
    credentials = new QStackedWidget;
    auto *local = new QLabel("使用 Windows 系统 OCR，无需下载识别模型。\n请在系统中安装需要识别的语言包。");
    local->setWordWrap(true);
    credentials->addWidget(local);
    const QStringList labels = {"Access Key ID", "Secret Access Key", "API Key",
                                "Secret Key",    "Secret ID",         "Secret Key"};
    for (int i = 0; i < 3; ++i) {
        auto *page = new QWidget;
        auto *f = new QFormLayout(page);
        for (int j = 0; j < 2; ++j) {
            auto *e = secretEdit();
            secrets.append(e);
            f->addRow(labels[i * 2 + j], e);
            connect(e, &QLineEdit::textEdited, this, [this] { syncOcr(); });
        }
        credentials->addWidget(page);
    }
    of->addRow(credentials);
    ol->addLayout(of, 1);
    tabs->addTab(ocrPage, "OCR");
    connect(ocrs, &QListWidget::currentRowChanged, this, [this] { loadOcr(); });
    connect(ocrName, &QLineEdit::textEdited, this, [this] { syncOcr(); });
    connect(provider, &QComboBox::currentIndexChanged, this, [this](int i) {
        credentials->setCurrentIndex(i);
        syncOcr();
    });
    connect(oa, &QPushButton::clicked, this, [this] {
        draft.ocrs.append(OcrConfig{});
        refreshOcrList();
        ocrs->setCurrentRow(int(draft.ocrs.size() - 1));
        loadOcr();
    });
    connect(od, &QPushButton::clicked, this, [this] {
        int i = ocrs->currentRow();
        if (i < 0)
            return;
        draft.ocrs.removeAt(i);
        if (i < draft.activeOcr)
            --draft.activeOcr;
        draft.activeOcr = qBound(0, draft.activeOcr, qMax(0, int(draft.ocrs.size() - 1)));
        refreshOcrList();
        ocrs->setCurrentRow(qMin(i, int(draft.ocrs.size() - 1)));
        loadOcr();
    });
    connect(active, &QPushButton::clicked, this, [this] {
        if (ocrs->currentRow() < 0)
            return;
        draft.activeOcr = ocrs->currentRow();
        refreshOcrList();
    });
    refreshOcrList();
    ocrs->setCurrentRow(draft.activeOcr);
    loadOcr();

    auto *promptPage = new QWidget;
    auto *pl = new QVBoxLayout(promptPage);
    pl->addWidget(new QLabel("使用 {text} 表示识别结果；未填写占位符时，原文将追加到提示词末尾。"));
    prompt = new QPlainTextEdit(draft.prompt);
    pl->addWidget(prompt);
    auto *reset = new QPushButton("恢复默认提示词");
    pl->addWidget(reset, 0, Qt::AlignLeft);
    connect(reset, &QPushButton::clicked, this, [this] { prompt->setPlainText(Settings::defaultPrompt()); });
    tabs->addTab(promptPage, "翻译提示词");
    auto *general = new QWidget;
    auto *gl = new QFormLayout(general);
    hotkey = new HotkeyEdit;
    hotkey->modifiers = draft.modifiers;
    hotkey->key = draft.key;
    hotkey->refresh();
    connect(hotkey, &HotkeyEdit::recording, this, &SettingsDialog::recordingHotkey);
    gl->addRow("截图快捷键", hotkey);
    autoStart = new QCheckBox("登录 Windows 后启动");
    autoStart->setChecked(draft.autoStart);
    gl->addRow("", autoStart);
    gl->addRow(new QLabel(
        "关闭主窗口后保留在系统托盘；从托盘菜单退出。\n配置沿用 %APPDATA%\\SnapLingo\\settings.json。"));
    tabs->addTab(general, "快捷键与启动");
    auto *about = new QLabel(
        "<h2>SnapLingo 0.3.0</h2><p>C++ + Qt 便携版</p><p>截图 → OCR → 多模型流式翻译</p><p><a "
        "href='https://github.com/zouchanglin/SnapLingo'>GitHub · zouchanglin/SnapLingo</a></p><p>Qt "
        "采用动态链接，许可与第三方声明见软件目录 licenses。</p>");
    about->setAlignment(Qt::AlignCenter);
    about->setOpenExternalLinks(true);
    tabs->addTab(about, "关于");
    auto *footer = new QDialogButtonBox;
    auto *saveButton = footer->addButton("保存", QDialogButtonBox::AcceptRole);
    saveButton->setProperty("primary", true);
    auto *closeButton = footer->addButton("关闭", QDialogButtonBox::RejectRole);
    layout->addWidget(footer);
    connect(saveButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}
void SettingsDialog::loadModel() {
    loading = true;
    int i = models->currentRow();
    bool valid = i >= 0 && i < draft.models.size();
    Model m = valid ? draft.models[i] : Model{};
    modelName->setText(valid ? m.name : QString{});
    endpoint->setText(valid ? m.endpoint : QString{});
    apiKey->setText(m.key);
    modelId->setText(valid ? m.model : QString{});
    thinking->setChecked(valid && m.thinking);
    for (auto *e : {modelName, endpoint, apiKey, modelId})
        e->setEnabled(valid);
    thinking->setEnabled(valid);
    loading = false;
}
void SettingsDialog::syncModel() {
    if (loading)
        return;
    int i = models->currentRow();
    if (i < 0 || i >= draft.models.size())
        return;
    auto &m = draft.models[i];
    m.name = modelName->text();
    m.endpoint = endpoint->text().trimmed();
    m.key = apiKey->text().trimmed();
    m.model = modelId->text().trimmed();
    m.thinking = thinking->isChecked();
    models->item(i)->setText(m.name);
}
void SettingsDialog::refreshOcrList() {
    QSignalBlocker block(ocrs);
    int i = ocrs->currentRow();
    ocrs->clear();
    for (int n = 0; n < draft.ocrs.size(); ++n)
        ocrs->addItem((n == draft.activeOcr ? "★ " : "") + draft.ocrs[n].name);
    ocrs->setCurrentRow(qMin(i, int(draft.ocrs.size() - 1)));
}
void SettingsDialog::loadOcr() {
    loading = true;
    int i = ocrs->currentRow();
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
    int i = ocrs->currentRow();
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
    ocrs->item(i)->setText((i == draft.activeOcr ? "★ " : "") + o.name);
}
void SettingsDialog::save() {
    syncModel();
    syncOcr();
    draft.prompt = prompt->toPlainText();
    draft.modifiers = hotkey->modifiers;
    draft.key = hotkey->key;
    draft.autoStart = autoStart->isChecked();
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
            QMessageBox::information(this, "设置", "已保存并立即生效");
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "保存失败", QString::fromUtf8(e.what()));
    }
}
