#include "capture.h"
#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QtGui/qscreen_platform.h>
#include <stdexcept>
#include <windows.h>

static QPoint physicalCursor() {
    POINT p{};
    GetCursorPos(&p);
    return {p.x, p.y};
}
static BOOL CALLBACK enumerate(HMONITOR h, HDC, LPRECT, LPARAM data) {
    MONITORINFOEXW i{};
    i.cbSize = sizeof(i);
    GetMonitorInfoW(h, &i);
    auto r = i.rcMonitor;
    reinterpret_cast<QVector<Monitor> *>(data)->append(
        {QString::fromWCharArray(i.szDevice), QRect(r.left, r.top, r.right - r.left, r.bottom - r.top)});
    return TRUE;
}
QVector<Monitor> Capture::monitors() {
    QVector<Monitor> m;
    EnumDisplayMonitors(nullptr, nullptr, enumerate, reinterpret_cast<LPARAM>(&m));
    return m;
}
QRect Capture::screenPixels(QScreen *screen) {
    auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!native || !GetMonitorInfoW(native->handle(), &info))
        throw std::runtime_error("无法获取显示器坐标，请重新连接显示器后重试");
    auto r = info.rcMonitor;
    return {r.left, r.top, r.right - r.left, r.bottom - r.top};
}
QRect Capture::selection(QPoint a, QPoint b, QRect bounds) {
    return QRect(QPoint(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                 QPoint(qMax(a.x(), b.x()), qMax(a.y(), b.y())))
        .intersected(bounds);
}
QImage Capture::grabDesktop(QRect &bounds) {
    bounds = QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                   GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.width();
    info.bmiHeader.biHeight = -bounds.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        throw std::runtime_error("无法创建桌面快照");
    }
    auto old = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, bounds.width(), bounds.height(), screen, bounds.x(), bounds.y(),
                     SRCCOPY | CAPTUREBLT);
    QImage image;
    if (ok)
        image =
            QImage(static_cast<uchar *>(bits), bounds.width(), bounds.height(), QImage::Format_RGB32).copy();
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (image.isNull())
        throw std::runtime_error("无法截取桌面");
    return image;
}
class Overlay : public QWidget {
    Capture *capture;
    QRect pixels;

  public:
    Overlay(Capture *c, QScreen *screen, const QRect &physical)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), capture(c),
          pixels(physical) {
        setWindowTitle("SnapLingo 截图选区");
        setAttribute(Qt::WA_DeleteOnClose, false);
        setMouseTracking(true);
        setCursor(Qt::BlankCursor);
        setScreen(screen);
        setGeometry(screen->geometry());
        auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(esc, &QShortcut::activated, c, &Capture::cancel);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            capture->cancel();
            return;
        }
        if (e->button() == Qt::LeftButton) {
            grabMouse();
            capture->begin();
        }
    }
    void mouseMoveEvent(QMouseEvent *) override {
        capture->move();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            releaseMouse();
            capture->end();
        }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QRect source = pixels.translated(-capture->desktop.topLeft());
        p.drawImage(rect(), capture->snapshot, source);
        p.fillRect(rect(), QColor(0, 0, 0, 100));
        auto map = [&](QRect r) {
            return QRectF((r.x() - pixels.x()) * double(width()) / pixels.width(),
                          (r.y() - pixels.y()) * double(height()) / pixels.height(),
                          r.width() * double(width()) / pixels.width(),
                          r.height() * double(height()) / pixels.height());
        };
        if (capture->selecting) {
            auto r = Capture::selection(capture->anchor, capture->cursor, capture->desktop);
            auto local = map(r);
            p.save();
            p.setClipRect(local);
            p.drawImage(rect(), capture->snapshot, source);
            p.restore();
            p.setPen(QPen(QColor("#408cff"), 2));
            p.drawRect(local);
        }
        if (!pixels.contains(capture->cursor))
            return;
        QPointF pos((capture->cursor.x() - pixels.x()) * double(width()) / pixels.width(),
                    (capture->cursor.y() - pixels.y()) * double(height()) / pixels.height());
        p.setPen(QPen(Qt::black, 3));
        p.drawLine(pos + QPointF(-14, 0), pos + QPointF(14, 0));
        p.drawLine(pos + QPointF(0, -14), pos + QPointF(0, 14));
        p.setPen(Qt::white);
        p.drawLine(pos + QPointF(-14, 0), pos + QPointF(14, 0));
        p.drawLine(pos + QPointF(0, -14), pos + QPointF(0, 14));
        QPoint mag(qBound(4, int(pos.x()) + 24, width() - 164), qBound(4, int(pos.y()) + 24, height() - 132));
        QRect src(capture->cursor - capture->desktop.topLeft() - QPoint(25, 16), QSize(50, 32));
        src.moveLeft(qBound(0, src.x(), qMax(0, capture->snapshot.width() - 50)));
        src.moveTop(qBound(0, src.y(), qMax(0, capture->snapshot.height() - 32)));
        QRect target(mag, QSize(150, 96));
        p.drawImage(target, capture->snapshot, src);
        p.setPen(QPen(QColor("#408cff"), 2));
        p.drawRect(target);
        p.setPen(Qt::red);
        p.drawLine(target.center() - QPoint(6, 0), target.center() + QPoint(6, 0));
        p.drawLine(target.center() - QPoint(0, 6), target.center() + QPoint(0, 6));
        QRect label(mag + QPoint(0, 98), QSize(150, 26));
        p.fillRect(label, QColor("#182236"));
        p.setPen(Qt::white);
        auto size = Capture::selection(capture->anchor, capture->cursor, capture->desktop).size();
        p.drawText(label, Qt::AlignCenter,
                   capture->selecting ? QString("%1 × %2").arg(size.width()).arg(size.height())
                                      : QStringLiteral("拖动选区 · Esc 取消"));
    }
};
Capture::Capture(QObject *parent) : QObject(parent) {}
Capture::~Capture() {
    qDeleteAll(overlays);
}
void Capture::start() {
    snapshot = grabDesktop(desktop);
    cursor = physicalCursor();
    for (auto *screen : QGuiApplication::screens()) {
        overlays.append(new Overlay(this, screen, screenPixels(screen)));
    }
    for (auto *w : overlays) {
        w->show();
        w->raise();
    }
    if (!overlays.isEmpty())
        overlays.front()->activateWindow();
}
void Capture::move() {
    cursor = physicalCursor();
    for (auto *w : overlays)
        w->update();
}
void Capture::begin() {
    anchor = physicalCursor();
    selecting = true;
    move();
}
void Capture::end() {
    if (!selecting || finished)
        return;
    move();
    auto area = selection(anchor, cursor, desktop);
    finished = true;
    for (auto *w : overlays)
        w->hide();
    emit captured(area.width() < 5 || area.height() < 5 ? QImage{}
                                                        : snapshot.copy(area.translated(-desktop.topLeft())));
    deleteLater();
}
void Capture::cancel() {
    if (finished)
        return;
    finished = true;
    for (auto *w : overlays)
        w->hide();
    emit captured({});
    deleteLater();
}
