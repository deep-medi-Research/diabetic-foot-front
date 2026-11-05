using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using i3_cs;
using System.Windows.Interop;
using System.Threading;
using System.ComponentModel;
//using System.Windows.

namespace cs_lib_test_wpf
{

    /// <summary>
    /// MainWindow.xaml에 대한 상호 작용 논리
    /// </summary>
    public partial class MainWindow : Window
    {
        private TE_A c = null;
        private TE_B teb = null;
        private bool m_run = false;
        private delegate void UpdateImageDelegate(byte[] buf, int width, int height);
        private int mColorMap;

        private float m_fTempTarget1;
        private float m_fTempTarget2;
        private float m_fTempMeasure1;
        private float m_fTempMeasure2;

        int[] camModelList = new int[] { Constants.I3_Model_TE_Q1, Constants.I3_Model_TE_V1, Constants.I3_Model_TE_Q2};
        int[] colorMapTable = new int[3];

        public void Setup()
        {
            comboBox.Items.Add("TE_Q1");
            comboBox.Items.Add("TE_V1");
            comboBox.Items.Add("TE_Q2");
            comboBox.SelectedIndex = 0;

            comboBox_colorMap.Items.Add("WhiteHot");
            comboBox_colorMap.Items.Add("Purple");
            comboBox_colorMap.Items.Add("Cyan");
            comboBox_colorMap.SelectedIndex = 0;

            colorMapTable[0] = Constants.I3_ColorMap_WhiteHot;
            colorMapTable[1] = Constants.I3_ColorMap_Purple;
            colorMapTable[2] = Constants.I3_ColorMap_Cyan;
        }

        private void UpdateImage(byte[] buf, int width, int height)
        {
            BitmapSource bmp = BitmapSource.Create(width, height, 96, 96, PixelFormats.Bgr24, null, buf, 3 * width);
            image.Source = bmp;
        }

        public MainWindow()
        {
            InitializeComponent();

            Setup();
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            Window wnd = Window.GetWindow(this);
            var wnd_helper = new WindowInteropHelper(wnd);
            IntPtr hwnd = wnd_helper.Handle;
            UsbUtils.RegisterTE(hwnd);
        }

        void Run()
        {
            while (m_run)
            {
                int width = c.GetImageWidth();
                int height = c.GetImageHeight();
                int size = width * height;
                unsafe
                {
                    ushort[] buf = new ushort[size];
                    fixed (ushort* _buf = buf)
                    {
                        if (c.RecvImage(_buf, true) == 1)
                        {
                            byte[] clr_buf = new byte[size * 3];
                            byte[] buf8 = new byte[size];
                            for (int i = 0; i < size; ++i)
                            {
                                buf8[i] = (byte)(buf[i] >> 8);
                            }
                            fixed (byte* _clr_buf = clr_buf, _buf8 = buf8)
                            {
                                ImageProc.ApplyColorMap(_clr_buf, _buf8, mColorMap, c.GetImageWidth(), c.GetImageHeight());
                                image.Dispatcher.Invoke(new UpdateImageDelegate(this.UpdateImage), clr_buf, c.GetImageWidth(), c.GetImageHeight());
                            }
                        }
                    }
                }
            }
        }

        private void Button_GetImage_Click(object sender, RoutedEventArgs e)
        {
            if (m_run)
            {
                m_run = false;
                Button_GetImage.Content = "Run";
            }
            else
            {
                Thread thrd = new Thread(new ThreadStart(Run));
                m_run = true;
                Button_GetImage.Content = "Stop";
                thrd.Start();
            }
        }

        private void button_reset_Click(object sender, RoutedEventArgs e)
        {
            if (!c.ResetMainBoard())
            {
                MessageBox.Show("reset failed");
            }
        }

        private void radioButton_temp_shutter_Click(object sender, RoutedEventArgs e)
        {
            if (!c.SetShutterMode(Constants.I3_Temp_Shutter))
            {
                MessageBox.Show("set temp mode failed");
            }
        }

        private void radioButton_time_shutter_Click(object sender, RoutedEventArgs e)
        {
            if (!c.SetShutterMode(Constants.I3_Time_Shutter))
            {
                MessageBox.Show("set time mode failed");
            }

        }

        private void button_shutter_Click(object sender, RoutedEventArgs e)
        {
            if (!c.ShutterCalibrationOn())
            {
                MessageBox.Show("calib failed");
            }
        }

        private void image_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            unsafe
            {
                if (c != null)
                {
                    int width = c.GetImageWidth();
                    int height = c.GetImageHeight();
                    ushort[] buf = new ushort[width * height];
                    fixed (ushort* _buf = buf)
                    {
                        c.CalcTemp(_buf);
                        Point pt = e.GetPosition(image);
                        int x = (int)(pt.X / image.Width * width);
                        int y = (int)(pt.Y / image.Height * height);
                        float temp = _buf[(int)(x + c.GetImageWidth() * y)];
                        temp = (temp - 5000) / 100;
                        String str = String.Format("{0}, {1}, {2} C", x, y, temp);
                        MessageBox.Show(str);
                    }
                }
                else if(teb != null)
                {
                    int width = teb.GetImageWidth();
                    int height = teb.GetImageHeight();
                    Point pt = e.GetPosition(image);
                    int x = (int)(pt.X / image.Width * width);
                    int y = (int)(pt.Y / image.Height * height);
                    float t = teb.CalcTemp(x, y);
                    String str = String.Format("{0}, {1}, {2} C", x, y, t);
                    MessageBox.Show(str);
                }
            }
        }

        private void textBox_emissivity_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (!((Key.D0 <= e.Key && e.Key <= Key.D9) || (e.Key >= Key.NumPad0 && e.Key <= Key.NumPad9) || (e.Key == Key.Back) || (e.Key == Key.Decimal)))
            {
                e.Handled = true;

            }
        }

        private void textBox_emissivity_TextChanged(object sender, TextChangedEventArgs e)
        {
            float emissivity = float.Parse(textBox_emissivity.Text);
            c.SetEmissivity(emissivity);
        }

        private void button_savesetting_Click(object sender, RoutedEventArgs e)
        {
            TE_Setting setting;
            unsafe
            {
                c.GetSetting(&setting);
                if (c.SaveSetting(&setting))
                {
                    MessageBox.Show("Setting saved");
                }
                else
                {
                    MessageBox.Show("Failed to save setting");
                }
            }
        }

        private void button_getsetting_Click(object sender, RoutedEventArgs e)
        {
            unsafe
            {
                TE_Setting setting;
                c.GetSetting(&setting);
            }
        }

        private void button_connect_Click(object sender, RoutedEventArgs e)
        {
            c = new TE_A();
            if (c.OpenTE(0))
            {
                MessageBox.Show("connected");
            }
            else
            {
                MessageBox.Show("failed to connect");
            }
        }

        private void button_close_Click(object sender, RoutedEventArgs e)
        {
            c.CloseTE();
            MessageBox.Show("Closed");
        }

        private void button_scan_Click(object sender, RoutedEventArgs e)
        {
            unsafe
            {
                SCANINFO[] scan = new SCANINFO[Constants.I3_Max_Usb_Num];
                fixed (SCANINFO* scan_buf = scan)
                {
                    if (UsbUtils.ScanTE(scan_buf) != 1)
                    {
                        MessageBox.Show("scan failed");
                    }
                    else
                    {
                        string str = "";
                        for(int i = 0; i < Constants.I3_Max_Usb_Num; ++i)
                        {
                            if(scan_buf[i].bDevCon > 0)
                                str += String.Format("{0}\n", i);
                        }
                        MessageBox.Show(str);
                    }
                }
            }
        }

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            int ret = UsbUtils.WindowProc(msg, wParam, lParam);
            if(ret == 1)
                MessageBox.Show("Device connected");
            else if(ret == 3)
                MessageBox.Show("Device removed");
            return IntPtr.Zero;
        }

        private void Window_SourceInitialized(object sender, EventArgs e)
        {
            HwndSource source = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
            source.AddHook(new HwndSourceHook(WndProc));
        }

        private void slider_shutter_temp_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            double val = slider_shutter_temp.Value;
            if (label_shutter_temp != null)
                label_shutter_temp.Content = val.ToString();
            if (c != null)
                c.SetShutterTemperature((ushort)val);
        }

        private void slider_shutter_time_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            double val = slider_shutter_time.Value;
            if(label_shutter_time != null)
                label_shutter_time.Content = val.ToString();
            if(c != null)
                c.SetShutterTime((ushort)val);
        }

        private void button_getPt100_Click(object sender, RoutedEventArgs e)
        {
            if (c != null)
            {
                float val = c.GetShutterPt100Temp();
                String str = String.Format("PT100 Value : {0}", val);
                MessageBox.Show(str);
            }

        }

        private void comboBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {

        }

        private void button_open_b_Click(object sender, RoutedEventArgs e)
        {
            if (teb == null)
            {
                teb = new TE_B();
            }
            if (teb.OpenTE(camModelList[comboBox.SelectedIndex], 0))
            {
                MessageBox.Show("Device Connected");
            }
            else
            {
                MessageBox.Show("Failed to open device");
            }
        }

        private void button_close_b_Click(object sender, RoutedEventArgs e)
        {
            if(teb != null)
            {
                teb.CloseTE();
                teb = null;
            }
        }

        private void button_run_b_Click(object sender, RoutedEventArgs e)
        {
            if (m_run)
            {
                m_run = false;
                button_run_b.Content = "Run";
            }
            else
            {
                Thread thrd = new Thread(new ThreadStart(Run_b));
                m_run = true;
                button_run_b.Content = "Stop";
                thrd.Start();
            }
        }

        void Run_b()
        {
            while (m_run)
            {
                int width = teb.GetImageWidth();
                int height = teb.GetImageHeight();
                int size = width * height;
                unsafe
                {
                    ushort[] buf = new ushort[size];
                    fixed (ushort* _buf = buf)
                    {
                        if (teb.RecvImage(_buf) == 1)
                        {

                            byte[] clr_buf = new byte[size * 3];
                            byte[] buf8 = new byte[size];
                            for (int i = 0; i < size; ++i)
                            {
                                buf8[i] = (byte)(buf[i] >> 8);
                            }
                            fixed (byte* _clr_buf = clr_buf, _buf8 = buf8)
                            {
                                ImageProc.ApplyColorMap(_clr_buf, _buf8, mColorMap, teb.GetImageWidth(), teb.GetImageHeight());
                                image.Dispatcher.Invoke(new UpdateImageDelegate(this.UpdateImage), clr_buf, teb.GetImageWidth(), teb.GetImageHeight());

                            }
                        }
                    }
                }
            }
        }

        private void button_calib_b_Click(object sender, RoutedEventArgs e)
        {
            if (!teb.ShutterCalibrationOn())
            {
                MessageBox.Show("Calibration Failed");
            }
        }

        private void button_update_dead_b_Click(object sender, RoutedEventArgs e)
        {
            if (teb.UpdateDead() != 1)
            {
                MessageBox.Show("Failed to update bad pixels");
            }
        }

        private void comboBox_colorMap_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            mColorMap = colorMapTable[comboBox_colorMap.SelectedIndex];
        }

        private void button_save_cal_b_Click(object sender, RoutedEventArgs e)
        {
            byte[] fname = Encoding.ASCII.GetBytes("test");
            //MessageBox.Show(fname);
            unsafe
            {
                fixed(byte *s = fname){
                    sbyte* path = (sbyte*)s;
                    c.LensShadingCorrection();
                    if (!c.SaveCalibration(path))
                    {
                        MessageBox.Show("Failed");
                    }
                }
            }
        }

        private void button_load_cal_b_Click(object sender, RoutedEventArgs e)
        {
            byte[] fname = Encoding.ASCII.GetBytes("test");
            unsafe
            {
                fixed (byte* s = fname)
                {
                    sbyte* path = (sbyte*)s;
                    if (!c.LoadCalibration(path)) { 
                        MessageBox.Show("Failed");
                    }
                }
            }
        }

        // Update Temperature Calibration Data
        private void button_update_temp_data_Click(object sender, RoutedEventArgs e)
        {
            c.UpdateTempData(m_fTempTarget1, m_fTempMeasure1, m_fTempTarget2, m_fTempMeasure2);
        }   

        private void textBox_tempTarget1_TextChanged(object sender, TextChangedEventArgs e)
        {
            float t;
            if (float.TryParse(textBox_tempTarget1.Text, out t)){
                m_fTempTarget1 = t;
            }
        }

        private void textBox_tempTarget2_TextChanged(object sender, TextChangedEventArgs e)
        {
            float t;
            if (float.TryParse(textBox_tempTarget2.Text, out t)) {
                m_fTempTarget2 = t;
            }
        }

        private void textBox_tempMeasure1_TextChanged(object sender, TextChangedEventArgs e)
        {
            float t;
            if(float.TryParse(textBox_tempMeasure1.Text, out t))
            {
                m_fTempMeasure1 = t;
            }
        }

        private void textBox_tempMeasure2_TextChanged(object sender, TextChangedEventArgs e)
        {
            float t;
            if(float.TryParse(textBox_tempMeasure2.Text, out t)){
                m_fTempMeasure2 = float.Parse(textBox_tempMeasure2.Text);
            }
        }
    }
}
