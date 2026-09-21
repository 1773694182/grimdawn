using System.IO.Pipes;
using System.Text;

namespace GrimDawnTeleporter;

public sealed class PluginIpcClient
{
    public string Send(int processId, string command, int timeoutMs = 3000)
    {
        var pipeName = $"GrimDawnTeleporter.Plugin.{processId}";
        using var pipe = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.None);
        pipe.Connect(timeoutMs);

        var request = Encoding.UTF8.GetBytes(command.EndsWith('\n') ? command : command + "\n");
        pipe.Write(request, 0, request.Length);
        pipe.Flush();

        var buffer = new byte[65536];
        var readTask = Task.Run(() => pipe.Read(buffer, 0, buffer.Length));
        if (!readTask.Wait(Math.Max(timeoutMs, 5000)))
        {
            throw new TimeoutException($"插件未在预期时间内响应命令：{command}");
        }

        var read = readTask.Result;
        return read > 0
            ? Encoding.UTF8.GetString(buffer, 0, read).Trim()
            : string.Empty;
    }
}
