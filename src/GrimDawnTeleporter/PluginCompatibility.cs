using System.Diagnostics;
using System.IO;
using Microsoft.Win32;

namespace GrimDawnTeleporter;

/// <summary>
/// 插件兼容性检查结果。
/// </summary>
/// <param name="IsCompatible">是否可以安全注入。</param>
/// <param name="UsesDynamicCrt">插件是否仍依赖系统 VC 运行库（msvcp140/vcruntime140）。</param>
/// <param name="RequiredCrtVersion">插件编译工具链对应的运行库版本，例如 14.43。</param>
/// <param name="SystemCrtVersion">当前系统实际安装的运行库版本。</param>
/// <param name="Message">给用户的说明文字。</param>
public sealed record PluginCompatibilityReport(
    bool IsCompatible,
    bool UsesDynamicCrt,
    Version? RequiredCrtVersion,
    Version? SystemCrtVersion,
    string Message);

/// <summary>
/// 在注入插件前检查插件与当前环境的兼容性。
///
/// 背景：插件使用新版 MSVC STL 编译时，std::mutex 等对象的内存布局与旧版
/// msvcp140.dll 不一致。若目标机器（典型如虚拟机或未更新运行库的系统）里的
/// msvcp140.dll 比编译插件所用的工具链旧，注入后插件在锁定时会触发
/// 0xC0000005 访问冲突，异常发生在游戏进程内，表现为“启动工具后游戏闪退”。
/// 因此这里在注入前做一次版本比对，并给出明确提示。
/// </summary>
public static class PluginCompatibility
{
    private const string Msvcp140 = "MSVCP140.dll";
    private const string Vcruntime140 = "VCRUNTIME140.dll";
    private const string Vcruntime1401 = "VCRUNTIME140_1.dll";
    private const string Msvcp1401 = "MSVCP140_1.dll";
    private const string Msvcp1402 = "MSVCP140_2.dll";

    public static PluginCompatibilityReport Check(string pluginPath)
    {
        if (!File.Exists(pluginPath))
        {
            return new PluginCompatibilityReport(false, false, null, null, $"找不到插件文件：{pluginPath}");
        }

        if (!TryReadPluginInfo(pluginPath, out var linkerVersion, out var imports))
        {
            return new PluginCompatibilityReport(true, false, null, null,
                "无法解析插件文件头，已跳过运行库兼容性检查。");
        }

        var usesDynamicCrt = imports.Any(name =>
            string.Equals(name, Msvcp140, StringComparison.OrdinalIgnoreCase)
            || string.Equals(name, Msvcp1401, StringComparison.OrdinalIgnoreCase)
            || string.Equals(name, Msvcp1402, StringComparison.OrdinalIgnoreCase)
            || string.Equals(name, Vcruntime140, StringComparison.OrdinalIgnoreCase)
            || string.Equals(name, Vcruntime1401, StringComparison.OrdinalIgnoreCase));

        if (!usesDynamicCrt)
        {
            return new PluginCompatibilityReport(true, false, null, null,
                "插件使用静态运行库（/MT），不依赖系统 VC 运行库，兼容性检查通过。");
        }

        var required = new Version(
            Math.Max(linkerVersion.Major, 14),
            linkerVersion.Minor > 0 ? linkerVersion.Minor : 0);
        var requiredNormalized = Normalize(required);

        if (!TryGetSystemRuntimeVersion(out var systemVersion, out var systemPath))
        {
            return new PluginCompatibilityReport(false, true, required, null,
                $"插件依赖系统 VC 运行库，但未找到 {Msvcp140}。请安装最新的 “Visual C++ 2015-2022 可再发行组件 (x64)” 后重试，"
                + "或使用 build-release.ps1 重新编译插件（工程已默认静态运行库）。");
        }

        var systemNormalized = Normalize(systemVersion);
        if (systemNormalized >= requiredNormalized)
        {
            return new PluginCompatibilityReport(true, true, required, systemVersion,
                $"插件运行库版本兼容（要求 >= {requiredNormalized}，本机 {systemVersion}）。");
        }

        var message =
            $"插件与当前系统运行库不兼容，已阻止注入以避免游戏闪退。{Environment.NewLine}"
            + $"原因：插件由工具链 14.{requiredNormalized.Minor} 编译（动态 VC 运行库），而本机 {Msvcp140} 版本为 {systemVersion}（{systemPath}）。"
            + "新版 STL 的对象布局与旧运行库不一致，注入后插件会在锁定时触发访问冲突，连带游戏进程一起退出。"
            + Environment.NewLine
            + "解决办法（任选其一）：" + Environment.NewLine
            + "1. 安装最新的 “Visual C++ 2015-2022 可再发行组件 (x64)” 后重启工具；" + Environment.NewLine
            + "2. 运行 build-release.ps1 重新编译插件（工程已默认 /MT 静态运行库，不再依赖系统 msvcp140）；" + Environment.NewLine
            + "3. 不使用插件，直接以“外部内存”模式使用传送功能（默认已是该模式）。";

        return new PluginCompatibilityReport(false, true, required, systemVersion, message);
    }

    /// <summary>
    /// 粗略判断当前是否运行在虚拟机中（用于给出兼容性提示）。
    /// </summary>
    public static bool IsLikelyVirtualMachine()
    {
        try
        {
            var systemDirectory = Environment.SystemDirectory;
            string[] guestDriverPaths =
            [
                Path.Combine(systemDirectory, "vm3dum64.dll"),      // VMware SVGA 3D
                Path.Combine(systemDirectory, "vboxguest.dll"),     // VirtualBox Guest Additions
                Path.Combine(systemDirectory, "vmhgfs.dll"),        // VMware 共享文件夹
                Path.Combine(systemDirectory, "vmusbmouse.dll"),    // VMware 鼠标驱动
                Path.Combine(systemDirectory, "drivers", "VBoxMouse.sys"),
                Path.Combine(systemDirectory, "drivers", "vmmouse.sys")
            ];

            foreach (var driver in guestDriverPaths)
            {
                if (File.Exists(driver))
                {
                    return true;
                }
            }

            const string systemInformationKey = @"HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\SystemInformation";
            var manufacturer = Registry.GetValue(systemInformationKey, "SystemManufacturer", null) as string;
            var productName = Registry.GetValue(systemInformationKey, "SystemProductName", null) as string;
            var text = $"{manufacturer} {productName}";
            return text.Contains("VMware", StringComparison.OrdinalIgnoreCase)
                || text.Contains("VirtualBox", StringComparison.OrdinalIgnoreCase)
                || text.Contains("Virtual Machine", StringComparison.OrdinalIgnoreCase)
                || text.Contains("QEMU", StringComparison.OrdinalIgnoreCase)
                || text.Contains("Hyper-V", StringComparison.OrdinalIgnoreCase)
                || text.Contains("KVM", StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception)
        {
            return false;
        }
    }

    private static Version Normalize(Version version)
    {
        return new Version(
            version.Major,
            version.Minor,
            version.Build > 0 ? version.Build : 0,
            version.Revision > 0 ? version.Revision : 0);
    }

    private static bool TryGetSystemRuntimeVersion(out Version version, out string path)
    {
        version = new Version(0, 0);
        path = Path.Combine(Environment.SystemDirectory, Msvcp140);
        try
        {
            if (!File.Exists(path))
            {
                return false;
            }

            var fileVersion = FileVersionInfo.GetVersionInfo(path).FileVersion;
            if (string.IsNullOrWhiteSpace(fileVersion))
            {
                return false;
            }

            var text = fileVersion.Split(' ')[0];
            if (!Version.TryParse(text, out var parsed))
            {
                return false;
            }

            version = parsed;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }

    private static bool TryReadPluginInfo(string path, out Version linkerVersion, out List<string> imports)
    {
        linkerVersion = new Version(0, 0);
        imports = [];

        try
        {
            var data = File.ReadAllBytes(path);
            if (data.Length < 0x100)
            {
                return false;
            }

            var peOffset = BitConverter.ToInt32(data, 0x3C);
            if (peOffset <= 0 || peOffset + 24 + 112 > data.Length)
            {
                return false;
            }

            var optionalHeaderOffset = peOffset + 24;
            var magic = BitConverter.ToUInt16(data, optionalHeaderOffset);
            var isPe32Plus = magic == 0x20B;
            var directoriesOffset = optionalHeaderOffset + (isPe32Plus ? 112 : 96);

            var numberOfSections = BitConverter.ToUInt16(data, peOffset + 6);
            var sizeOfOptionalHeader = BitConverter.ToUInt16(data, peOffset + 20);
            var sectionHeadersOffset = optionalHeaderOffset + sizeOfOptionalHeader;

            linkerVersion = new Version(data[optionalHeaderOffset + 2], data[optionalHeaderOffset + 3]);

            if (directoriesOffset + 16 > data.Length)
            {
                return true;
            }

            var numberOfDirectories = BitConverter.ToInt32(data, optionalHeaderOffset + (isPe32Plus ? 108 : 92));
            if (numberOfDirectories < 2)
            {
                return true;
            }

            var importRva = BitConverter.ToInt32(data, directoriesOffset + 8);
            if (importRva <= 0)
            {
                return true;
            }

            var sections = new List<(uint VirtualAddress, uint VirtualSize, uint RawPointer, uint RawSize)>();
            for (var i = 0; i < numberOfSections; i++)
            {
                var offset = sectionHeadersOffset + i * 40;
                if (offset + 40 > data.Length)
                {
                    break;
                }

                sections.Add((
                    BitConverter.ToUInt32(data, offset + 12),
                    BitConverter.ToUInt32(data, offset + 8),
                    BitConverter.ToUInt32(data, offset + 20),
                    BitConverter.ToUInt32(data, offset + 16)));
            }

            long ToFileOffset(int rva)
            {
                foreach (var section in sections)
                {
                    var size = Math.Max(section.VirtualSize, section.RawSize);
                    if (rva >= section.VirtualAddress && rva < section.VirtualAddress + size)
                    {
                        return section.RawPointer + (rva - section.VirtualAddress);
                    }
                }

                return -1;
            }

            var descriptorOffset = ToFileOffset(importRva);
            if (descriptorOffset < 0)
            {
                return true;
            }

            for (var i = 0; i < 1024; i++)
            {
                var offset = descriptorOffset + i * 20;
                if (offset + 20 > data.Length)
                {
                    break;
                }

                var nameRva = BitConverter.ToInt32(data, (int)offset + 12);
                var firstThunk = BitConverter.ToInt32(data, (int)offset + 16);
                if (nameRva == 0 && firstThunk == 0)
                {
                    break;
                }

                var nameOffset = ToFileOffset(nameRva);
                if (nameOffset < 0)
                {
                    continue;
                }

                var end = (int)nameOffset;
                while (end < data.Length && data[end] != 0)
                {
                    end++;
                }

                if (end > nameOffset)
                {
                    imports.Add(System.Text.Encoding.ASCII.GetString(data, (int)nameOffset, end - (int)nameOffset));
                }
            }

            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }
}
