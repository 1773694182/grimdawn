using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Windows;
using GrimDawnTeleporter.Models;
using Microsoft.Win32;

namespace GrimDawnTeleporter;

public partial class MainWindow : Window
{
    private const uint VkF6 = 0x75;
    private const uint VkF7 = 0x76;
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

        AutoDetectAndAttachPlugin();
    }

    private void Window_Closing(object? sender, CancelEventArgs e)
    {
        SavePoints();
        _hotkeyService?.Dispose();
        _processService.CloseStartedProcesses();
    }

    private void DetectProcess_Click(object sender, RoutedEventArgs e)
    {
        RunSafely(() =>
        {
            var info = _teleportService.GetGameProcess();
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
        var text = OutputTextBox.Text;
        if (!string.IsNullOrWhiteSpace(text))
        {
            Clipboard.SetText(text);
            SetStatus("已复制输出内容到剪贴板。");
        }
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
                SetStatus($"已检测到 x86 游戏进程：{info.DisplayName}。插件仅用于 x64，当前保留旧传送模式。");
                return;
            }

            var message = AttachPluginIfNeeded(info.Process.Id);
            SetStatus($"已检测到进程：{info.DisplayName}。{message}");
        }
        catch (Exception ex)
        {
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
}
