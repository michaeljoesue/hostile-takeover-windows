// GameMenu - connects the M editor to the Windows version of Hostile Takeover.
//
// Adds a "Game" menu to the editor:
//
//   Play Level in Game (F5)        Exports the active level into a private
//                                  "Editor Test" mission pack, installs it and
//                                  starts the game straight into the level.
//   Install Mission Pack in Game   Exports all open levels as a named mission
//                                  pack and installs it, so it shows up in the
//                                  game's Add-On mission list.
//   Open Built-in Mission...       Opens one of the game's own story (S_*),
//                                  challenge (C_*) or multiplayer (M_*) levels
//                                  from the Author Kit.
//   Rebuild Built-in Missions      Rebuilds the game's data file from the
//                                  Author Kit (so edits to built-in levels,
//                                  art, text etc. take effect) and installs it.
//   Restore Original Missions      Puts back the game data file that shipped.
//   Game Location...               Chooses HostileTakeover.exe.
//
// Installed packs go where the game looks for downloaded packs:
// %APPDATA%\HostileTakeover\MissionPacks\<id>-<md5>, with a small JSON file of
// the same name in MissionPackInfos holding the pack's title. If the game is
// in portable mode (portable.txt next to the exe) they go in its UserData
// folder instead.

using System;
using System.Collections;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using SpiffLib;

namespace m {

public class GameMenu {
    const string kszGameExe = "HostileTakeover.exe";
    const string kszDataFile = "htdata832.pdb";
    const string kszDataBackup = "htdata832.original.pdb";
    const uint kidTestPack = 0x7e570001;    // "Editor Test" pack

    Form m_frmMain;
    LevelDoc.ValidateErrorDelegate m_dgtValidate;
    string m_strSettingsFile;
    string m_strGameExe;
    string m_strStartDir;
    string m_strAuthor;

    public static void Install(Form frmMain, MainMenu menu, LevelDoc.ValidateErrorDelegate dgtValidate) {
        new GameMenu(frmMain, menu, dgtValidate);
    }

    GameMenu(Form frmMain, MainMenu menu, LevelDoc.ValidateErrorDelegate dgtValidate) {
        m_frmMain = frmMain;
        m_dgtValidate = dgtValidate;
        m_strStartDir = Environment.CurrentDirectory;
        m_strSettingsFile = Path.Combine(EditorDir, "game.ini");
        LoadSettings();

        MenuItem mniGame = new MenuItem("&Game");
        MenuItem mniPlay = new MenuItem("&Play Level in Game", new EventHandler(OnPlay), Shortcut.F5);
        MenuItem mniInstall = new MenuItem("&Install Mission Pack in Game...", new EventHandler(OnInstallPack));
        MenuItem mniOpenBuiltIn = new MenuItem("&Open Built-in Mission...", new EventHandler(OnOpenBuiltIn));
        MenuItem mniRebuild = new MenuItem("&Rebuild Built-in Missions", new EventHandler(OnRebuild));
        MenuItem mniRestore = new MenuItem("Res&tore Original Missions", new EventHandler(OnRestore));
        MenuItem mniLocation = new MenuItem("Game &Location...", new EventHandler(OnLocation));
        mniGame.MenuItems.AddRange(new MenuItem[] {
            mniPlay, mniInstall, new MenuItem("-"),
            mniOpenBuiltIn, mniRebuild, mniRestore, new MenuItem("-"),
            mniLocation });

        // Put it before Window / Help
        int i = menu.MenuItems.Count;
        for (int n = 0; n < menu.MenuItems.Count; n++) {
            string strText = menu.MenuItems[n].Text.Replace("&", "");
            if (strText == "Window" || strText == "Help") {
                i = n;
                break;
            }
        }
        menu.MenuItems.Add(i, mniGame);

        // Open file dialogs start in the Author Kit's level folder
        if (Directory.Exists(AuthorKitDataDir)) {
            try {
                Environment.CurrentDirectory = AuthorKitDataDir;
            } catch {
            }
        }

        // Levels named on the command line (or dropped onto M.exe) are
        // opened once the main window is up.
        m_frmMain.Load += new EventHandler(OnMainLoad);
    }

    void OnMainLoad(object sender, EventArgs e) {
        string[] astrArgs = Environment.GetCommandLineArgs();
        for (int n = 1; n < astrArgs.Length; n++) {
            string strFile = astrArgs[n];
            if (!strFile.ToLowerInvariant().EndsWith(".ld"))
                continue;
            try {
                strFile = Path.GetFullPath(Path.Combine(m_strStartDir, strFile));
                if (File.Exists(strFile))
                    DocManager.OpenDocument(strFile);
            } catch (Exception ex) {
                MessageBox.Show(m_frmMain, "Couldn't open " + strFile + ":\n\n" + ex.Message, "M");
            }
        }
    }

    // ---- locations ----------------------------------------------------

    static string EditorDir {
        get { return Path.GetDirectoryName(Application.ExecutablePath); }
    }

    static string AuthorKitDir {
        get { return Path.Combine(EditorDir, "AuthorKit"); }
    }

    static string AuthorKitDataDir {
        get { return Path.Combine(AuthorKitDir, "data"); }
    }

    string GameExe {
        get {
            if (m_strGameExe != null && File.Exists(m_strGameExe))
                return m_strGameExe;

            // The editor normally lives in an "Editor" folder inside the game folder
            string[] astrCandidates = {
                Path.Combine(Path.GetDirectoryName(EditorDir), kszGameExe),
                Path.Combine(EditorDir, kszGameExe),
            };
            foreach (string str in astrCandidates) {
                if (File.Exists(str))
                    return str;
            }
            return null;
        }
    }

    string GameDir {
        get { return Path.GetDirectoryName(GameExe); }
    }

    // Must match HostHelpers::Init in game/sdl/win/hosthelpers.cpp
    string GameUserDir {
        get {
            if (File.Exists(Path.Combine(GameDir, "portable.txt")))
                return Path.Combine(GameDir, "UserData");
            string strAppData = Environment.GetEnvironmentVariable("APPDATA");
            if (strAppData == null || strAppData.Length == 0)
                return Path.Combine(GameDir, "UserData");
            return Path.Combine(strAppData, "HostileTakeover");
        }
    }

    void LoadSettings() {
        try {
            if (File.Exists(m_strSettingsFile)) {
                Ini ini = new Ini(m_strSettingsFile);
                Ini.Section sec = ini["Game"];
                if (sec["Path"] != null)
                    m_strGameExe = sec["Path"].Value;
                if (sec["Author"] != null)
                    m_strAuthor = sec["Author"].Value;
            }
        } catch {
            m_strGameExe = null;
        }
    }

    void SaveSettings() {
        try {
            Ini ini = new Ini();
            Ini.Section sec = new Ini.Section("Game");
            if (m_strGameExe != null)
                sec.Add(new Ini.Property("Path", m_strGameExe));
            if (m_strAuthor != null)
                sec.Add(new Ini.Property("Author", m_strAuthor));
            ini.Add(sec);
            ini.Save(m_strSettingsFile);
        } catch {
        }
    }

    bool EnsureGame() {
        if (GameExe != null)
            return true;
        MessageBox.Show(m_frmMain, "Please show the editor where " + kszGameExe + " is.",
                "Hostile Takeover not found");
        return ChooseGame();
    }

    bool ChooseGame() {
        OpenFileDialog ofd = new OpenFileDialog();
        ofd.Title = "Where is Hostile Takeover?";
        ofd.Filter = "Hostile Takeover (" + kszGameExe + ")|" + kszGameExe + "|Programs (*.exe)|*.exe";
        ofd.FileName = kszGameExe;
        if (m_strGameExe != null)
            ofd.InitialDirectory = Path.GetDirectoryName(m_strGameExe);
        if (ofd.ShowDialog(m_frmMain) != DialogResult.OK)
            return false;
        m_strGameExe = ofd.FileName;
        SaveSettings();
        return true;
    }

    static bool IsGameRunning() {
        return Process.GetProcessesByName(Path.GetFileNameWithoutExtension(kszGameExe)).Length != 0;
    }

    bool EnsureGameClosed(string strWhy) {
        while (IsGameRunning()) {
            DialogResult dr = MessageBox.Show(m_frmMain,
                    "Hostile Takeover is running. Please close it " + strWhy + ", then click Retry.",
                    "Hostile Takeover is running", MessageBoxButtons.RetryCancel, MessageBoxIcon.Information);
            if (dr != DialogResult.Retry)
                return false;
        }
        return true;
    }

    // ---- level helpers ------------------------------------------------

    LevelDoc[] OpenLevels() {
        LevelDocTemplate doct = (LevelDocTemplate)DocManager.FindDocTemplate(typeof(LevelDoc));
        Document[] adoc = doct.GetDocuments();
        LevelDoc[] alvld = new LevelDoc[adoc.Length];
        for (int n = 0; n < adoc.Length; n++)
            alvld[n] = (LevelDoc)adoc[n];
        return alvld;
    }

    // Validation errors that only say a unit, building or Galaxite sits on
    // terrain the editor thinks is blocked. The game's own missions have many
    // of these (their terrain maps are identical to what shipped) and they
    // play fine, so they're reported as warnings. Anything else - items out
    // of bounds, too many switches or areas, bad sides - is a real error.
    static bool IsPlacementWarning(string str) {
        return str != null && (str.IndexOf(" is on top of: ") >= 0 ||
                str.IndexOf("is inaccessible due to structure blockage") >= 0);
    }

    int m_cErrors;
    int m_cWarnings;

    void ValidateCallback(LevelDoc lvld, LevelDoc.ValidateError ve, int tx, int ty, object ob, string str) {
        if (ve == LevelDoc.ValidateError.Error) {
            if (IsPlacementWarning(str))
                m_cWarnings++;
            else
                m_cErrors++;
        }
        m_dgtValidate(lvld, ve, tx, ty, ob, str);
    }

    bool ValidateLevels(LevelDoc[] alvld) {
        LevelDoc lvldActive = (LevelDoc)DocManager.GetActiveDocument(typeof(LevelDoc));
        OutputForm.HideIt();
        OutputForm.Clear();
        try {
            foreach (LevelDoc lvld in alvld) {
                // Validation and saving rely on which document is active
                DocManager.SetActiveDocument(typeof(LevelDoc), lvld);
                m_cErrors = 0;
                m_cWarnings = 0;
                lvld.Validate(new LevelDoc.ValidateErrorDelegate(ValidateCallback));
                if (m_cErrors != 0) {
                    // Errors can make the game misbehave, but a few of the
                    // game's own missions have some (e.g. scenery partly off
                    // the map) and play fine, so let the author decide.
                    DialogResult dr = MessageBox.Show(m_frmMain,
                            lvld.GetName() + " has " + m_cErrors + " error" + (m_cErrors == 1 ? "" : "s") +
                            (m_cWarnings != 0 ? " and " + m_cWarnings + " placement warning" +
                                (m_cWarnings == 1 ? "" : "s") : "") +
                            " (listed in the output window).\n\n" +
                            "Errors can make the level misbehave or crash the game. " +
                            "It's best to fix them first.\n\nContinue anyway?",
                            "Level has errors", MessageBoxButtons.YesNo, MessageBoxIcon.Warning,
                            MessageBoxDefaultButton.Button2);
                    if (dr != DialogResult.Yes)
                        return false;
                } else if (m_cWarnings != 0) {
                    DialogResult dr = MessageBox.Show(m_frmMain,
                            lvld.GetName() + " has " + m_cWarnings + " placement warning" +
                            (m_cWarnings == 1 ? "" : "s") + " (listed in the output window): " +
                            "things standing on terrain the editor thinks is blocked.\n\n" +
                            "The game's own missions have warnings like these and play fine. Continue?",
                            "Placement warnings", MessageBoxButtons.YesNo, MessageBoxIcon.Information);
                    if (dr != DialogResult.Yes)
                        return false;
                }
            }
        } finally {
            if (lvldActive != null)
                DocManager.SetActiveDocument(typeof(LevelDoc), lvldActive);
        }
        return true;
    }

    // Exports levels as an expansion .pdb and installs it under packid.
    void InstallPack(LevelDoc[] alvld, uint id, string strTitle, string strAuthor) {
        string strTemp = Path.Combine(Path.GetTempPath(), "ht-pack-" + id.ToString("x8") + ".pdb");
        OutputTools.SaveExpansionPdb(strTemp, alvld, "1.1");

        byte[] abHash;
        using (MD5 md5 = MD5.Create()) {
            using (FileStream stm = File.OpenRead(strTemp))
                abHash = md5.ComputeHash(stm);
        }
        StringBuilder sb = new StringBuilder();
        foreach (byte b in abHash)
            sb.Append(b.ToString("x2"));
        string strName = id.ToString("x8") + "-" + sb.ToString();

        string strPacks = Path.Combine(GameUserDir, "MissionPacks");
        string strInfos = Path.Combine(GameUserDir, "MissionPackInfos");
        Directory.CreateDirectory(strPacks);
        Directory.CreateDirectory(strInfos);

        // Replace any earlier version of this pack
        foreach (string strDir in new string[] { strPacks, strInfos }) {
            foreach (string strFile in Directory.GetFiles(strDir, id.ToString("x8") + "-*")) {
                try {
                    File.Delete(strFile);
                } catch {
                }
            }
        }

        File.Copy(strTemp, Path.Combine(strPacks, strName), true);
        File.Delete(strTemp);
        File.WriteAllText(Path.Combine(strInfos, strName), "{\"t\":\"" + JsonEscape(strTitle) + "\",\"a\":\"" + JsonEscape(strAuthor) + "\"}",
                new UTF8Encoding(false));
    }

    static string JsonEscape(string str) {
        StringBuilder sb = new StringBuilder();
        foreach (char ch in str) {
            if (ch == '"' || ch == '\\')
                sb.Append('\\').Append(ch);
            else if (ch < ' ' || ch > '~')
                sb.Append("\\u").Append(((int)ch).ToString("x4"));
            else
                sb.Append(ch);
        }
        return sb.ToString();
    }

    // Stable pack id for a title, so reinstalling a pack replaces it.
    static uint PackIdFromTitle(string strTitle) {
        byte[] ab;
        using (MD5 md5 = MD5.Create())
            ab = md5.ComputeHash(Encoding.UTF8.GetBytes(strTitle.Trim().ToLowerInvariant()));
        uint id = (uint)(ab[0] << 24 | ab[1] << 16 | ab[2] << 8 | ab[3]);
        // Keep clear of the main game data id (0xffffffff), the test pack and
        // small ids that a mission pack server might hand out.
        id = 0x40000000 | (id & 0x3fffffff);
        if (id == kidTestPack)
            id++;
        return id;
    }

    // ---- commands -----------------------------------------------------

    void OnPlay(object sender, EventArgs e) {
        LevelDoc lvld = (LevelDoc)DocManager.GetActiveDocument(typeof(LevelDoc));
        if (lvld == null) {
            MessageBox.Show(m_frmMain, "Open a level first.", "Play Level in Game");
            return;
        }
        if (lvld.MaxPlayers > 1) {
            MessageBox.Show(m_frmMain, "This is a multiplayer level (" + lvld.MaxPlayers +
                    " players). Play Level in Game runs single player levels.\n\n" +
                    "To try it, use Install Mission Pack in Game and pick it from the game's multiplayer missions.",
                    "Play Level in Game");
            return;
        }
        if (!EnsureGame() || !EnsureGameClosed("so the level can be loaded"))
            return;
        if (!ValidateLevels(new LevelDoc[] { lvld }))
            return;

        try {
            Cursor.Current = Cursors.WaitCursor;
            InstallPack(new LevelDoc[] { lvld }, kidTestPack, "Editor Test", "the M editor");
        } catch (Exception ex) {
            MessageBox.Show(m_frmMain, "Couldn't export the level:\n\n" + ex.Message, "Play Level in Game");
            return;
        } finally {
            Cursor.Current = Cursors.Default;
        }
        LaunchGame("-pack " + kidTestPack.ToString("x8"));
    }

    void OnInstallPack(object sender, EventArgs e) {
        LevelDoc[] alvld = OpenLevels();
        if (alvld.Length == 0) {
            MessageBox.Show(m_frmMain, "Open the levels you want in the pack first.", "Install Mission Pack");
            return;
        }
        if (!EnsureGame())
            return;

        string strDefault = alvld[0].Title;
        string strTitle = EditStringForm.DoModal("Install Mission Pack in Game",
                "Mission pack title (" + alvld.Length + (alvld.Length == 1 ? " level" : " levels") +
                "; installing again with the same title replaces it):", strDefault);
        if (strTitle == null || strTitle.Trim().Length == 0)
            return;
        string strAuthor = EditStringForm.DoModal("Install Mission Pack in Game",
                "Author (shown in the game as \"" + strTitle.Trim() + " by ...\"):",
                m_strAuthor != null ? m_strAuthor : "");
        if (strAuthor == null)
            return;
        m_strAuthor = strAuthor.Trim();
        SaveSettings();
        if (!EnsureGameClosed("so it notices the new pack"))
            return;
        if (!ValidateLevels(alvld))
            return;

        try {
            Cursor.Current = Cursors.WaitCursor;
            InstallPack(alvld, PackIdFromTitle(strTitle), strTitle.Trim(), m_strAuthor);
        } catch (Exception ex) {
            MessageBox.Show(m_frmMain, "Couldn't export the mission pack:\n\n" + ex.Message, "Install Mission Pack");
            return;
        } finally {
            Cursor.Current = Cursors.Default;
        }

        DialogResult dr = MessageBox.Show(m_frmMain, "\"" + strTitle.Trim() + "\" is installed.\n\n" +
                "In the game choose Play, then Single Player (or Multiplayer for multiplayer levels), " +
                "then the Add-On tab.\n\nStart the game now?", "Mission pack installed",
                MessageBoxButtons.YesNo, MessageBoxIcon.Information);
        if (dr == DialogResult.Yes)
            LaunchGame("");
    }

    void OnOpenBuiltIn(object sender, EventArgs e) {
        if (!Directory.Exists(AuthorKitDataDir)) {
            MessageBox.Show(m_frmMain, "The Author Kit folder is missing:\n" + AuthorKitDataDir, "Open Built-in Mission");
            return;
        }
        OpenFileDialog ofd = new OpenFileDialog();
        ofd.Title = "Open Built-in Mission";
        ofd.InitialDirectory = AuthorKitDataDir;
        ofd.Filter = "Story missions (S_*.ld)|S_*.ld|Challenge missions (C_*.ld)|C_*.ld|" +
                "Multiplayer missions (M_*.ld)|M_*.ld|All levels (*.ld)|*.ld";
        if (ofd.ShowDialog(m_frmMain) != DialogResult.OK)
            return;
        DocManager.OpenDocument(ofd.FileName);
    }

    void OnRebuild(object sender, EventArgs e) {
        string strBat = Path.Combine(AuthorKitDataDir, "build-data.bat");
        if (!File.Exists(strBat)) {
            MessageBox.Show(m_frmMain, "Can't find " + strBat, "Rebuild Built-in Missions");
            return;
        }
        if (!EnsureGame())
            return;

        // Save edited built-in levels first so the build sees them
        foreach (LevelDoc lvld in OpenLevels()) {
            string strPath = lvld.GetPath();
            if (lvld.IsModified() && strPath != null &&
                    Path.GetFullPath(Path.GetDirectoryName(strPath)).TrimEnd('\\', '/').ToLowerInvariant() ==
                    Path.GetFullPath(AuthorKitDataDir).TrimEnd('\\', '/').ToLowerInvariant()) {
                lvld.Save();
            }
        }

        BuildForm frm = new BuildForm(strBat, AuthorKitDataDir);
        frm.ShowDialog(m_frmMain);
        if (!frm.Succeeded)
            return;

        if (!EnsureGameClosed("so its data can be updated"))
            return;
        string strBuilt = Path.Combine(Path.Combine(AuthorKitDir, "game"), kszDataFile);
        string strDst = Path.Combine(GameDir, kszDataFile);
        string strBackup = Path.Combine(GameDir, kszDataBackup);
        try {
            if (!File.Exists(strBackup) && File.Exists(strDst))
                File.Copy(strDst, strBackup);
            File.Copy(strBuilt, strDst, true);
        } catch (Exception ex) {
            MessageBox.Show(m_frmMain, "The data built, but it couldn't be copied into the game:\n\n" + ex.Message,
                    "Rebuild Built-in Missions");
            return;
        }
        DialogResult dr = MessageBox.Show(m_frmMain, "The game now uses your rebuilt missions.\n" +
                "(The original data is saved as " + kszDataBackup + "; use Restore Original Missions to go back.)\n\n" +
                "Start the game now?", "Built-in missions rebuilt", MessageBoxButtons.YesNo, MessageBoxIcon.Information);
        if (dr == DialogResult.Yes)
            LaunchGame("");
    }

    void OnRestore(object sender, EventArgs e) {
        if (!EnsureGame())
            return;
        string strBackup = Path.Combine(GameDir, kszDataBackup);
        if (!File.Exists(strBackup)) {
            MessageBox.Show(m_frmMain, "The game is already using its original missions.", "Restore Original Missions");
            return;
        }
        if (!EnsureGameClosed("so its data can be restored"))
            return;
        try {
            File.Copy(strBackup, Path.Combine(GameDir, kszDataFile), true);
            File.Delete(strBackup);
        } catch (Exception ex) {
            MessageBox.Show(m_frmMain, "Couldn't restore the original data:\n\n" + ex.Message, "Restore Original Missions");
            return;
        }
        MessageBox.Show(m_frmMain, "The original built-in missions are back.", "Restore Original Missions");
    }

    void OnLocation(object sender, EventArgs e) {
        ChooseGame();
    }

    void LaunchGame(string strArgs) {
        try {
            ProcessStartInfo psi = new ProcessStartInfo(GameExe, strArgs);
            psi.WorkingDirectory = GameDir;
            psi.UseShellExecute = false;
            Process.Start(psi);
        } catch (Exception ex) {
            MessageBox.Show(m_frmMain, "Couldn't start the game:\n\n" + ex.Message, "Hostile Takeover");
        }
    }
}

// Runs build-data.bat and shows its output.
class BuildForm : Form {
    TextBox m_tbx;
    Button m_btn;
    Process m_prc;
    string m_strBat;
    string m_strDir;
    bool m_fSucceeded;
    bool m_fDone;

    public bool Succeeded {
        get { return m_fSucceeded; }
    }

    public BuildForm(string strBat, string strDir) {
        m_strBat = strBat;
        m_strDir = strDir;
        Text = "Rebuilding Built-in Missions";
        Size = new Size(760, 480);
        StartPosition = FormStartPosition.CenterParent;
        MinimizeBox = false;

        m_tbx = new TextBox();
        m_tbx.Multiline = true;
        m_tbx.ReadOnly = true;
        m_tbx.ScrollBars = ScrollBars.Both;
        m_tbx.WordWrap = false;
        m_tbx.Font = new Font(FontFamily.GenericMonospace, 8.5f);
        m_tbx.Dock = DockStyle.Fill;

        m_btn = new Button();
        m_btn.Text = "Cancel";
        m_btn.Dock = DockStyle.Bottom;
        m_btn.Height = 30;
        m_btn.Click += new EventHandler(OnButton);

        Controls.Add(m_tbx);
        Controls.Add(m_btn);
        Load += new EventHandler(OnLoad);
        FormClosing += new FormClosingEventHandler(OnClosing);
    }

    void OnLoad(object sender, EventArgs e) {
        Append("Building the game data. The first build takes a few minutes; later builds only redo what changed.\r\n\r\n");
        ProcessStartInfo psi = new ProcessStartInfo("cmd.exe", "/c \"\"" + m_strBat + "\"\"");
        psi.WorkingDirectory = m_strDir;
        psi.UseShellExecute = false;
        psi.CreateNoWindow = true;
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        m_prc = new Process();
        m_prc.StartInfo = psi;
        m_prc.EnableRaisingEvents = true;
        m_prc.OutputDataReceived += new DataReceivedEventHandler(OnData);
        m_prc.ErrorDataReceived += new DataReceivedEventHandler(OnData);
        m_prc.Exited += new EventHandler(OnExited);
        try {
            m_prc.Start();
            m_prc.BeginOutputReadLine();
            m_prc.BeginErrorReadLine();
        } catch (Exception ex) {
            Append("Couldn't start the build: " + ex.Message + "\r\n");
            Finish(false);
        }
    }

    void OnData(object sender, DataReceivedEventArgs e) {
        if (e.Data != null)
            BeginInvoke(new AppendDelegate(Append), new object[] { e.Data + "\r\n" });
    }

    delegate void AppendDelegate(string str);
    delegate void FinishDelegate(bool fSuccess);

    void Append(string str) {
        m_tbx.AppendText(str);
    }

    void OnExited(object sender, EventArgs e) {
        // Let the output readers drain
        m_prc.WaitForExit();
        BeginInvoke(new FinishDelegate(Finish), new object[] { m_prc.ExitCode == 0 });
    }

    void Finish(bool fSuccess) {
        m_fDone = true;
        m_fSucceeded = fSuccess;
        Append(fSuccess ? "\r\nFinished.\r\n" : "\r\nThe build failed; see the messages above.\r\n");
        m_btn.Text = fSuccess ? "Continue" : "Close";
    }

    void OnButton(object sender, EventArgs e) {
        Close();
    }

    void OnClosing(object sender, FormClosingEventArgs e) {
        if (m_fDone)
            return;
        if (MessageBox.Show(this, "Stop the build?", Text, MessageBoxButtons.YesNo) != DialogResult.Yes) {
            e.Cancel = true;
            return;
        }
        try {
            // Stop cmd and everything it started
            Process.Start(new ProcessStartInfo("taskkill", "/T /F /PID " + m_prc.Id) {
                CreateNoWindow = true, UseShellExecute = false });
        } catch {
        }
        m_fSucceeded = false;
    }
}

}
