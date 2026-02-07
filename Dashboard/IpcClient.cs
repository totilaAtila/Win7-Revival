using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace CrystalFrame.Dashboard
{
    public class StatusData
    {
        public TaskbarStatus Taskbar { get; set; }
        public StartStatus Start { get; set; }
    }

    public class TaskbarStatus
    {
        public bool Found { get; set; }
        public string Edge { get; set; }
        public bool AutoHide { get; set; }
        public bool Enabled { get; set; }
        public int Opacity { get; set; }
    }

    public class StartStatus
    {
        public bool Detected { get; set; }
        public bool IsOpen { get; set; }
        public float Confidence { get; set; }
        public bool Enabled { get; set; }
        public int Opacity { get; set; }
    }

    public class StatusUpdateEventArgs : EventArgs
    {
        public StatusData Status { get; set; }
    }

    public class IpcClient : IDisposable
    {
        private NamedPipeClientStream _pipe;
        private StreamReader _reader;
        private StreamWriter _writer;
        private Task _listenerTask;
        private CancellationTokenSource _cts;
        private bool _disposed = false;
        
        // ✅ ADDED: Signal for first successful connection
        private TaskCompletionSource<bool> _firstConnectionTcs;
        
        // Reconnection settings
        private static readonly int[] RetryDelaysMs = { 1000, 2000, 4000, 8000, 15000 };
        private const int MaxRetryDelay = 15000;

        public event EventHandler<StatusUpdateEventArgs> StatusUpdated;
        public event EventHandler<string> ErrorReceived;
        public event EventHandler<bool> ConnectionChanged;
        
        public bool IsConnected { get; private set; }

        // ✅ MODIFIED: Now returns when first connection succeeds (or timeout)
        public async Task ConnectAsync()
        {
            _cts = new CancellationTokenSource();
            _firstConnectionTcs = new TaskCompletionSource<bool>();
            
            // Start background reconnection loop (fire-and-forget)
            _ = ConnectWithRetryAsync(_cts.Token);
            
            // Wait for first connection (max 10 seconds)
            var timeoutTask = Task.Delay(10000);
            var completedTask = await Task.WhenAny(_firstConnectionTcs.Task, timeoutTask);
            
            if (completedTask == timeoutTask)
            {
                throw new TimeoutException("Failed to connect to Core within 10 seconds");
            }
            
            var connected = await _firstConnectionTcs.Task;
            if (!connected)
            {
                throw new Exception("Initial connection failed");
            }
        }

        private async Task ConnectWithRetryAsync(CancellationToken ct)
        {
            int attempt = 0;
            bool firstConnectionAttempted = false;
            
            while (!ct.IsCancellationRequested)
            {
                try
                {
                    _pipe = new NamedPipeClientStream(".", "CrystalFrame", PipeDirection.InOut);
                    await _pipe.ConnectAsync(5000);
                    
                    _reader = new StreamReader(_pipe, Encoding.UTF8);
                    _writer = new StreamWriter(_pipe, Encoding.UTF8) { AutoFlush = true };
                    
                    IsConnected = true;
                    ConnectionChanged?.Invoke(this, true);
                    Debug.WriteLine("IPC connected to Core");
                    
                    // ✅ ADDED: Signal first connection success
                    if (!firstConnectionAttempted)
                    {
                        _firstConnectionTcs?.TrySetResult(true);
                        firstConnectionAttempted = true;
                    }
                    
                    attempt = 0;  // Reset on successful connection
                    
                    // Start listener (will return when connection drops)
                    await ListenAsync(ct);
                }
                catch (Exception ex) when (!ct.IsCancellationRequested)
                {
                    Debug.WriteLine($"IPC connection failed: {ex.Message}");
                    
                    // ✅ ADDED: Signal first connection failure
                    if (!firstConnectionAttempted)
                    {
                        // Don't fail immediately, will retry
                        firstConnectionAttempted = false;
                    }
                }
                
                // Connection lost or failed — clean up and retry
                CleanupConnection();
                IsConnected = false;
                ConnectionChanged?.Invoke(this, false);
                
                if (ct.IsCancellationRequested) break;
                
                int delay = attempt < RetryDelaysMs.Length 
                    ? RetryDelaysMs[attempt] 
                    : MaxRetryDelay;
                attempt++;
                
                Debug.WriteLine($"IPC reconnecting in {delay}ms (attempt {attempt})...");
                
                try { await Task.Delay(delay, ct); }
                catch (TaskCanceledException) { break; }
            }
            
            // ✅ ADDED: If loop exits without connection, signal failure
            if (!firstConnectionAttempted)
            {
                _firstConnectionTcs?.TrySetResult(false);
            }
        }

        private void CleanupConnection()
        {
            _writer?.Dispose();
            _writer = null;
            _reader?.Dispose();
            _reader = null;
            _pipe?.Dispose();
            _pipe = null;
        }

        private async Task ListenAsync(CancellationToken ct)
        {
            try
            {
                while (!ct.IsCancellationRequested)
                {
                    var line = await _reader.ReadLineAsync();
                    if (string.IsNullOrEmpty(line))
                    {
                        Debug.WriteLine("IPC connection closed by Core");
                        return;  // Return to reconnect loop
                    }

                    HandleMessage(line);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"IPC listener error: {ex.Message}");
                // Return to reconnect loop
            }
        }

        private void HandleMessage(string json)
        {
            try
            {
                using var doc = JsonDocument.Parse(json);
                var type = doc.RootElement.GetProperty("type").GetString();

                switch (type)
                {
                    case "StatusUpdate":
                        var dataElement = doc.RootElement.GetProperty("data");
                        var status = new StatusData
                        {
                            Taskbar = new TaskbarStatus
                            {
                                Found = dataElement.GetProperty("taskbar").GetProperty("found").GetBoolean(),
                                Edge = dataElement.GetProperty("taskbar").GetProperty("edge").GetString(),
                                AutoHide = dataElement.GetProperty("taskbar").GetProperty("autoHide").GetBoolean(),
                                Enabled = dataElement.GetProperty("taskbar").GetProperty("enabled").GetBoolean(),
                                Opacity = dataElement.GetProperty("taskbar").GetProperty("opacity").GetInt32()
                            },
                            Start = new StartStatus
                            {
                                Detected = dataElement.GetProperty("start").GetProperty("detected").GetBoolean(),
                                IsOpen = dataElement.GetProperty("start").GetProperty("isOpen").GetBoolean(),
                                Confidence = (float)dataElement.GetProperty("start").GetProperty("confidence").GetDouble(),
                                Enabled = dataElement.GetProperty("start").GetProperty("enabled").GetBoolean(),
                                Opacity = dataElement.GetProperty("start").GetProperty("opacity").GetInt32()
                            }
                        };

                        StatusUpdated?.Invoke(this, new StatusUpdateEventArgs { Status = status });
                        break;

                    case "Error":
                        var errorMsg = doc.RootElement.GetProperty("data").GetProperty("message").GetString();
                        ErrorReceived?.Invoke(this, errorMsg);
                        break;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"IPC message parse error: {ex.Message}");
            }
        }

        public async Task SendCommandAsync(string type, object data)
        {
            if (!IsConnected || _writer == null) return;
            
            try
            {
                var msg = new
                {
                    type = type,
                    data = data
                };

                var json = JsonSerializer.Serialize(msg);
                await _writer.WriteLineAsync(json);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"IPC send error: {ex.Message}");
            }
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            
            _cts?.Cancel();
            _listenerTask?.Wait(2000);
            CleanupConnection();
            _cts?.Dispose();
        }
    }
}
