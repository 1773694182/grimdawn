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

        var buffer = new byte[4096];
        var read = pipe.Read(buffer, 0, buffer.Length);
        return Encoding.UTF8.GetString(buffer, 0, read).Trim();
    }
}
