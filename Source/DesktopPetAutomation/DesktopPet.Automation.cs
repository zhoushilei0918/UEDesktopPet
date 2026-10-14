using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using AutomationTool;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using UnrealBuildTool;

/// <summary>UE 标准 Stage 扩展：只修改本次桌宠游戏的暂存 EXE，随后由 UE 原生 Archive 复制。</summary>
public sealed class DesktopPetNamingStage : CustomStagingHandler
{
    private string ApplicationName = "";
    private bool HasCustomName;
    protected override bool TryInitialize(ProjectParams Params, DeploymentContext SC)
    {
        // 自动化程序集可能被 UAT 跨工程发现；必须依据本次构建收据确认插件实际启用。
        if (!OperatingSystem.IsWindows() || SC.StageTargetPlatform.PlatformType != UnrealTargetPlatform.Win64 ||
            SC.DedicatedServer ||
            !SC.StageTargets.Any(
                t => t.Receipt.RuntimeDependencies.Any(
                    d => d.Path.GetFileName().Equals("DesktopPet.uplugin", StringComparison.OrdinalIgnoreCase))))
            return false;
        var EditorConfig =
            ConfigCache.ReadHierarchy(ConfigHierarchyType.Editor, SC.ProjectRoot, UnrealTargetPlatform.Win64);
        EditorConfig.GetString(
            "/Script/DesktopPetEditor.DesktopPetPackagingSettings", "PetApplicationName", out string CustomName);
        ApplicationName = (CustomName ?? "").Trim();
        HasCustomName = ApplicationName.Length != 0;
        if (ApplicationName.Length == 0)
        {
            // 默认名称来自 UE 自己的项目配置；不从上一次修改过的 EXE 读取，清空后可正常恢复默认。
            var GameConfig =
                ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, SC.ProjectRoot, UnrealTargetPlatform.Win64);
            GameConfig.GetString(
                "/Script/EngineSettings.GeneralProjectSettings", "ProjectName", out string ProjectName);
            ApplicationName = string.IsNullOrWhiteSpace(ProjectName) ? SC.ShortProjectName : ProjectName.Trim();
        }
        if (ApplicationName.Length > 128 || ApplicationName.Any(char.IsControl))
            throw new AutomationException("DesktopPet：程序名称必须是 1–128 个字符的单行文本。");
        if (HasCustomName &&
            (ApplicationName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || ApplicationName.EndsWith('.') ||
             System.Text.RegularExpressions.Regex.IsMatch(ApplicationName,
                                                          @"^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)",
                                                          System.Text.RegularExpressions.RegexOptions.IgnoreCase)))
            throw new AutomationException(
                "DesktopPet：名称必须是有效的 Windows 文件名，不能包含路径符号、保留设备名或结尾句点。");
        return true;
    }

    public override void GetFilesToStage(ProjectParams Params, DeploymentContext SC)
    {
        if (!HasCustomName)
            return; // 留空时保留 UE 原生 EXE 文件名。
        var GamePaths = SC.FilesToStage.NonUFSFiles.Keys.Select(P => P.Name.Replace('\\', '/'))
                            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var Bootstraps = SC.FilesToStage.NonUFSSystemFiles
                             .Where(P => P.Key.Name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                             .Where(P =>
                                    {
                                        string Child = PetExeResources.ReadBootstrapTarget(P.Value.FullName);
                                        return Child != null && GamePaths.Contains(Child.Replace('\\', '/'));
                                    })
                             .ToList();
        if (Bootstraps.Count == 0)
            return; // 项目禁用 Bootstrap 时不制造额外启动器。
        if (Bootstraps.Count != 1)
            throw new AutomationException(
                "DesktopPet：自定义入口名称时请每次只打包一个游戏目标/配置，避免同名 EXE 冲突。");
        var Old = Bootstraps[0];
        var New = new StagedFileReference(ApplicationName + ".exe");
        if (!New.Equals(Old.Key) &&
            (SC.FilesToStage.NonUFSSystemFiles.ContainsKey(New) || SC.FilesToStage.NonUFSFiles.ContainsKey(New) ||
             SC.FilesToStage.UFSFiles.ContainsKey(New)))
            throw new AutomationException("DesktopPet：打包名称与已有文件冲突：{0}", New.Name);
        // 修改 UE 的 Stage 清单，而非打包完成后只改磁盘文件，Archive 和清单会使用同一个文件名。
        SC.FilesToStage.NonUFSSystemFiles.Remove(Old.Key);
        SC.FilesToStage.NonUFSSystemFiles.Add(New, Old.Value);
        CommandUtils.Logger.LogInformation("DesktopPet packaged entry: {Entry}", New.Name);
    }

    public override void PostStagingFileCopy(ProjectParams Params, DeploymentContext SC)
    {
        string Root = Path.GetFullPath(SC.StageDirectory.FullName);
        var Executables = SC.StageTargets.SelectMany(t => t.Receipt.BuildProducts)
                              .Where(p => p.Type == BuildProductType.Executable)
                              .Select(p => p.Path)
                              .ToHashSet();
        // 用构建收据和 Stage 清单精确找实际游戏，不扫描或修改 CrashReportClient、安装器等程序。
        var Games = SC.FilesToStage.NonUFSFiles.Where(p => Executables.Contains(p.Value))
                        .Select(p => Path.GetFullPath(Path.Combine(Root, p.Key.Name)))
                        .ToHashSet(StringComparer.OrdinalIgnoreCase);
        if (Games.Count == 0)
            throw new AutomationException("DesktopPet：暂存清单中没有找到游戏 EXE，无法自动设置名称。");
        var Plans = Games.Select(p => new PetExeResources.Plan(p, ApplicationName, ApplicationName)).ToList();
        foreach (var Pair in SC.FilesToStage.NonUFSSystemFiles)
        {
            if (!Pair.Key.Name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                continue;
            string PathInStage = Path.GetFullPath(Path.Combine(Root, Pair.Key.Name));
            string Child = PetExeResources.ReadBootstrapTarget(PathInStage);
            if (Child == null)
                continue;
            string Target = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(PathInStage), Child));
            if (Games.Contains(Target))
                Plans.Add(new PetExeResources.Plan(PathInStage, ApplicationName + "Game", ApplicationName));
        }
        foreach (var Plan in Plans)
            if (!Plan.Path.StartsWith(Root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar,
                                      StringComparison.OrdinalIgnoreCase))
                throw new AutomationException("DesktopPet：拒绝修改暂存目录以外的文件：{0}", Plan.Path);
        PetExeResources.ApplyAll(Plans, Path.Combine(SC.ProjectRoot.FullName, "Intermediate", "DesktopPetPackaging"));
        CommandUtils.Logger.LogInformation(
            "DesktopPet automatic names: {Launcher} / {Game}; updated {Count} staged EXEs.",
            ApplicationName + "Game",
            ApplicationName,
            Plans.Count);
    }

    public override void GetFilesToArchive(ProjectParams Params, DeploymentContext SC)
    {
        // 重新打包到同一目录时，UE 不会自动删除旧名称。只归档本插件拥有且启动目标相同的旧启动器。
        // 普通用户文件、其他 EXE 和内部项目资源目录均不处理。
        string StageRoot = Path.GetFullPath(SC.StageDirectory.FullName),
               ArchiveRoot = Path.GetFullPath(SC.ArchiveDirectory.FullName);
        var Current = SC.ArchivedFiles
                          .Where(P => Path.GetDirectoryName(Path.GetFullPath(P.Key))
                                          .Equals(StageRoot, StringComparison.OrdinalIgnoreCase) &&
                                      P.Key.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                          .Select(P => (Destination: Path.GetFullPath(Path.Combine(ArchiveRoot, P.Value)),
                                        Child: PetExeResources.ReadBootstrapTarget(P.Key)))
                          .Where(P => P.Child != null)
                          .ToList();
        if (Current.Count == 0 || !Directory.Exists(ArchiveRoot))
            return;
        foreach (string FilePath in Directory.EnumerateFiles(ArchiveRoot, "*.exe", SearchOption.TopDirectoryOnly))
        {
            string Candidate = Path.GetFullPath(FilePath);
            if (Current.Any(P => P.Destination.Equals(Candidate, StringComparison.OrdinalIgnoreCase)))
                continue;
            string Original = FileVersionInfo.GetVersionInfo(Candidate).OriginalFilename;
            if (Original == null || !Original.StartsWith("BootstrapPackagedGame", StringComparison.OrdinalIgnoreCase))
                continue;
            string Child = PetExeResources.ReadBootstrapTarget(Candidate);
            if (Child == null || !Current.Any(P => P.Child.Equals(Child, StringComparison.OrdinalIgnoreCase)))
                continue;
            // 4.8 的旧默认启动器还没有专用标记，额外允许迁移同工程的默认入口名。
            bool Legacy = Path.GetFileNameWithoutExtension(Candidate).Equals(SC.ShortProjectName,
                                                                             StringComparison.OrdinalIgnoreCase);
            if (!Legacy && PetExeResources.ReadApplicationName(Candidate) == null)
                continue;
            if (!Path.GetDirectoryName(Candidate).Equals(ArchiveRoot, StringComparison.OrdinalIgnoreCase))
                throw new AutomationException("DesktopPet：旧启动器路径越界。");
            string Backup = Path.Combine(SC.ProjectRoot.FullName,
                                         "Saved",
                                         "DesktopPetPackaging",
                                         "PreviousLaunchers",
                                         Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Backup);
            File.Move(Candidate, Path.Combine(Backup, Path.GetFileName(Candidate)));
            CommandUtils.Logger.LogInformation("DesktopPet moved previous launcher to backup: {Entry}", Candidate);
        }
    }
}

/// <summary>保留图标、UE 启动参数、其他版本字段和语言；只更新显示名称及插件专用名称资源。</summary>
internal static class PetExeResources
{
    internal const string NameResource = "DESKTOPPET_APPLICATION_NAME";
    internal sealed record Plan(string Path, string Description, string WindowName);
    private sealed class Block
    {
        public ushort Type;
        public string Key = "";
        public byte[] Value = Array.Empty<byte>();
        public List<Block> Children = new();
    }
    private sealed record Version(ushort Language, byte[] Bytes);
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate bool LanguageCallback(IntPtr Module, IntPtr Type, IntPtr Name, ushort Language, IntPtr Parameter);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryExW(string File, IntPtr Reserved, uint Flags);
    [DllImport("kernel32.dll")]
    private static extern bool FreeLibrary(IntPtr Module);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindResourceW(IntPtr Module, IntPtr Name, IntPtr Type);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindResourceW")]
    private static extern IntPtr FindNamedResource(IntPtr Module, string Name, IntPtr Type);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindResourceExW(IntPtr Module, IntPtr Type, IntPtr Name, ushort Language);
    [DllImport("kernel32.dll")]
    private static extern uint SizeofResource(IntPtr Module, IntPtr Resource);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LoadResource(IntPtr Module, IntPtr Resource);
    [DllImport("kernel32.dll")]
    private static extern IntPtr LockResource(IntPtr Resource);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumResourceLanguagesW(
        IntPtr Module, IntPtr Type, IntPtr Name, LanguageCallback Callback, IntPtr Parameter);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr BeginUpdateResourceW(string File, bool Delete);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool UpdateResourceW(
        IntPtr Update, IntPtr Type, IntPtr Name, ushort Language, byte[] Data, uint Size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "UpdateResourceW", SetLastError = true)]
    private static extern bool UpdateNamedResource(
        IntPtr Update, IntPtr Type, string Name, ushort Language, byte[] Data, uint Size);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool EndUpdateResourceW(IntPtr Update, bool Discard);
    private static int Align(int P) => (P + 3) & ~3;
    private static ushort U16(byte[] Data, int P) => BitConverter.ToUInt16(Data, P);
    private static Block Parse(byte[] Data, int Start, int Limit)
    {
        if (Start + 6 > Limit)
            throw new InvalidDataException("VERSIONINFO header");
        int End = Start + U16(Data, Start), Length = U16(Data, Start + 2), P = Start + 6;
        var B = new Block { Type = U16(Data, Start + 4) };
        if (End > Limit || End <= P || B.Type > 1)
            throw new InvalidDataException("VERSIONINFO length");
        int KeyStart = P;
        while (P + 2 <= End && U16(Data, P) != 0)
            P += 2;
        if (P + 2 > End)
            throw new InvalidDataException("VERSIONINFO key");
        B.Key = Encoding.Unicode.GetString(Data, KeyStart, P - KeyStart);
        P = Align(P + 2);
        int Bytes = Length * (B.Type == 1 ? 2 : 1);
        if (P + Bytes > End)
        {
            if (Bytes == 0 && P - End < 4)
                return B;
            throw new InvalidDataException("VERSIONINFO value");
        }
        B.Value = Data.AsSpan(P, Bytes).ToArray();
        P = Align(P + Bytes);
        while (P + 6 <= End)
        {
            var Child = Parse(Data, P, End);
            B.Children.Add(Child);
            P = Align(P + U16(Data, P));
        }
        return B;
    }
    private static void Encode(Block B, BinaryWriter W)
    {
        long Start = W.BaseStream.Position;
        W.Write((ushort)0);
        W.Write(checked((ushort)(B.Type == 1 ? B.Value.Length / 2 : B.Value.Length)));
        W.Write(B.Type);
        W.Write(Encoding.Unicode.GetBytes(B.Key + "\0"));
        Pad(W);
        W.Write(B.Value);
        foreach (var Child in B.Children)
        {
            Pad(W);
            Encode(Child, W);
        }
        long End = W.BaseStream.Position;
        W.BaseStream.Position = Start;
        W.Write(checked((ushort)(End - Start)));
        W.BaseStream.Position = End;
    }
    private static void Pad(BinaryWriter W)
    {
        while ((W.BaseStream.Position & 3) != 0)
            W.Write((byte)0);
    }
    private static void SetString(Block B, string Key, string Value)
    {
        var Child = B.Children.FirstOrDefault(C => C.Key == Key);
        if (Child == null)
        {
            Child = new Block { Key = Key };
            B.Children.Add(Child);
        }
        Child.Type = 1;
        Child.Value = Encoding.Unicode.GetBytes(Value + "\0");
    }
    private static byte[] ReadResource(IntPtr Module, IntPtr Resource)
    {
        if (Resource == IntPtr.Zero)
            return null;
        IntPtr Data = LockResource(LoadResource(Module, Resource));
        if (Data == IntPtr.Zero)
            return null;
        byte[] Bytes = new byte[SizeofResource(Module, Resource)];
        Marshal.Copy(Data, Bytes, 0, Bytes.Length);
        return Bytes;
    }
    internal static string ReadBootstrapTarget(string Path)
    {
        IntPtr Module = LoadLibraryExW(Path, IntPtr.Zero, 0x22);
        if (Module == IntPtr.Zero)
            throw new IOException("无法读取暂存 EXE：" + Path);
        try
        {
            var Bytes = ReadResource(Module, FindResourceW(Module, new IntPtr(201), new IntPtr(10)));
            return Bytes != null && Bytes.Length >= 2 && Bytes.Length % 2 == 0 && U16(Bytes, Bytes.Length - 2) == 0 ?
                       Encoding.Unicode.GetString(Bytes, 0, Bytes.Length - 2) :
                       null;
        }
        finally
        {
            FreeLibrary(Module);
        }
    }
    internal static string ReadApplicationName(string Path)
    {
        IntPtr Module = LoadLibraryExW(Path, IntPtr.Zero, 0x22);
        if (Module == IntPtr.Zero)
            return null;
        try
        {
            var Bytes = ReadResource(Module, FindNamedResource(Module, NameResource, new IntPtr(10)));
            return Bytes != null && Bytes.Length >= 2 && Bytes.Length % 2 == 0 && U16(Bytes, Bytes.Length - 2) == 0 ?
                       Encoding.Unicode.GetString(Bytes, 0, Bytes.Length - 2) :
                       null;
        }
        finally
        {
            FreeLibrary(Module);
        }
    }
    private static List<Version> Prepare(Plan P)
    {
        // 预检签名及占用；在签名前修改资源。绝不重写引擎安装目录或项目 Binaries 的源 EXE。
        using (var File = new FileStream(P.Path, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) using (
            var Reader = new BinaryReader(File))
        {
            if (Reader.ReadUInt16() != 0x5a4d)
                throw new InvalidDataException("DOS header");
            File.Position = 0x3c;
            int Offset = Reader.ReadInt32();
            if (Offset < 0 || Offset + 168L > File.Length)
                throw new InvalidDataException("PE offset");
            File.Position = Offset;
            if (Reader.ReadUInt32() != 0x4550)
                throw new InvalidDataException("PE header");
            File.Position = Offset + 24;
            if (Reader.ReadUInt16() != 0x20b)
                throw new InvalidDataException("需要 Win64 EXE");
            File.Position = Offset + 24 + 112 + 4 * 8 + 4;
            if (Reader.ReadUInt32() != 0)
                throw new IOException("DesktopPet：请在自动命名完成后签名 EXE：" + P.Path);
        }
        IntPtr Module = LoadLibraryExW(P.Path, IntPtr.Zero, 0x22);
        if (Module == IntPtr.Zero)
            throw new IOException("LoadLibraryEx：" + P.Path);
        var Versions = new List<Version>();
        try
        {
            // 不从原生枚举回调抛出托管异常，先收集资源，再在托管栈解析。
            LanguageCallback Callback = (M, T, N, L, A) =>
            {
                var Bytes = ReadResource(M, FindResourceExW(M, T, N, L));
                if (Bytes == null)
                    return false;
                Versions.Add(new Version(L, Bytes));
                return true;
            };
            if (!EnumResourceLanguagesW(Module, new IntPtr(16), new IntPtr(1), Callback, IntPtr.Zero) ||
                Versions.Count == 0)
                throw new InvalidDataException("EXE 缺少版本资源：" + P.Path);
        }
        finally
        {
            FreeLibrary(Module);
        }
        return Versions
            .Select(V =>
                    {
                        var Root = Parse(V.Bytes, 0, V.Bytes.Length);
                        var Tables =
                            Root.Children.Where(C => C.Key == "StringFileInfo").SelectMany(C => C.Children).ToList();
                        if (Root.Key != "VS_VERSION_INFO" || Tables.Count == 0)
                            throw new InvalidDataException("VERSIONINFO strings");
                        foreach (var T in Tables)
                        {
                            SetString(T, "FileDescription", P.Description);
                            SetString(T, "ProductName", P.Description);
                        }
                        using var Stream = new MemoryStream();
                        using var Writer = new BinaryWriter(Stream);
                        Encode(Root, Writer);
                        return new Version(V.Language, Stream.ToArray());
                    })
            .ToList();
    }
    private static void Apply(Plan P, List<Version> Versions)
    {
        IntPtr Handle = BeginUpdateResourceW(P.Path, false);
        if (Handle == IntPtr.Zero)
            throw new IOException("BeginUpdateResource：" + P.Path);
        try
        {
            foreach (var V in Versions)
                if (!UpdateResourceW(Handle, new IntPtr(16), new IntPtr(1), V.Language, V.Bytes, (uint)V.Bytes.Length))
                    throw new IOException("UpdateResource：" + P.Path);
            if (P.WindowName != null)
            {
                byte[] Bytes = Encoding.Unicode.GetBytes(P.WindowName + "\0");
                if (!UpdateNamedResource(Handle, new IntPtr(10), NameResource, 0, Bytes, (uint)Bytes.Length))
                    throw new IOException("窗口名称资源写入失败");
            }
            IntPtr Commit = Handle;
            Handle = IntPtr.Zero;
            if (!EndUpdateResourceW(Commit, false))
                throw new IOException("EndUpdateResource：" + P.Path);
        }
        finally
        {
            if (Handle != IntPtr.Zero)
                EndUpdateResourceW(Handle, true);
        }
    }
    internal static void ApplyAll(List<Plan> Plans, string BackupRoot)
    {
        // 先验证全部文件，再创建本次专属备份。任一写入失败都会恢复本批文件并中止打包。
        var Prepared = Plans.Select(Prepare).ToList();
        string Backup = Path.Combine(BackupRoot, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Backup);
        for (int I = 0; I < Plans.Count; ++I)
            File.Copy(Plans[I].Path, Path.Combine(Backup, I + ".exe"));
        try
        {
            for (int I = 0; I < Plans.Count; ++I)
                Apply(Plans[I], Prepared[I]);
        }
        catch
        {
            for (int I = 0; I < Plans.Count; ++I)
                File.Copy(Path.Combine(Backup, I + ".exe"), Plans[I].Path, true);
            throw;
        }
        // 仅删除本函数刚创建的 GUID 备份目录，不接收用户提供的删除路径。
        Directory.Delete(Backup, true);
    }
}
