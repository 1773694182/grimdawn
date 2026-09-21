using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using GrimDawnTeleporter.Models;
using Microsoft.Win32;

namespace GrimDawnTeleporter;

public partial class MainWindow : Window
{
    private const uint VkF6 = 0x75;
    private const uint VkF7 = 0x76;
    private const uint VkF8 = 0x77;
    private const uint VkF9 = 0x78;
    private const uint VkF10 = 0x79;
    private readonly ConfigService _configService = new();
    private readonly GameProcessService _processService = new();
    private readonly InjectorService _injectorService = new();
    private readonly PluginIpcClient _pluginIpcClient = new();
    private readonly ObservableCollection<TeleportPoint> _points = [];
    private readonly ObservableCollection<TeleportPoint> _filteredPoints = [];
    private readonly ObservableCollection<string> _groups = [];
    private readonly ObservableCollection<string> _filteredGroups = [];
    private readonly ObservableCollection<GroupSummary> _groupSummaries = [];
    private string _selectedGroup = TeleportPoint.UngroupedName;
    private bool _updatingGroupSelection;
    private MemoryConfig _memoryConfig = new();
    private TeleportPointStore _store = null!;
    private TeleportService _teleportService = null!;
    private SessionAddressStore _sessionAddressStore = null!;
    private AutoScanSeedStore _autoScanSeedStore = null!;
    private HotkeyService? _hotkeyService;
    private Coordinate3? _currentCoordinate;
    private Coordinate3? _lastPluginPosition;
    private bool _godModeEnabled;
    private bool _oneShotEnabled;
    private CancellationTokenSource? _autoFarmCts;
    private Task? _autoFarmTask;

    public MainWindow()
    {
        InitializeComponent();
    }

    private void Window_Loaded(object sender, RoutedEventArgs e)
    {
        _memoryConfig = _configService.LoadMemoryConfig();
        _store = new TeleportPointStore(_configService.TeleportPointsPath);
        _sessionAddressStore = new SessionAddressStore(_configService.SessionAddressPath);
        _autoScanSeedStore = new AutoScanSeedStore(_configService.AutoScanSeedPath);
        _teleportService = new TeleportService(_memoryConfig, _processService, _sessionAddressStore, _autoScanSeedStore);

        var pointFile = _store.LoadFile();
        foreach (var group in pointFile.Groups.Select(NormalizeGroupName).Where(group => group.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase))
        {
            _groups.Add(group);
        }

        foreach (var point in pointFile.Points)
        {
            point.Group = NormalizeGroupName(point.Group);
            _points.Add(point);
        }

        RefreshGroups();
        PointsGrid.ItemsSource = _filteredPoints;
        GroupComboBox.ItemsSource = _filteredGroups;
        GroupListBox.ItemsSource = _groupSummaries;
        RefreshFilter();
        RefreshGroupSearch();
        RefreshGroupPanel();

        _hotkeyService = new HotkeyService(this);
        _hotkeyService.Register(1, VkF6, () => AddCurrentPoint_Click(this, new RoutedEventArgs()));
        _hotkeyService.Register(2, VkF7, () => TeleportSelected_Click(this, new RoutedEventArgs()));
        _hotkeyService.Register(3, VkF8, ToggleGodModeFromHotkey);
        _hotkeyService.Register(4, VkF9, ToggleOneShotFromHotkey);
        _hotkeyService.Register(5, VkF10, StopAutoFarm);

        AutoFarmTargetComboBox.ItemsSource = _points;
        AutoFarmTargetComboBox.DisplayMemberPath = nameof(TeleportPoint.Name);

        UpdateGodModeToggleUi();
        UpdateOneShotToggleUi();
        AutoDetectAndAttachPlugin();
    }

    private void Window_Closing(object? sender, CancelEventArgs e)
    {
        SavePoints();
        _autoFarmCts?.Cancel();
        _hotkeyService?.Dispose();
        _processService.CloseStartedProcesses();
    }

    private void DetectProcess_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            UpdateProcessBadge($"已连接 {info.DisplayName}", true);
            if (_teleportService.TryRestoreSessionAddress())
            {
                SetStatus($"已检测到进程：{info.DisplayName}。已恢复本次游戏进程的动态坐标地址。");
                return;
            }

            var autoScanned = _teleportService.TryAutoScanFromSavedSeed();
            SetStatus(autoScanned is { }
                ? $"已检测到进程：{info.DisplayName}。已使用上次初始化坐标自动扫描并恢复地址。"
                : $"已检测到进程：{info.DisplayName}");
        });
    }

    private void AttachPlugin_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var message = AttachPluginIfNeeded();
            var info = _teleportService.GetGameProcess();
            UpdateProcessBadge($"已连接 {info.DisplayName}", true);
            SetStatus(message);
        });
    }

    private void PluginStatus_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "{\"type\":\"get_status\"}");
            SetStatus($"插件响应：{response}");
        });
    }

    private void AddCurrentPoint_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var coordinate = ReadCurrentCoordinate();
            var point = new TeleportPoint
            {
                Name = $"记录点 {DateTime.Now:yyyyMMdd-HHmmss}",
                X = coordinate.X,
                Y = coordinate.Y,
                Z = coordinate.Z,
                Group = _selectedGroup == TeleportPoint.UngroupedName ? string.Empty : _selectedGroup,
                CreatedAt = DateTime.Now
            };
            _points.Add(point);
            RefreshFilter();
            SavePoints();
            RefreshGroupPanel();
            SelectPoint(point);
            SetStatus($"已记录当前位置：{point.Name} ({point.CoordinateText})");
        });
    }

    private void CurrencyApiRead_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var value = ReadMoneyThroughPlugin();
            CurrencyCurrentBox.Text = value.ToString(CultureInfo.InvariantCulture);
            SetStatus($"已通过游戏 API 读取当前货币：{value}");
        });
    }

    private void CurrencyApiSet_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var targetValue = ParseCurrencyValue(CurrencyTargetBox.Text, "目标货币数量");
            var value = SetMoneyThroughPlugin(targetValue);
            CurrencyCurrentBox.Text = value.ToString(CultureInfo.InvariantCulture);
            SetStatus($"已通过游戏 API 设置货币：{value}");
        });
    }
    
    private void ToggleGodModeFromHotkey()
    {
        GodModeToggleBtn.IsChecked = GodModeToggleBtn.IsChecked != true;
        GodModeToggle_Click(this, new RoutedEventArgs());
    }

    private void UpdateGodModeToggleUi()
    {
        GodModeToggleBtn.IsChecked = _godModeEnabled;
        GodModeToggleBtn.Content = _godModeEnabled ? "已开启" : "已关闭";
    }

    private void GodModeToggle_Click(object sender, RoutedEventArgs e)
    {
        var enable = GodModeToggleBtn.IsChecked == true;
        RunSafely(() =>
        {
            try
            {
                var info = _teleportService.GetGameProcess();
                if (!info.IsX64)
                {
                    throw new InvalidOperationException("x86 版本暂不支持作弊功能，请使用 x64 版本。");
                }

                var command = $"god_mode:{enable}";
                var response = _pluginIpcClient.Send(info.Process.Id, command);
                EnsurePluginResponseType(response, "god_mode");
                _godModeEnabled = enable;
                SetStatus($"无敌模式已{(enable ? "开启" : "关闭")}");
            }
            finally
            {
                UpdateGodModeToggleUi();
            }
        });
    }

    private void GodModeStatus_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            if (!info.IsX64)
            {
                throw new InvalidOperationException("无敌模式状态查询仅支持 x64 游戏进程。");
            }

            var response = _pluginIpcClient.Send(info.Process.Id, "{\"type\":\"get_god_mode\"}");
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            if (!root.TryGetProperty("enabled", out var enabled))
            {
                throw new InvalidOperationException($"无法获取无敌模式状态：{response}");
            }

            _godModeEnabled = enabled.GetBoolean();
            UpdateGodModeToggleUi();
            SetStatus($"无敌模式状态：{(_godModeEnabled ? "已启用" : "未启用")}");
        });
    }
    
    private void InstaKill_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var radius = ParseInstaKillRadius();
            var info = _teleportService.GetGameProcess();
            if (!info.IsX64)
            {
                throw new InvalidOperationException("秒杀怪物仅支持 x64 游戏进程。");
            }

            var command = string.Format(CultureInfo.InvariantCulture, "kill_monsters:{0}", radius);
            var response = _pluginIpcClient.Send(info.Process.Id, command, 30000);
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            if (!root.TryGetProperty("type", out var type)
                || !string.Equals(type.GetString(), "kill_monsters", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException($"插件未返回 kill_monsters：{response}");
            }

            var killed = root.TryGetProperty("killed", out var killedProperty) ? killedProperty.GetInt32() : 0;
            SetStatus($"秒杀完成：半径 {radius} 内击杀 {killed} 个怪物。");
        });
    }

    private float ParseInstaKillRadius()
    {
        var text = InstaKillRadiusBox.Text.Trim();
        if (float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var radius)
            && radius > 0
            && radius <= 500)
        {
            return radius;
        }

        throw new InvalidOperationException("请输入 0 到 500 之间的秒杀半径。");
    }

    private void ToggleOneShotFromHotkey()
    {
        OneShotToggleBtn.IsChecked = OneShotToggleBtn.IsChecked != true;
        OneShotToggle_Click(this, new RoutedEventArgs());
    }

    private void UpdateOneShotToggleUi()
    {
        OneShotToggleBtn.IsChecked = _oneShotEnabled;
        OneShotToggleBtn.Content = _oneShotEnabled ? "已开启" : "已关闭";
    }

    private void OneShotToggle_Click(object sender, RoutedEventArgs e)
    {
        var enable = OneShotToggleBtn.IsChecked == true;
        RunSafely(() =>
        {
            try
            {
                var info = _teleportService.GetGameProcess();
                if (!info.IsX64)
                {
                    throw new InvalidOperationException("一击必杀仅支持 x64 游戏进程。");
                }

                var command = $"one_shot:{enable}";
                var response = _pluginIpcClient.Send(info.Process.Id, command);
                using var document = JsonDocument.Parse(response);
                if (!document.RootElement.TryGetProperty("enabled", out _))
                {
                    throw new InvalidOperationException($"插件未确认一击必杀状态：{response}");
                }

                _oneShotEnabled = enable;
                SetStatus($"一击必杀已{(enable ? "开启" : "关闭")}（占位实现，需完成伤害 Hook 后才会实际生效）。");
            }
            finally
            {
                UpdateOneShotToggleUi();
            }
        });
    }
    
    private void ModifyStrength_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            if (info.IsX64)
            {
                var strengthValue = 100; // 默认值
                if (int.TryParse(StrengthTextBox.Text, out var parsed))
                {
                    strengthValue = parsed;
                }
                
                var command = $"modify_attribute:strength:{strengthValue}";
                var response = _pluginIpcClient.Send(info.Process.Id, command);
                SetStatus($"力量修改命令已发送 (值：{strengthValue})。注意：此功能需要进一步逆向完善。");
            }
            else
            {
                MessageBox.Show(this, "x86 版本暂不支持属性修改功能。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        });
    }

    private void LaunchX86_Click(object sender, RoutedEventArgs e)
    {
        LaunchGame(useX64: false);
    }

    private void LaunchX64_Click(object sender, RoutedEventArgs e)
    {
        LaunchGame(useX64: true);
    }

    private void LaunchGame(bool useX64)
    {
        RunSafely(() =>
        {
            var exePath = useX64 ? _memoryConfig.GameExePathX64 : _memoryConfig.GameExePathX86;
            if (string.IsNullOrWhiteSpace(exePath))
            {
                throw new InvalidOperationException("尚未配置游戏路径，请检查 data\\MemoryConfig.json。");
            }

            var process = _processService.StartGame(exePath);
            SetStatus($"已启动{(useX64 ? " x64" : " x86")} 游戏进程（PID {process.Id}）。进入游戏后点击“检测进程”。");
        });
    }

    private void UpdateProcessBadge(string text, bool connected)
    {
        ProcessStatusText.Text = text;
        ProcessStatusText.Foreground = (System.Windows.Media.Brush)FindResource(connected ? "SuccessBrush" : "MutedBrush");
        ProcessStatusDot.Fill = (System.Windows.Media.Brush)FindResource(connected ? "SuccessBrush" : "WarningBrush");
    }

    private void TeleportSelected_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var point = GetSelectedPoint();
            var info = _teleportService.GetGameProcess();
            if (info.IsX64)
            {
                _lastPluginPosition = ReadPluginCoordinate(info.Process.Id);
                var teleportCommand = string.Format(CultureInfo.InvariantCulture, "teleport:{0},{1},{2}", point.X, point.Y, point.Z);
                var teleportResponse = _pluginIpcClient.Send(info.Process.Id, teleportCommand);
                EnsurePluginResponseType(teleportResponse, "teleport");
                SetStatus($"已通过插件传送到：{point.Name} ({point.CoordinateText})。传送前位置已自动备份。");
                return;
            }

            _teleportService.TeleportTo(point);
            SetStatus($"已传送到：{point.Name} ({point.CoordinateText})。传送前位置已自动备份。");
        });
    }

    private void ReturnLastPosition_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            if (info.IsX64 && _lastPluginPosition is { } lastPluginPosition)
            {
                var teleportCommand = string.Format(CultureInfo.InvariantCulture, "teleport:{0},{1},{2}", lastPluginPosition.X, lastPluginPosition.Y, lastPluginPosition.Z);
                var teleportResponse = _pluginIpcClient.Send(info.Process.Id, teleportCommand);
                EnsurePluginResponseType(teleportResponse, "teleport");
                SetStatus("已通过插件返回上一个位置。");
                return;
            }

            _teleportService.ReturnToLastPosition();
            SetStatus("已返回上一个位置。");
        });
    }

    private void DeleteSelected_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var point = GetSelectedPoint();
            _points.Remove(point);
            RefreshFilter();
            SavePoints();
            RefreshGroupPanel(_selectedGroup);
            SetStatus($"已删除记录点：{point.Name}");
        });
    }

    private void SavePoints_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            SavePoints();
            SetStatus("已保存传送点列表。");
        });
    }

    private void CreateGroup_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var group = NormalizeGroupName(NewGroupBox.Text);
            if (group.Length == 0)
            {
                throw new InvalidOperationException("请输入分组名称。");
            }

            if (_groups.Any(existing => string.Equals(existing, group, StringComparison.OrdinalIgnoreCase)))
            {
                GroupComboBox.SelectedItem = _groups.First(existing => string.Equals(existing, group, StringComparison.OrdinalIgnoreCase));
                RefreshGroupPanel(group);
                RefreshFilter();
                SetStatus($"分组已存在：{group}");
                return;
            }

            _groups.Add(group);
            SortGroups();
            RefreshGroupSearch();
            RefreshGroupPanel(group);
            RefreshFilter();
            GroupComboBox.SelectedItem = group;
            NewGroupBox.Clear();
            SavePoints();
            SetStatus($"已新建分组：{group}");
        });
    }

    private void AssignSelectedToGroup_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var point = GetSelectedPoint();
            var group = NormalizeGroupName(GroupComboBox.Text);
            if (group.Length == 0)
            {
                throw new InvalidOperationException("请先选择或输入分组。");
            }

            EnsureGroupExists(group);
            point.Group = group;
            SavePoints();
            RefreshGroupSearch();
            RefreshGroupPanel(group);
            RefreshFilter();
            SelectPoint(point);
            SetStatus($"已将坐标“{point.Name}”加入分组：{group}");
        });
    }

    private void RemoveSelectedFromGroup_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var point = GetSelectedPoint();
            point.Group = string.Empty;
            SavePoints();
            RefreshGroupSearch();
            RefreshGroupPanel(TeleportPoint.UngroupedName);
            RefreshFilter();
            SelectPoint(point);
            SetStatus($"已将坐标“{point.Name}”移出分组。");
        });
    }

    private void DeleteGroup_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            if (GroupListBox.SelectedItem is not GroupSummary selectedGroup)
            {
                throw new InvalidOperationException("请先选择要删除的分组。");
            }

            if (selectedGroup.Name == TeleportPoint.UngroupedName)
            {
                throw new InvalidOperationException("未分组不能删除。");
            }

            var result = MessageBox.Show(this,
                $"确定删除分组“{selectedGroup.Name}”吗？该分组内的坐标会移动到“{TeleportPoint.UngroupedName}”。",
                "删除分组",
                MessageBoxButton.OKCancel,
                MessageBoxImage.Warning);

            if (result != MessageBoxResult.OK)
            {
                return;
            }

            foreach (var point in _points.Where(point => string.Equals(point.GroupDisplayName, selectedGroup.Name, StringComparison.OrdinalIgnoreCase)))
            {
                point.Group = string.Empty;
            }

            var existingGroup = _groups.FirstOrDefault(group => string.Equals(group, selectedGroup.Name, StringComparison.OrdinalIgnoreCase));
            if (existingGroup is not null)
            {
                _groups.Remove(existingGroup);
            }

            SavePoints();
            RefreshGroupSearch();
            RefreshGroupPanel(TeleportPoint.UngroupedName);
            RefreshFilter();
            SetStatus($"已删除分组“{selectedGroup.Name}”，组内坐标已移动到“{TeleportPoint.UngroupedName}”。");
        });
    }

    private void GroupSearchBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        RefreshGroupSearch();
        RefreshGroupPanel(_selectedGroup);
        GroupComboBox.IsDropDownOpen = _filteredGroups.Count > 0;
    }

    private void GroupListBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        if (GroupListBox.SelectedItem is GroupSummary group)
        {
            if (_updatingGroupSelection)
            {
                return;
            }

            _selectedGroup = group.Name;
            SelectedGroupTitle.Text = $"当前分组：{_selectedGroup}";
            GroupComboBox.SelectedItem = group.Name == TeleportPoint.UngroupedName ? null : group.Name;
            RefreshFilter();
            SetStatus($"已选择分组：{group.Name}");
        }
    }

    private void ImportGi_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Filter = "Grim Internals 传送列表|GrimInternals_TeleportList.txt|文本文件|*.txt|所有文件|*.*",
            InitialDirectory = @"K:\SteamLibrary\steamapps\common\Grim Dawn"
        };

        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        RunSafely(() =>
        {
            var imported = TeleportPointStore.ImportGrimInternals(dialog.FileName);
            foreach (var point in imported)
            {
                _points.Add(point);
            }

            RefreshFilter();
            SavePoints();
            RefreshGroupSearch();
            RefreshGroupPanel();
            SetStatus($"已导入 {imported.Count} 个 Grim Internals 传送点。");
        });
    }

    private void ExportGi_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog
        {
            Filter = "Grim Internals 传送列表|GrimInternals_TeleportList.txt|文本文件|*.txt|所有文件|*.*",
            FileName = "GrimInternals_TeleportList.txt",
            InitialDirectory = @"K:\SteamLibrary\steamapps\common\Grim Dawn"
        };

        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        RunSafely(() =>
        {
            TeleportPointStore.ExportGrimInternals(dialog.FileName, _points);
            SetStatus($"已导出 Grim Internals 传送列表：{dialog.FileName}");
        });
    }

    private void OpenDataDirectory_Click(object sender, RoutedEventArgs e)
    {
        Process.Start(new ProcessStartInfo
        {
            FileName = _configService.DataDirectory,
            UseShellExecute = true
        });
    }

    private void CopyOutput_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (OutputTextBox is not null && !string.IsNullOrEmpty(OutputTextBox.Text))
            {
                // 确保选中整个文本内容
                OutputTextBox.Select(0, OutputTextBox.Text.Length);
                
                // 复制选中的文本
                Clipboard.SetText(OutputTextBox.SelectedText);
                SetStatus($"已复制 {OutputTextBox.Text.Length} 个字符到剪贴板。");
            }
            else
            {
                MessageBox.Show(this, "输出框为空，没有可复制的内容", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"复制失败：{ex.Message}", "错误", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OutputTextBox_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
    {
        // SelectionChanged 事件处理器（占位实现）
        // 如果需要跟踪选择变化，可以在这里添加逻辑
    }

    private Coordinate3 ReadCurrentCoordinate()
    {
        var info = _teleportService.GetGameProcess();
        if (info.IsX64)
        {
            var pluginCoordinate = ReadPluginCoordinate(info.Process.Id);
            SetStatus($"已通过插件读取当前位置：{pluginCoordinate}");
            return pluginCoordinate;
        }

        var coordinate = _teleportService.ReadCurrentCoordinate();
        _currentCoordinate = coordinate;
        CurrentCoordinateText.Text = coordinate.ToString();
        SetStatus($"已读取当前位置：{coordinate}");
        return coordinate;
    }

    private void AutoDetectAndAttachPlugin()
    {
        try
        {
            var info = _teleportService.GetGameProcess();
            if (info.IsX86)
            {
                UpdateProcessBadge($"已检测 x86 进程 {info.DisplayName}", false);
                SetStatus($"已检测到 x86 游戏进程：{info.DisplayName}。插件仅用于 x64，当前保留旧传送模式。");
                return;
            }

            var message = AttachPluginIfNeeded(info.Process.Id);
            UpdateProcessBadge($"已连接 {info.DisplayName}", true);
            SetStatus($"已检测到进程：{info.DisplayName}。{message}");
        }
        catch (Exception ex)
        {
            UpdateProcessBadge("未检测进程", false);
            SetStatus($"未自动附加插件：{ex.Message}");
        }
    }

    private string AttachPluginIfNeeded(int? processId = null)
    {
        var info = processId.HasValue ? GetGameProcessById(processId.Value) : _teleportService.GetGameProcess();
        if (info.IsX86)
        {
            throw new InvalidOperationException("当前插件仅支持 x64 Grim Dawn。请启动 x64 游戏进程。");
        }

        var targetProcessId = processId ?? info.Process.Id;
        if (IsPluginReady(targetProcessId))
        {
            return "插件已附加。";
        }

        var pluginPath = _injectorService.ResolvePluginPath();
        _injectorService.Inject(info.Process, pluginPath);
        if (!IsPluginReady(targetProcessId, 5000))
        {
            throw new InvalidOperationException("插件注入后未响应。请确认游戏已进入主菜单或世界。");
        }

        return $"已注入插件：{pluginPath}";
    }

    private static GameProcessInfo GetGameProcessById(int processId)
    {
        var process = Process.GetProcessById(processId);
        return new GameProcessInfo { Process = process, IsX86 = GameProcessService.IsProcessX86(process) };
    }

    private bool IsPluginReady(int processId, int timeoutMs = 500)
    {
        try
        {
            var response = _pluginIpcClient.Send(processId, "ping", timeoutMs);
            return response.Contains("pong", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
    }

    private Coordinate3 ReadPluginCoordinate(int? processId = null)
    {
        var info = _teleportService.GetGameProcess();
        if (info.IsX86)
        {
            throw new InvalidOperationException("插件坐标仅支持 x64 Grim Dawn。");
        }

        AttachPluginIfNeeded(processId ?? info.Process.Id);
        var response = _pluginIpcClient.Send(processId ?? info.Process.Id, "get_position");
        var coordinate = ParsePositionResponse(response);
        _currentCoordinate = coordinate;
        CurrentCoordinateText.Text = coordinate.ToString();
        return coordinate;
    }

    private uint ReadMoneyThroughPlugin()
    {
        var info = _teleportService.GetGameProcess();
        if (info.IsX86)
        {
            throw new InvalidOperationException("游戏 API 货币读取仅支持 x64 Grim Dawn。");
        }

        AttachPluginIfNeeded(info.Process.Id);
        var response = _pluginIpcClient.Send(info.Process.Id, "get_money");
        return ParseMoneyResponse(response);
    }

    private uint SetMoneyThroughPlugin(int targetValue)
    {
        var info = _teleportService.GetGameProcess();
        if (info.IsX86)
        {
            throw new InvalidOperationException("游戏 API 货币设置仅支持 x64 Grim Dawn。");
        }

        AttachPluginIfNeeded(info.Process.Id);
        var response = _pluginIpcClient.Send(info.Process.Id, $"set_money:{targetValue}");
        return ParseMoneyResponse(response);
    }

    private TeleportPoint GetSelectedPoint()
    {
        return PointsGrid.SelectedItem as TeleportPoint ?? throw new InvalidOperationException("请先选择一个传送点。");
    }

    private static int ParseCurrencyValue(string text, string fieldName)
    {
        if (!int.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) || value < 0)
        {
            throw new InvalidOperationException($"请输入有效的{fieldName}，只能使用 0 到 {int.MaxValue} 的整数。");
        }

        return value;
    }

    private static Coordinate3 ParsePositionResponse(string response)
    {
        using var document = JsonDocument.Parse(response);
        var root = document.RootElement;
        if (!root.TryGetProperty("type", out var type) || !string.Equals(type.GetString(), "position", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"插件未返回坐标：{response}");
        }

        return new Coordinate3(
            root.GetProperty("x").GetSingle(),
            root.GetProperty("y").GetSingle(),
            root.GetProperty("z").GetSingle());
    }

    private static uint ParseMoneyResponse(string response)
    {
        using var document = JsonDocument.Parse(response);
        var root = document.RootElement;
        if (root.TryGetProperty("type", out var errorType) && string.Equals(errorType.GetString(), "error", StringComparison.OrdinalIgnoreCase))
        {
            var message = root.TryGetProperty("message", out var messageProperty) ? messageProperty.GetString() : response;
            if (string.Equals(message, "unknown command", StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException("当前游戏进程里加载的是旧版 GrimDawnTeleporter.Plugin.dll，不支持货币 API 命令。请关闭游戏和工具，运行 stop-teleporter.bat、build-release.bat 后重新启动 x64 游戏和 x64 工具。");
            }

            throw new InvalidOperationException($"插件读取货币失败：{message}");
        }

        if (!root.TryGetProperty("type", out var type) || !string.Equals(type.GetString(), "money", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"插件未返回货币数量：{response}");
        }

        return root.GetProperty("value").GetUInt32();
    }

    private static void EnsurePluginResponseType(string response, string expectedType)
    {
        using var document = JsonDocument.Parse(response);
        var root = document.RootElement;
        if (!root.TryGetProperty("type", out var type) || !string.Equals(type.GetString(), expectedType, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"插件未返回 {expectedType}：{response}");
        }
    }

    private void SearchBox_TextChanged(object sender, System.Windows.Controls.TextChangedEventArgs e)
    {
        RefreshFilter();
    }

    private void PointsGrid_CellEditEnding(object sender, System.Windows.Controls.DataGridCellEditEndingEventArgs e)
    {
        Dispatcher.BeginInvoke(() =>
        {
            RefreshGroups();
            SavePoints();
            RefreshGroupSearch();
            RefreshGroupPanel(_selectedGroup);
            RefreshFilter();
        });
    }

    private void RefreshFilter()
    {
        var selectedPoint = PointsGrid?.SelectedItem as TeleportPoint;
        var keyword = SearchBox?.Text?.Trim() ?? string.Empty;
        _filteredPoints.Clear();

        foreach (var point in _points.Where(point => MatchesSelectedGroup(point) && Matches(point, keyword)))
        {
            _filteredPoints.Add(point);
        }

        if (selectedPoint is not null && _filteredPoints.Contains(selectedPoint))
        {
            SelectPoint(selectedPoint);
        }
    }

    private static bool Matches(TeleportPoint point, string keyword)
    {
        if (string.IsNullOrEmpty(keyword))
        {
            return true;
        }

        return point.Name.Contains(keyword, StringComparison.OrdinalIgnoreCase)
            || point.Group.Contains(keyword, StringComparison.OrdinalIgnoreCase)
            || point.Area.Contains(keyword, StringComparison.OrdinalIgnoreCase)
            || point.Note.Contains(keyword, StringComparison.OrdinalIgnoreCase);
    }

    private void RefreshGroups()
    {
        foreach (var group in _points.Select(point => NormalizeGroupName(point.Group)).Where(group => group.Length > 0))
        {
            EnsureGroupExists(group);
        }

        SortGroups();
    }

    private void EnsureGroupExists(string group)
    {
        group = NormalizeGroupName(group);
        if (group.Length > 0 && !_groups.Any(existing => string.Equals(existing, group, StringComparison.OrdinalIgnoreCase)))
        {
            _groups.Add(group);
        }
    }

    private void SortGroups()
    {
        var sortedGroups = _groups.Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.OrdinalIgnoreCase).ToList();
        _groups.Clear();
        foreach (var group in sortedGroups)
        {
            _groups.Add(group);
        }
    }

    private void RefreshGroupSearch()
    {
        var keyword = GroupSearchBox?.Text?.Trim() ?? string.Empty;
        _filteredGroups.Clear();
        foreach (var group in _groups.Where(group => string.IsNullOrEmpty(keyword) || group.Contains(keyword, StringComparison.OrdinalIgnoreCase)))
        {
            _filteredGroups.Add(group);
        }
    }

    private void RefreshGroupPanel(string? selectedGroup = null)
    {
        RefreshGroups();
        _groupSummaries.Clear();
        var keyword = GroupSearchBox?.Text?.Trim() ?? string.Empty;

        var ungroupedCount = _points.Count(point => string.IsNullOrWhiteSpace(point.Group));
        if (MatchesGroupName(TeleportPoint.UngroupedName, keyword))
        {
            _groupSummaries.Add(new GroupSummary(TeleportPoint.UngroupedName, ungroupedCount));
        }

        foreach (var group in _groups)
        {
            if (MatchesGroupName(group, keyword))
            {
                _groupSummaries.Add(new GroupSummary(group, _points.Count(point => string.Equals(point.GroupDisplayName, group, StringComparison.OrdinalIgnoreCase))));
            }
        }

        if (!string.IsNullOrWhiteSpace(selectedGroup))
        {
            _selectedGroup = selectedGroup;
        }

        SelectedGroupTitle.Text = $"当前分组：{_selectedGroup}";
        SelectGroupSummary(_selectedGroup);
    }

    private bool MatchesSelectedGroup(TeleportPoint point)
    {
        return string.Equals(point.GroupDisplayName, _selectedGroup, StringComparison.OrdinalIgnoreCase);
    }

    private void SelectGroupSummary(string groupName)
    {
        var summary = _groupSummaries.FirstOrDefault(group => string.Equals(group.Name, groupName, StringComparison.OrdinalIgnoreCase));
        if (summary is not null && !Equals(GroupListBox.SelectedItem, summary))
        {
            _updatingGroupSelection = true;
            try
            {
                GroupListBox.SelectedItem = summary;
                GroupListBox.ScrollIntoView(summary);
            }
            finally
            {
                _updatingGroupSelection = false;
            }
        }
    }

    private static string NormalizeGroupName(string? group)
    {
        return group?.Trim() ?? string.Empty;
    }

    private void SelectPoint(TeleportPoint point)
    {
        PointsGrid.SelectedItem = point;
        PointsGrid.ScrollIntoView(point);
    }

    private void SavePoints()
    {
        RefreshGroups();
        _store.SaveFile(new TeleportPointFile
        {
            Groups = _groups.ToList(),
            Points = _points.ToList()
        });
    }

    private static bool MatchesGroupName(string group, string keyword)
    {
        return string.IsNullOrWhiteSpace(keyword) || group.Contains(keyword, StringComparison.OrdinalIgnoreCase);
    }

    public sealed class GroupSummary
    {
        public GroupSummary(string name, int pointCount)
        {
            Name = name;
            PointCount = pointCount;
        }

        public string Name { get; }
        public int PointCount { get; }
    }

    private void RunSafely(Action action)
    {
        try
        {
            action();
        }
        catch (Exception ex)
        {
            SetStatus(ex.Message);
            MessageBox.Show(this, ex.Message, "操作失败", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void SetStatus(string message)
    {
        StatusText.Text = message;
        if (OutputTextBox is not null)
        {
            OutputTextBox.Text = message;
            OutputTextBox.CaretIndex = OutputTextBox.Text.Length;
        }
    }

    private void ListModules_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "list_modules");
            OutputTextBox.Text += $"\n[模块列表]\n{response}\n";
            SetStatus($"已获取模块列表：{response}");
        });
    }

    private void RunSymbolDiagnostic_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "resolve_core", 15000);
            OutputTextBox.Text += $"\n[符号诊断]\n{FormatJson(response)}\n";
            SetStatus("符号诊断完成，结果已写入输出日志。");
        });
    }

    private void ProbePlayer_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "probe_player");
            OutputTextBox.Text += $"\n[玩家探测]\n{FormatJson(response)}\n";
            SetStatus("玩家探测完成，结果已写入输出日志。");
        });
    }

    private void FindWorld_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "find_world", 20000);
            OutputTextBox.Text += $"\n[定位 World]\n{FormatJson(response)}\n";
            SetStatus("World 定位完成，结果已写入输出日志。");
        });
    }

    private void ProbeEntities_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "probe_entities:30", 20000);
            OutputTextBox.Text += $"\n[实体探测]\n{FormatJson(response)}\n";
            SetStatus("实体探测完成，结果已写入输出日志。");
        });
    }

    private void ListEntityTypes_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "list_entity_types:100", 30000);
            OutputTextBox.Text += $"\n[实体类型统计]\n{FormatJson(response)}\n";
            SetStatus("实体类型统计完成，结果已写入输出日志。");
        });
    }

    private void KillMonsters_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, "kill_monsters:50", 30000);
            OutputTextBox.Text += $"\n[秒杀]\n{FormatJson(response)}\n";
            SetStatus("秒杀命令已执行，结果已写入输出日志。");
        });
    }

    private static string FormatJson(string text)
    {
        try
        {
            using var document = JsonDocument.Parse(text);
            return JsonSerializer.Serialize(document.RootElement, new JsonSerializerOptions { WriteIndented = true });
        }
        catch (JsonException)
        {
            return text;
        }
    }

    private void ExpandCommands_Click(object sender, RoutedEventArgs e)
    {
        AdvancedCommandsExpander.IsExpanded = !AdvancedCommandsExpander.IsExpanded;
    }

    private void SendCustomCommand_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var command = CustomCommandTextBox.Text.Trim();
            if (string.IsNullOrEmpty(command))
            {
                MessageBox.Show(this, "请输入命令", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var info = _teleportService.GetGameProcess();
            var response = _pluginIpcClient.Send(info.Process.Id, command);
            OutputTextBox.Text += $"\n[命令：{command}]\n{response}\n";
            SetStatus($"命令执行完成");
            CustomCommandTextBox.Clear();
        });
    }

    private const uint MouseEventLeftDown = 0x0002;
    private const uint MouseEventLeftUp = 0x0004;

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr hWnd, out NativeRect rect);

    [DllImport("user32.dll")]
    private static extern bool ClientToScreen(IntPtr hWnd, ref NativePoint point);

    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    private static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int cmd);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    private readonly record struct GameStateResult(bool InGame, bool Loading);

    private void AutoFarmStart_Click(object sender, RoutedEventArgs e)
    {
        if (_autoFarmTask is { IsCompleted: false })
        {
            SetStatus("自动挂机已在运行。");
            return;
        }

        if (AutoFarmTargetComboBox.SelectedItem is not TeleportPoint target)
        {
            MessageBox.Show(this, "请先选择目标传送点。", "自动挂机", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        if (!float.TryParse(AutoFarmKillRadiusBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var killRadius)
            || killRadius <= 0
            || killRadius > 500)
        {
            MessageBox.Show(this, "请输入 0 到 500 之间的杀怪半径。", "自动挂机", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (!int.TryParse(AutoFarmMaxMinutesBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var maxMinutes)
            || maxMinutes <= 0
            || maxMinutes > 120)
        {
            MessageBox.Show(this, "请输入 1 到 120 之间的单轮时限（分钟）。", "自动挂机", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _autoFarmCts = new CancellationTokenSource();
        SetAutoFarmUi(running: true);
        SetAutoFarmStatus($"挂机启动，目标：{target.Name}");
        _autoFarmTask = RunAutoFarmAsync(target, killRadius, AutoFarmLootKeywordsBox.Text.Trim(), maxMinutes, _autoFarmCts.Token);
    }

    private void AutoFarmStop_Click(object sender, RoutedEventArgs e)
    {
        StopAutoFarm();
    }

    private void StopAutoFarm()
    {
        if (_autoFarmCts is null)
        {
            return;
        }

        _autoFarmCts.Cancel();
        SetAutoFarmStatus("正在停止...");
    }

    private void SetAutoFarmUi(bool running)
    {
        AutoFarmStartButton.IsEnabled = !running;
        AutoFarmStopButton.IsEnabled = running;
    }

    private void SetAutoFarmStatus(string text)
    {
        AutoFarmStatusText.Text = text;
        SetStatus(text);
    }

    private async Task RunAutoFarmAsync(TeleportPoint target, float killRadius, string lootKeywords, int maxMinutes, CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                SetAutoFarmStatus("检测游戏进程...");
                var info = TryGetGameProcess();
                if (info is null)
                {
                    SetAutoFarmStatus("等待游戏进程启动...");
                    await Task.Delay(2000, token);
                    continue;
                }

                var state = await Task.Run(() => QueryGameState(info.Process.Id), token);
                if (!state.InGame)
                {
                    if (!await TryEnterWorldAsync(info.Process, token))
                    {
                        SetAutoFarmStatus("进入游戏超时，重试...");
                        continue;
                    }
                }

                await Task.Run(() => SendPluginCommand(info.Process.Id, "reset_cache"), token);
                await Task.Delay(300, token);

                SetAutoFarmStatus("开启无敌模式...");
                await Task.Run(() => SendPluginCommand(info.Process.Id, "god_mode:true"), token);
                _godModeEnabled = true;
                UpdateGodModeToggleUi();

                SetAutoFarmStatus("等待游戏稳定（3 秒）...");
                await Task.Delay(3000, token);

                SetAutoFarmStatus("检测附近是否有实体（NPC/怪物/物体）...");
                var entitiesDetected = await WaitForEntitiesAsync(info.Process.Id, killRadius, TimeSpan.FromSeconds(60), token);
                SetAutoFarmStatus(entitiesDetected ? "检测到附近有实体，开始传送..." : "附近长时间无实体，仍执行传送");

                SetAutoFarmStatus($"传送到 {target.Name} ...");
                await Task.Run(() => SendPluginCommand(info.Process.Id, string.Format(
                    CultureInfo.InvariantCulture, "teleport:{0},{1},{2}", target.X, target.Y, target.Z)), token);
                var arrived = await WaitForPositionAsync(info.Process.Id, target, TimeSpan.FromSeconds(30), token);
                SetAutoFarmStatus(arrived ? "已到达目标点" : "传送等待超时，继续执行");
                await Task.Delay(800, token);

                SetAutoFarmStatus($"监控怪物（每秒检测+秒杀，半径 {killRadius}）...");
                var quietSeconds = 0;
                var lootAcquired = false;
                var ineffectiveStrikes = 0;
                var lastKilled = -1;
                var lastMonsterFound = -1;
                var farmDeadline = DateTime.UtcNow.AddMinutes(maxMinutes);
                while (!token.IsCancellationRequested && quietSeconds < 10)
                {
                    if (DateTime.UtcNow >= farmDeadline)
                    {
                        SetAutoFarmStatus($"刷怪 {maxMinutes} 分钟未获得目标物品，强制刷新地图...");
                        break;
                    }

                    var killCommand = string.Format(CultureInfo.InvariantCulture, "kill_monsters:{0}", killRadius);
                    var killResponse = await Task.Run(() => SendPluginCommandAndGetResponse(info.Process.Id, killCommand), token);
                    var (monsterFound, killedCount) = ParseKillResult(killResponse);

                    if (killedCount > 0 && killedCount == lastKilled && monsterFound == lastMonsterFound)
                    {
                        ineffectiveStrikes++;
                        if (ineffectiveStrikes >= 5)
                        {
                            SetAutoFarmStatus("怪物持续无法被击杀（训练假人/免疫怪），返回主菜单...");
                            await Task.Delay(500, token);
                            break;
                        }
                    }
                    else
                    {
                        ineffectiveStrikes = 0;
                    }

                    lastKilled = killedCount;
                    lastMonsterFound = monsterFound;

                    if (killedCount > 0)
                    {
                        quietSeconds = 0;
                        SetAutoFarmStatus($"击杀 {killedCount} 个怪物，等待掉落后拾取...");
                        await Task.Delay(1200, token);
                    }

                    var breakResponse = await Task.Run(() => SendPluginCommandAndGetResponse(info.Process.Id, string.Format(CultureInfo.InvariantCulture, "break_containers:{0}", killRadius)), token);
                    var brokenContainers = ParseBreakContainersResult(breakResponse);
                    if (brokenContainers > 0)
                    {
                        SetAutoFarmStatus($"打碎 {brokenContainers} 个战利品容器，等待掉落后拾取...");
                        await Task.Delay(1200, token);
                    }

                    var chestResponse = await Task.Run(() => SendPluginCommandAndGetResponse(info.Process.Id, "open_chest"), token);
                    var openedChests = ParseChestOpenResult(chestResponse);
                    if (openedChests > 0)
                    {
                        SetAutoFarmStatus($"开启 {openedChests} 个宝箱，等待掉落后拾取...");
                        await Task.Delay(1500, token);
                    }

                    var lootResponse = await Task.Run(() => SendPluginCommandAndGetResponse(info.Process.Id, BuildLootCommand(lootKeywords)), token);
                    var (lootFound, lootMatched, lootedCount) = ParseLootResult(lootResponse);
                    await Task.Delay(600, token);

                    if (lootedCount > 0)
                    {
                        lootAcquired = true;
                        SetAutoFarmStatus($"已拾取 {lootedCount} 件目标物品，等待状态稳定...");
                        await Task.Delay(3000, token);
                        break;
                    }

                    if (killedCount == 0 && monsterFound == 0 && lootMatched == 0)
                    {
                        SetAutoFarmStatus("附近无怪物且无匹配物品，返回主菜单进入下一轮...");
                        await Task.Delay(1000, token);
                        break;
                    }

                    quietSeconds++;
                    SetAutoFarmStatus(killedCount > 0
                        ? $"击杀 {killedCount} 个怪物，继续刷怪（{quietSeconds}/10）..."
                        : monsterFound > 0
                            ? $"检测到 {monsterFound} 个怪物但未击杀（{quietSeconds}/10）..."
                            : $"等待怪物刷新（{quietSeconds}/10）...");

                    await Task.Delay(1000, token);
                }

                if (!lootAcquired)
                {
                    SetAutoFarmStatus("退出前最后拾取...");
                    await Task.Run(() => SendPluginCommand(info.Process.Id, BuildLootCommand(lootKeywords)), token);
                    await Task.Delay(1000, token);
                }

                SetAutoFarmStatus("退出到主菜单（模拟菜单操作）...");
                await Task.Run(() => SendPluginCommand(info.Process.Id, "one_shot:false"), token);
                await Task.Delay(2000, token);
                var menuExited = await ExitToMainMenuViaUiAsync(info.Process, token);
                if (!menuExited)
                {
                    SetAutoFarmStatus("菜单退出未确认，等待状态变化...");
                    await WaitForMainMenuAsync(info.Process.Id, TimeSpan.FromSeconds(20), token);
                }
                SetAutoFarmStatus("一轮完成");
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            SetAutoFarmStatus($"挂机异常停止：{ex.Message}");
        }
        finally
        {
            if (_godModeEnabled)
            {
                var processInfo = TryGetGameProcess();
                if (processInfo is not null)
                {
                    SendPluginCommand(processInfo.Process.Id, "god_mode:false");
                }

                _godModeEnabled = false;
                UpdateGodModeToggleUi();
            }

            _autoFarmCts?.Dispose();
            _autoFarmCts = null;
            SetAutoFarmUi(running: false);
            SetAutoFarmStatus("自动挂机已停止");
        }
    }

    private GameStateResult QueryGameState(int processId)
    {
        try
        {
            var response = _pluginIpcClient.Send(processId, "is_in_game");
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            if (!root.TryGetProperty("inGame", out var inGame))
            {
                return new GameStateResult(false, true);
            }

            var loading = root.TryGetProperty("loading", out var loadingProperty) && loadingProperty.GetBoolean();
            return new GameStateResult(inGame.GetBoolean(), loading);
        }
        catch
        {
            return new GameStateResult(false, true);
        }
    }

    private async Task<bool> WaitForInGameAsync(int processId, TimeSpan timeout, CancellationToken token)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var state = await Task.Run(() => QueryGameState(processId), token);
            if (state.InGame)
            {
                return true;
            }

            await Task.Delay(1000, token);
        }

        return false;
    }

    private async Task<bool> WaitForMainMenuAsync(int processId, TimeSpan timeout, CancellationToken token)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var state = await Task.Run(() => QueryGameState(processId), token);
            if (!state.InGame)
            {
                return true;
            }

            await Task.Delay(1000, token);
        }

        return false;
    }

    private async Task<bool> WaitForPositionAsync(int processId, TeleportPoint target, TimeSpan timeout, CancellationToken token)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var position = await Task.Run(() => TryQueryPosition(processId), token);
            if (position is { } value)
            {
                var dx = value.X - target.X;
                var dy = value.Y - target.Y;
                var dz = value.Z - target.Z;
                if (dx * dx + dy * dy + dz * dz < 25.0)
                {
                    return true;
                }
            }

            await Task.Delay(500, token);
        }

        return false;
    }

    private Coordinate3? TryQueryPosition(int processId)
    {
        try
        {
            var response = _pluginIpcClient.Send(processId, "get_position");
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            if (root.TryGetProperty("type", out var type)
                && string.Equals(type.GetString(), "position", StringComparison.OrdinalIgnoreCase))
            {
                return new Coordinate3(
                    root.GetProperty("x").GetSingle(),
                    root.GetProperty("y").GetSingle(),
                    root.GetProperty("z").GetSingle());
            }
        }
        catch
        {
        }

        return null;
    }

    private static string BuildLootCommand(string lootKeywords)
    {
        return string.IsNullOrWhiteSpace(lootKeywords)
            ? "loot_items:60"
            : string.Format(CultureInfo.InvariantCulture, "loot_items:60:{0}", lootKeywords.Trim());
    }

    private async Task<bool> WaitForEntitiesAsync(int processId, float radius, TimeSpan timeout, CancellationToken token)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var scan = await Task.Run(() => TryScanEntities(processId, radius), token);
            if (scan is { Total: > 0 })
            {
                return true;
            }

            await Task.Delay(1000, token);
        }

        return false;
    }

    private readonly record struct EntityScanResult(int Total, int MonsterCount);

    private EntityScanResult? TryScanEntities(int processId, float radius)
    {
        try
        {
            var response = _pluginIpcClient.Send(processId, string.Format(
                CultureInfo.InvariantCulture, "list_entity_types:{0}", radius), 30000);
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            var total = root.TryGetProperty("total", out var totalProperty) ? totalProperty.GetInt32() : 0;
            var monsters = root.TryGetProperty("monsterCount", out var monsterProperty) ? monsterProperty.GetInt32() : 0;
            return new EntityScanResult(total, monsters);
        }
        catch
        {
        }

        return null;
    }

    private int? TryGetMonsterCount(int processId, float radius)
    {
        var scan = TryScanEntities(processId, radius);
        return scan?.MonsterCount;
    }

    private void SendPluginCommand(int processId, string command)
    {
        try
        {
            _pluginIpcClient.Send(processId, command);
        }
        catch
        {
        }
    }

    private string? SendPluginCommandAndGetResponse(int processId, string command)
    {
        try
        {
            return _pluginIpcClient.Send(processId, command, 30000);
        }
        catch
        {
            return null;
        }
    }

    private static (int Found, int Matched, int Looted) ParseLootResult(string? response)
    {
        if (string.IsNullOrEmpty(response))
        {
            return (0, 0, 0);
        }

        try
        {
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            var found = root.TryGetProperty("found", out var foundProperty) ? foundProperty.GetInt32() : 0;
            var matched = root.TryGetProperty("matched", out var matchedProperty) ? matchedProperty.GetInt32() : 0;
            var looted = root.TryGetProperty("looted", out var lootedProperty) ? lootedProperty.GetInt32() : 0;
            return (found, matched, looted);
        }
        catch (JsonException)
        {
        }

        return (0, 0, 0);
    }

    private static int ParseChestOpenResult(string? response)
    {
        if (string.IsNullOrEmpty(response))
        {
            return 0;
        }

        try
        {
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            return root.TryGetProperty("called", out var calledProperty) ? calledProperty.GetInt32() : 0;
        }
        catch (JsonException)
        {
        }

        return 0;
    }

    private static int ParseBreakContainersResult(string? response)
    {
        if (string.IsNullOrEmpty(response))
        {
            return 0;
        }

        try
        {
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            return root.TryGetProperty("broken", out var brokenProperty) ? brokenProperty.GetInt32() : 0;
        }
        catch (JsonException)
        {
        }

        return 0;
    }

    private static (int Found, int Killed) ParseKillResult(string? response)
    {
        if (string.IsNullOrEmpty(response))
        {
            return (0, 0);
        }

        try
        {
            using var document = JsonDocument.Parse(response);
            var root = document.RootElement;
            var found = root.TryGetProperty("found", out var foundProperty) ? foundProperty.GetInt32() : 0;
            var killed = root.TryGetProperty("killed", out var killedProperty) ? killedProperty.GetInt32() : 0;
            return (found, killed);
        }
        catch (JsonException)
        {
        }

        return (0, 0);
    }

    private GameProcessInfo? TryGetGameProcess()
    {
        try
        {
            return _teleportService.GetGameProcess();
        }
        catch
        {
            return null;
        }
    }

    private async Task<bool> TryEnterWorldAsync(Process process, CancellationToken token)
    {
        for (var attempt = 0; attempt < 10; attempt++)
        {
            var state = await Task.Run(() => QueryGameState(process.Id), token);
            if (state.InGame)
            {
                return true;
            }

            SetAutoFarmStatus($"进入游戏... 步骤 {attempt + 1}/10");
            await Task.Run(() => NudgeGameToEnterWorld(process, attempt), token);
            await Task.Delay(2500, token);
        }

        return false;
    }

    private static void NudgeGameToEnterWorld(Process process, int attempt)
    {
        try
        {
            process.Refresh();
            var hwnd = process.MainWindowHandle;
            if (hwnd == IntPtr.Zero)
            {
                return;
            }

            ShowWindow(hwnd, 9);
            ShowWindow(hwnd, 5);
            SetForegroundWindow(hwnd);
            Thread.Sleep(400);

            switch (attempt % 6)
            {
                case 0:
                    ClickClientPoint(hwnd, 0.50, 0.94);
                    break;
                case 1:
                    PressEnterKey();
                    break;
                case 2:
                    ClickClientPoint(hwnd, 0.15, 0.45);
                    break;
                case 3:
                    ClickClientPoint(hwnd, 0.50, 0.94);
                    break;
                case 4:
                    PressEnterKey();
                    break;
                case 5:
                    ClickClientPoint(hwnd, 0.50, 0.45);
                    break;
            }
        }
        catch
        {
        }
    }

    private static void ClickClientPoint(IntPtr hwnd, double relativeX, double relativeY)
    {
        if (!GetClientRect(hwnd, out var rect))
        {
            return;
        }

        var point = new NativePoint
        {
            X = (int)(rect.Right * relativeX),
            Y = (int)(rect.Bottom * relativeY)
        };

        if (!ClientToScreen(hwnd, ref point))
        {
            return;
        }

        SetCursorPos(point.X, point.Y);
        Thread.Sleep(120);
        mouse_event(MouseEventLeftDown, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(80);
        mouse_event(MouseEventLeftUp, 0, 0, 0, UIntPtr.Zero);
    }

    private static void PressEnterKey()
    {
        keybd_event(0x0D, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        keybd_event(0x0D, 0, 2, UIntPtr.Zero);
    }

    private const byte VkEscape = 0x1B;
    private const byte EscapeScanCode = 0x01;
    private const byte EnterScanCode = 0x1C;

    private async Task<bool> ExitToMainMenuViaUiAsync(Process process, CancellationToken token)
    {
        await Task.Run(() =>
        {
            process.Refresh();
            var hwnd = process.MainWindowHandle;
            if (hwnd == IntPtr.Zero)
            {
                return;
            }

            ShowWindow(hwnd, 9);
            ShowWindow(hwnd, 5);
            SetForegroundWindow(hwnd);
            Thread.Sleep(400);

            keybd_event(VkEscape, EscapeScanCode, 0, UIntPtr.Zero);
            Thread.Sleep(80);
            keybd_event(VkEscape, EscapeScanCode, 2, UIntPtr.Zero);
        }, token);

        await Task.Delay(1800, token);

        await Task.Run(() =>
        {
            process.Refresh();
            var hwnd = process.MainWindowHandle;
            if (hwnd != IntPtr.Zero)
            {
                ClickClientPoint(hwnd, 0.5148, 0.5306);
            }
        }, token);

        await Task.Delay(2500, token);

        for (var attempt = 0; attempt < 3; attempt++)
        {
            await Task.Run(() =>
            {
                process.Refresh();
                var hwnd = process.MainWindowHandle;
                if (hwnd != IntPtr.Zero)
                {
                    ClickClientPoint(hwnd, 0.4625, 0.5417);
                }
            }, token);

            await Task.Delay(1000, token);
        }

        var exited = await WaitForMainMenuAsync(process.Id, TimeSpan.FromSeconds(20), token);
        if (!exited)
        {
            await Task.Run(() =>
            {
                keybd_event(0x0D, EnterScanCode, 0, UIntPtr.Zero);
                Thread.Sleep(80);
                keybd_event(0x0D, EnterScanCode, 2, UIntPtr.Zero);
            }, token);
            await Task.Delay(1500, token);
            exited = await WaitForMainMenuAsync(process.Id, TimeSpan.FromSeconds(20), token);
        }

        return exited;
    }
}
