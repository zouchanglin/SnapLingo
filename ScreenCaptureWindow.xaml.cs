using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;

namespace SnapLingo;

public partial class ScreenCaptureWindow : Window
{
    private Point _startPoint;
    private bool _isSelecting;

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
