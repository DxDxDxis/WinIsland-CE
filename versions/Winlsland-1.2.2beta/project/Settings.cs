using System;
using System.Globalization;
using System.IO;
using System.Xml.Linq;

namespace WinIsland
{
    internal sealed class IslandSettings
    {
        internal bool Resident = true;
        internal bool HideNative = true;
        internal double DwellSeconds = 4;
        // 0 follows CompositionTarget.Rendering (the system refresh cadence); low-performance
        // systems automatically clamp that default to 60 FPS.
        internal int FrameRate = 0;
        internal event Action Changed;
        private readonly string path;
        internal IslandSettings(string path) { this.path = path; Load(); }
        internal static string DefaultPath { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WinIsland", "settings.xml"); } }
        internal static bool ParseSeconds(string text, out double seconds)
        {
            return (double.TryParse(text, NumberStyles.AllowDecimalPoint | NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite,
                CultureInfo.CurrentCulture, out seconds) || double.TryParse(text, NumberStyles.AllowDecimalPoint,
                CultureInfo.InvariantCulture, out seconds)) && !double.IsNaN(seconds) && !double.IsInfinity(seconds) && seconds >= 0.1 && seconds <= 3600;
        }
        internal static bool ParseFrameRate(string text, out int fps)
        {
            if (!int.TryParse((text ?? "").Trim(), NumberStyles.Integer, CultureInfo.CurrentCulture, out fps) &&
                !int.TryParse((text ?? "").Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out fps)) return false;
            return fps == 0 || (fps >= 30 && fps <= 240);
        }
        internal void Update(bool resident, bool hideNative, double seconds, int frameRate)
        {
            if (seconds < 0.1 || seconds > 3600 || double.IsNaN(seconds) || double.IsInfinity(seconds) || frameRate < 0 || (frameRate != 0 && (frameRate < 30 || frameRate > 240))) return;
            if (Resident == resident && HideNative == hideNative && DwellSeconds == seconds && FrameRate == frameRate) return;
            Resident = resident; HideNative = hideNative; DwellSeconds = seconds; FrameRate = frameRate;
            Save(); if (Changed != null) Changed();
        }
        internal void Update(bool resident, bool hideNative, double seconds)
        { Update(resident, hideNative, seconds, FrameRate); }
        private void Load()
        {
            try
            {
                if (path == null || !File.Exists(path)) return;
                var doc = XDocument.Load(path); var root = doc.Root;
                if (root == null || root.Name != "WinIsland") return;
                bool b; double n;
                if (bool.TryParse((string)root.Element("Resident"), out b)) Resident = b;
                if (bool.TryParse((string)root.Element("HideNative"), out b)) HideNative = b;
                if (ParseSeconds((string)root.Element("DwellSeconds"), out n)) DwellSeconds = n;
                int fps; if (ParseFrameRate((string)root.Element("FrameRate"), out fps)) FrameRate = fps;
            }
            catch (Exception ex) { Log.Error("Settings load", ex); }
        }
        private void Save()
        {
            if (path == null) return;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                string temp = path + ".tmp";
                new XDocument(new XElement("WinIsland", new XAttribute("version", AppVersion.Display),
                    new XElement("Resident", Resident), new XElement("HideNative", HideNative),
                    new XElement("DwellSeconds", DwellSeconds.ToString(CultureInfo.InvariantCulture)),
                    new XElement("FrameRate", FrameRate))).Save(temp);
                if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            }
            catch (Exception ex) { Log.Error("Settings save", ex); }
        }
    }
}
