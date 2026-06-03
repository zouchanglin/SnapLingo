using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;

namespace SnapLingo;

public partial class ScreenCaptureWindow : Window
{
    private const int MagnifierZoom = 3;
    private const int MagnifierWidth = 150;
    private const int MagnifierHeight = 93;

    private Point _startPoint;
    private bool _isSelecting;
    private BitmapSource? _screenSnapshot;

    public BitmapSource? CapturedImage { get; private set; }

    public ScreenCaptureWindow()
    {
        InitializeComponent();
        MouseLeftButtonDown += OnMouseLeftButtonDown;
        MouseMove += OnMouseMove;
        MouseLeftButtonUp += OnMouseLeftButtonUp;
        KeyDown += OnKeyDown;
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        UpdateOverlay(0, 0, 0, 0);
        CaptureScreenSnapshot();
    }

    private void CaptureScreenSnapshot()
    {
        var dpiScale = VisualTreeHelper.GetDpi(this);
        var screenW = (int)(SystemParameters.VirtualScreenWidth * dpiScale.DpiScaleX);
        var screenH = (int)(SystemParameters.VirtualScreenHeight * dpiScale.DpiScaleY);

        using var bmp = new System.Drawing.Bitmap(screenW, screenH);
        using var g = System.Drawing.Graphics.FromImage(bmp);
        g.CopyFromScreen(0, 0, 0, 0, new System.Drawing.Size(screenW, screenH));

        var bmpData = bmp.LockBits(
            new System.Drawing.Rectangle(0, 0, screenW, screenH),
            System.Drawing.Imaging.ImageLockMode.ReadOnly,
            System.Drawing.Imaging.PixelFormat.Format32bppArgb);

        _screenSnapshot = BitmapSource.Create(
            screenW, screenH,
            dpiScale.PixelsPerInchX, dpiScale.PixelsPerInchY,
            PixelFormats.Bgra32, null,
            bmpData.Scan0, screenH * bmpData.Stride, bmpData.Stride);

        bmp.UnlockBits(bmpData);
        _screenSnapshot.Freeze();
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Escape)
        {
            DialogResult = false;
            Close();
        }
    }

    private void OnMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        _startPoint = e.GetPosition(OverlayCanvas);
        _isSelecting = true;

        Canvas.SetLeft(SelectionRect, _startPoint.X);
        Canvas.SetTop(SelectionRect, _startPoint.Y);
        SelectionRect.Width = 0;
        SelectionRect.Height = 0;
        SelectionRect.Visibility = Visibility.Visible;

        OverlayCanvas.CaptureMouse();
    }

    private void OnMouseMove(object sender, MouseEventArgs e)
    {
        var pos = e.GetPosition(OverlayCanvas);
        Canvas.SetLeft(Crosshair, pos.X);
        Canvas.SetTop(Crosshair, pos.Y);

        UpdateMagnifier(pos);

        if (!_isSelecting) return;

        var x = Math.Min(_startPoint.X, pos.X);
        var y = Math.Min(_startPoint.Y, pos.Y);
        var width = Math.Abs(pos.X - _startPoint.X);
        var height = Math.Abs(pos.Y - _startPoint.Y);

        Canvas.SetLeft(SelectionRect, x);
        Canvas.SetTop(SelectionRect, y);
        SelectionRect.Width = width;
        SelectionRect.Height = height;

        UpdateOverlay(x, y, width, height);
    }

    private void UpdateMagnifier(Point pos)
    {
        if (_screenSnapshot == null) return;

        var screenW = SystemParameters.VirtualScreenWidth;
        var screenH = SystemParameters.VirtualScreenHeight;

        double magX = pos.X + 30;
        double magY = pos.Y - MagnifierHeight - 30;

        if (magY < 10) magY = pos.Y + 30;
        if (magX + MagnifierWidth + 10 > screenW) magX = pos.X - MagnifierWidth - 30;

        Canvas.SetLeft(MagnifierCanvas, magX);
        Canvas.SetTop(MagnifierCanvas, magY);

        var dpiScale = VisualTreeHelper.GetDpi(this);
        var pixelX = (int)(pos.X * dpiScale.DpiScaleX);
        var pixelY = (int)(pos.Y * dpiScale.DpiScaleY);

        var srcW = MagnifierWidth / MagnifierZoom;
        var srcH = MagnifierHeight / MagnifierZoom;
        var srcX = pixelX - srcW / 2;
        var srcY = pixelY - srcH / 2;

        srcX = Math.Clamp(srcX, 0, _screenSnapshot.PixelWidth - srcW);
        srcY = Math.Clamp(srcY, 0, _screenSnapshot.PixelHeight - srcH);

        if (srcW > _screenSnapshot.PixelWidth || srcH > _screenSnapshot.PixelHeight) return;

        var crop = new CroppedBitmap(_screenSnapshot, new Int32Rect(srcX, srcY, srcW, srcH));
        MagnifierImage.Source = crop;

        TxtMagnifierCoord.Text = $"{pixelX}, {pixelY}";
    }

    private void UpdateOverlay(double x, double y, double width, double height)
    {
        var screenWidth = SystemParameters.VirtualScreenWidth;
        var screenHeight = SystemParameters.VirtualScreenHeight;

        var fullRect = new RectangleGeometry(new Rect(0, 0, screenWidth, screenHeight));
        var holeRect = new RectangleGeometry(new Rect(x, y, width, height));

        OverlayPath.Data = new CombinedGeometry(GeometryCombineMode.Exclude, fullRect, holeRect);
    }

    private void OnMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (!_isSelecting) return;
        _isSelecting = false;
        OverlayCanvas.ReleaseMouseCapture();

        var currentPoint = e.GetPosition(OverlayCanvas);
        var x = Math.Min(_startPoint.X, currentPoint.X);
        var y = Math.Min(_startPoint.Y, currentPoint.Y);
        var width = Math.Abs(currentPoint.X - _startPoint.X);
        var height = Math.Abs(currentPoint.Y - _startPoint.Y);

        if (width < 5 || height < 5)
        {
            DialogResult = false;
            Close();
            return;
        }

        CaptureScreen(x, y, width, height);
        DialogResult = true;
        Close();
    }

    private void CaptureScreen(double x, double y, double width, double height)
    {
        var dpiScale = VisualTreeHelper.GetDpi(this);
        var scaleX = dpiScale.DpiScaleX;
        var scaleY = dpiScale.DpiScaleY;

        var pixelX = (int)(x * scaleX);
        var pixelY = (int)(y * scaleY);
        var pixelWidth = (int)(width * scaleX);
        var pixelHeight = (int)(height * scaleY);

        using var bitmap = new System.Drawing.Bitmap(pixelWidth, pixelHeight);
        using var graphics = System.Drawing.Graphics.FromImage(bitmap);
        graphics.CopyFromScreen(pixelX, pixelY, 0, 0, new System.Drawing.Size(pixelWidth, pixelHeight));

        var bitmapData = bitmap.LockBits(
            new System.Drawing.Rectangle(0, 0, pixelWidth, pixelHeight),
            System.Drawing.Imaging.ImageLockMode.ReadOnly,
            System.Drawing.Imaging.PixelFormat.Format32bppArgb);

        CapturedImage = BitmapSource.Create(
            pixelWidth, pixelHeight,
            dpiScale.PixelsPerInchX, dpiScale.PixelsPerInchY,
            PixelFormats.Bgra32,
            null,
            bitmapData.Scan0,
            pixelHeight * bitmapData.Stride,
            bitmapData.Stride);

        bitmap.UnlockBits(bitmapData);
        CapturedImage.Freeze();
    }
}
