[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$BackendExe,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SqliteTestExe,

    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',

    [string]$SourceConfigDirectory,
    [string]$RuntimeRoot
)

$ErrorActionPreference = 'Stop'
if (-not $SourceConfigDirectory) { $SourceConfigDirectory = Join-Path $PSScriptRoot '..\config' }

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class CodePilotSseTestProcess
{
    private static TcpListener llmListener;
    private static Thread llmThread;
    private static volatile bool stopLlm;
    private static readonly ManualResetEvent llmRequestReceived = new ManualResetEvent(false);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop;
        public string lpTitle; public int dwX; public int dwY; public int dwXSize;
        public int dwYSize; public int dwXCountChars; public int dwYCountChars;
        public int dwFillAttribute; public int dwFlags; public short wShowWindow;
        public short cbReserved2; public IntPtr hStdInput; public IntPtr hStdOutput;
        public IntPtr hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public IntPtr hProcess; public IntPtr hThread;
        public int dwProcessId; public int dwThreadId;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string applicationName,
        StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
        bool inheritHandles, int creationFlags, IntPtr environment,
        string currentDirectory, ref STARTUPINFO startupInfo,
        out PROCESS_INFORMATION processInformation);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(uint processId);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GenerateConsoleCtrlEvent(uint ctrlEvent, uint processGroupId);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetConsoleCtrlHandler(IntPtr handlerRoutine, bool add);
    private const int CREATE_NEW_CONSOLE = 0x10;
    private const int CREATE_NEW_PROCESS_GROUP = 0x200;
    private const int CREATE_UNICODE_ENVIRONMENT = 0x400;
    private const int STARTF_USESHOWWINDOW = 1;
    private const short SW_HIDE = 0;
    private const uint CTRL_C_EVENT = 0;
    private const uint ATTACH_PARENT_PROCESS = 0xFFFFFFFF;

    public static int Start(string executable, string arguments, string workingDirectory)
    {
        var startupInfo = new STARTUPINFO();
        startupInfo.cb = Marshal.SizeOf(startupInfo);
        startupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION processInformation;
        var commandLine = new StringBuilder("\"" + executable + "\" " + arguments);
        bool created = CreateProcessW(executable, commandLine, IntPtr.Zero,
            IntPtr.Zero, false, CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP |
            CREATE_UNICODE_ENVIRONMENT, IntPtr.Zero, workingDirectory,
            ref startupInfo, out processInformation);
        if (!created) throw new Win32Exception(Marshal.GetLastWin32Error(), "Unable to start backend process");
        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);
        return processInformation.dwProcessId;
    }

    public static bool SendCtrlC(int processId)
    {
        FreeConsole();
        bool attached = AttachConsole((uint)processId);
        if (!attached) { AttachConsole(ATTACH_PARENT_PROCESS); return false; }
        SetConsoleCtrlHandler(IntPtr.Zero, true);
        bool sent = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        Thread.Sleep(250);
        FreeConsole(); AttachConsole(ATTACH_PARENT_PROCESS);
        SetConsoleCtrlHandler(IntPtr.Zero, false);
        return sent;
    }

    public static void StartLocalLlmStub(int port, int delayMilliseconds)
    {
        llmRequestReceived.Reset(); stopLlm = false;
        llmListener = new TcpListener(IPAddress.Loopback, port); llmListener.Start();
        llmThread = new Thread(() => {
            while (!stopLlm) {
                try {
                    using (TcpClient client = llmListener.AcceptTcpClient())
                    using (NetworkStream stream = client.GetStream()) {
                        StreamReader reader = new StreamReader(stream, Encoding.UTF8, false, 4096, true);
                        string line;
                        while (!String.IsNullOrEmpty(line = reader.ReadLine())) { }
                        llmRequestReceived.Set(); Thread.Sleep(delayMilliseconds);
                        string body = "{\"choices\":[{\"message\":{\"content\":\"<done>local cancellation stub</done>\"}}]}";
                        byte[] bodyBytes = Encoding.UTF8.GetBytes(body);
                        byte[] headers = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + bodyBytes.Length + "\r\nConnection: close\r\n\r\n");
                        try {
                            stream.Write(headers, 0, headers.Length); stream.Write(bodyBytes, 0, bodyBytes.Length); stream.Flush();
                        } catch (IOException) {
                            // Expected when task cancellation actively closes
                            // the in-flight LLM connection before this delay ends.
                        }
                    }
                } catch (SocketException) { if (!stopLlm) throw; }
                catch (ObjectDisposedException) { }
            }
        });
        llmThread.IsBackground = true; llmThread.Start();
    }

    public static bool WaitForLocalLlmRequest(int timeoutMilliseconds)
    {
        return llmRequestReceived.WaitOne(timeoutMilliseconds);
    }

    public static void StopLocalLlmStub()
    {
        stopLlm = true;
        if (llmListener != null) llmListener.Stop();
        if (llmThread != null) llmThread.Join(3000);
        llmListener = null; llmThread = null;
    }
}
'@

function Test-TcpPort {
    param([int]$Port)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $async = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($async); return $true
    } catch { return $false } finally { $client.Dispose() }
}

function Wait-TcpPort {
    param([int]$Port, [bool]$ExpectedOpen, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-TcpPort $Port) -eq $ExpectedOpen) { return $true }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Test-ProcessExited {
    param([int]$ProcessId)
    try { return [System.Diagnostics.Process]::GetProcessById($ProcessId).HasExited }
    catch [System.ArgumentException] { return $true }
}

function Wait-ProcessExit {
    param([int]$ProcessId, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-ProcessExited $ProcessId) { return $true }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Invoke-JsonRequest {
    param([string]$Method, [string]$Path, [AllowNull()][string]$Body, [bool]$SendBody = $false)
    $id = [Guid]::NewGuid().ToString('N')
    $bodyFile = Join-Path $requestDirectory "$id-body.json"
    $args = @('--silent', '--show-error', '--max-time', '10', '--request', $Method,
        '--output', $bodyFile, '--write-out', '%{http_code}|%{content_type}')
    if ($SendBody) {
        $inputFile = Join-Path $requestDirectory "$id-request.json"
        [IO.File]::WriteAllText($inputFile, [string]$Body, [Text.UTF8Encoding]::new($false))
        $args += @('--header', 'Content-Type: application/json', '--data-binary', "@$inputFile")
    }
    $url = "$baseUri$Path"
    $metadata = & $curl.Source @args $url
    if ($LASTEXITCODE -ne 0) { throw "curl.exe failed for $Method $url with exit code $LASTEXITCODE." }
    $parts = (($metadata | Out-String).Trim()).Split('|', 2)
    $text = if (Test-Path -LiteralPath $bodyFile) { Get-Content -Raw -LiteralPath $bodyFile } else { '' }
    $json = if ($text) { $text | ConvertFrom-Json } else { $null }
    return [pscustomobject]@{ StatusCode = [int]$parts[0]; ContentType = $parts[1]; Json = $json; Body = $text; Url = $url }
}

function Assert-True { param([bool]$Condition, [string]$Message) if (-not $Condition) { throw $Message } }

function Invoke-SqliteExec {
    param([string]$Sql)
    & $resolvedSqliteTest $databasePath exec $Sql
    if ($LASTEXITCODE -ne 0) { throw 'sqlite test helper exec failed.' }
}

function Invoke-SqliteScalar {
    param([string]$Sql)
    $value = & $resolvedSqliteTest $databasePath scalar $Sql
    if ($LASTEXITCODE -ne 0) { throw 'sqlite test helper scalar failed.' }
    return [string]$value
}

function New-Task {
    param([string]$Name, [ValidateSet('answer', 'workspace')][string]$Mode = 'answer')
    $body = [ordered]@{
        session_id = $sessionId; workspace_id = $workspaceId; input = $Name
        options = [ordered]@{ execution_mode = $Mode; auto_run_safe_commands = $false; require_permission_for_file_write = $true; max_steps = 1 }
    } | ConvertTo-Json -Depth 5 -Compress
    $response = Invoke-JsonRequest POST '/api/v1/tasks' $body $true
    Assert-True ($response.StatusCode -eq 200) "Task creation returned HTTP $($response.StatusCode): $($response.Body)"
    Assert-True ($response.Json.success -eq $true) "Task creation did not return success=true: $($response.Body)"
    return [string]$response.Json.data.id
}

function Get-Task { param([string]$TaskId) return Invoke-JsonRequest GET "/api/v1/tasks/$TaskId" $null }

function Wait-TaskTerminal {
    param([string]$TaskId, [int]$TimeoutSeconds = 15)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $response = Get-Task $TaskId
        if ($response.StatusCode -eq 200 -and @('completed', 'failed', 'cancelled') -contains $response.Json.data.status) { return $response }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Task $TaskId did not reach a terminal state within $TimeoutSeconds seconds."
}

function Start-SseClient {
    param([string]$Name, [string]$TaskId, [int]$MaxSeconds = 25)
    $stdout = Join-Path $streamDirectory "$Name.stdout.txt"
    $stderr = Join-Path $streamDirectory "$Name.stderr.txt"
    $url = "$baseUri/api/v1/tasks/$TaskId/events"
    $arguments = "--silent --show-error --no-buffer --include --max-time $MaxSeconds `"$url`""
    $process = Start-Process -FilePath $curl.Source -ArgumentList $arguments -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
    $client = [pscustomobject]@{ Name = $Name; TaskId = $TaskId; Url = $url; Process = $process; Stdout = $stdout; Stderr = $stderr; TimedOut = $false; ActivelyStopped = $false }
    $script:sseClients.Add($client)
    return $client
}

function Wait-ClientOutput {
    param($Client, [string]$Pattern, [int]$TimeoutSeconds = 10)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-Path -LiteralPath $Client.Stdout) -and (Get-Content -Raw -LiteralPath $Client.Stdout -ErrorAction SilentlyContinue) -match $Pattern) { return $true }
        $Client.Process.Refresh()
        if ($Client.Process.HasExited) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Wait-SseClient {
    param($Client, [int]$TimeoutSeconds = 30)
    if (-not $Client.Process.WaitForExit($TimeoutSeconds * 1000)) {
        $Client.TimedOut = $true
        Stop-Process -Id $Client.Process.Id -Force -ErrorAction SilentlyContinue
        $Client.Process.WaitForExit(3000) | Out-Null
        throw "SSE client '$($Client.Name)' exceeded the test timeout."
    }
}

function Stop-SseClient {
    param($Client)
    $Client.Process.Refresh()
    if (-not $Client.Process.HasExited) {
        $Client.ActivelyStopped = $true
        Stop-Process -Id $Client.Process.Id -Force -ErrorAction SilentlyContinue
        $Client.Process.WaitForExit(3000) | Out-Null
    }
}

function Read-SseResult {
    param($Client)
    $raw = if (Test-Path -LiteralPath $Client.Stdout) { Get-Content -Raw -LiteralPath $Client.Stdout } else { '' }
    $separator = [regex]::Match($raw, "\r?\n\r?\n")
    $headerText = if ($separator.Success) { $raw.Substring(0, $separator.Index) } else { $raw }
    $body = if ($separator.Success) { $raw.Substring($separator.Index + $separator.Length) } else { '' }
    $status = if ($headerText -match '^HTTP/\S+\s+(\d+)') { [int]$matches[1] } else { 0 }
    $contentType = if ($headerText -match '(?im)^Content-Type:\s*([^\r\n]+)') { $matches[1].Trim() } else { '' }
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($frame in [regex]::Split($body, "\r?\n\r?\n")) {
        if (-not $frame.Trim() -or $frame.TrimStart().StartsWith(':')) { continue }
        $eventName = ''; $dataLines = [System.Collections.Generic.List[string]]::new()
        foreach ($line in [regex]::Split($frame, "\r?\n")) {
            if ($line -match '^event:\s?(.*)$') { $eventName = $matches[1] }
            elseif ($line -match '^data:\s?(.*)$') { $dataLines.Add($matches[1]) }
        }
        if (-not $eventName -and $dataLines.Count -eq 0) { continue }
        $dataText = $dataLines -join "`n"; $data = $null
        try { $data = $dataText | ConvertFrom-Json } catch { throw "Invalid SSE JSON in '$($Client.Name)': $dataText" }
        $events.Add([pscustomobject]@{ Event = $eventName; DataText = $dataText; Data = $data })
    }
    return [pscustomobject]@{ Client = $Client; Raw = $raw; StatusCode = $status; ContentType = $contentType; Events = $events }
}

function Assert-SseContract {
    param($Result, [string]$ExpectedTaskId, [bool]$RequireStreamEnd = $true)
    Assert-True ($Result.StatusCode -eq 200) "Expected SSE HTTP 200, got $($Result.StatusCode) for $($Result.Client.Url)."
    Assert-True ($Result.ContentType -match '^text/event-stream(?:\s*;|$)') "Unexpected SSE Content-Type '$($Result.ContentType)'."
    Assert-True ($Result.Events.Count -gt 0) "No SSE frames were parsed for $($Result.Client.Name)."
    foreach ($event in $Result.Events) {
        Assert-True (-not [string]::IsNullOrWhiteSpace($event.Event)) "SSE frame had an empty event name."
        Assert-True ($null -ne $event.Data) "SSE frame data was not JSON."
        if ($event.Event -eq 'stream_end') {
            Assert-True ($event.Data.type -eq 'stream_end') 'stream_end data.type was not stream_end.'
        } else {
            Assert-True ($event.Data.task_id -eq $ExpectedTaskId) "Event '$($event.Event)' carried task_id '$($event.Data.task_id)' instead of '$ExpectedTaskId'."
            Assert-True ($event.Data.type -eq $event.Event) "event '$($event.Event)' did not match data.type '$($event.Data.type)'."
            foreach ($field in @('id', 'content', 'metadata', 'created_at')) {
                Assert-True ($null -ne $event.Data.PSObject.Properties[$field]) "Event '$($event.Event)' lacked data.$field."
            }
        }
    }
    $endCount = @($Result.Events | Where-Object Event -eq 'stream_end').Count
    if ($RequireStreamEnd) { Assert-True ($endCount -eq 1) "Expected exactly one stream_end; got $endCount." }
    $endIndex = -1
    for ($i = 0; $i -lt $Result.Events.Count; $i++) { if ($Result.Events[$i].Event -eq 'stream_end') { $endIndex = $i } }
    if ($endIndex -ge 0) { Assert-True ($endIndex -eq ($Result.Events.Count - 1)) 'A business event appeared after stream_end.' }
}

function Assert-TerminalConsistency {
    param($CancelResponse, $TaskResponse, $SseResult, [string]$TaskId)
    Assert-True ($CancelResponse.StatusCode -eq 200 -and $CancelResponse.Json.success -eq $true) "Cancel failed for $TaskId`: $($CancelResponse.Body)"
    Assert-True ($TaskResponse.StatusCode -eq 200 -and $TaskResponse.Json.success -eq $true) "Final query failed for $TaskId`: $($TaskResponse.Body)"
    $responseStatus = [string]$CancelResponse.Json.data.status
    $databaseStatus = [string]$TaskResponse.Json.data.status
    Assert-True ($responseStatus -eq $databaseStatus) "Cancel response status '$responseStatus' disagreed with final database status '$databaseStatus' for $TaskId."
    $expectedEvent = switch ($databaseStatus) {
        'completed' { 'task_completed' }
        'failed' { 'task_failed' }
        'cancelled' { 'task_cancelled' }
        default { throw "Task $TaskId had non-terminal status '$databaseStatus' after cancellation." }
    }
    $terminalEvents = @($SseResult.Events | Where-Object { $_.Event -in @('task_completed', 'task_failed', 'task_cancelled') })
    Assert-True ($terminalEvents.Count -eq 1) "Expected exactly one terminal event for $TaskId; got $($terminalEvents.Count)."
    Assert-True ($terminalEvents[0].Event -eq $expectedEvent) "Database status '$databaseStatus' expected '$expectedEvent', got '$($terminalEvents[0].Event)' for $TaskId."
    return $databaseStatus
}

function Add-ScenarioResult {
    param([string]$Name, [string]$TaskId, $Result, [bool]$Passed, [bool]$TimedOut = $false, [string]$Note = '')
    $names = @(); $counts = [ordered]@{}
    if ($null -ne $Result) {
        $names = @($Result.Events | ForEach-Object Event)
        foreach ($group in ($names | Group-Object)) { $counts[$group.Name] = $group.Count }
    }
    $script:contractResults.Add([ordered]@{
        scenario = $Name; task_id = $TaskId; connected = ($null -ne $Result -and $Result.StatusCode -eq 200)
        events = $names; event_counts = $counts; stream_end = ($names -contains 'stream_end')
        timed_out = $TimedOut; passed = $Passed; note = $Note
    })
    $countText = @($counts.GetEnumerator() | ForEach-Object { '{0}={1}' -f $_.Key, $_.Value }) -join ', '
    Write-Host ("[SSE] result: passed={0}; task={1}; events=[{2}]; note={3}" -f $Passed, $TaskId, $countText, $Note)
}

function Invoke-Scenario {
    param([string]$Name, [scriptblock]$Body)
    $script:currentScenario = $Name
    Write-Host "[SSE] $Name"
    & $Body
}

$resolvedBackend = (Resolve-Path -LiteralPath $BackendExe).Path
$resolvedSqliteTest = (Resolve-Path -LiteralPath $SqliteTestExe).Path
$resolvedConfig = (Resolve-Path -LiteralPath $SourceConfigDirectory).Path
if (-not $RuntimeRoot) { $RuntimeRoot = Join-Path (Split-Path -Parent $resolvedBackend) 'test-runtime' }
$runDirectory = Join-Path $RuntimeRoot (('{0}-sse-{1}' -f $BuildType, [Guid]::NewGuid().ToString('N')))
$binDirectory = Join-Path $runDirectory 'bin'; $configDirectory = Join-Path $runDirectory 'config'
$workspaceDirectory = Join-Path $runDirectory 'workspace'; $requestDirectory = Join-Path $runDirectory 'test-results\requests'
$streamDirectory = Join-Path $runDirectory 'test-results\streams'; $contractFile = Join-Path $runDirectory 'test-results\backend-sse-contract.json'
$databasePath = Join-Path $runDirectory 'storage\agent.db'
$baseUri = 'http://127.0.0.1:8080'; $backendProcessId = $null; $failure = $null; $gracefulStopRequested = $false; $localLlmStubStarted = $false
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
$sseClients = [System.Collections.Generic.List[object]]::new(); $contractResults = [System.Collections.Generic.List[object]]::new()

try {
    if (-not $curl) { throw 'curl.exe is required for the backend SSE regression test.' }
    if (Test-TcpPort 8080) { throw 'TCP port 8080 is already in use before the test starts.' }
    foreach ($directory in @($binDirectory, $configDirectory, (Join-Path $runDirectory 'logs'),
            (Join-Path $runDirectory 'storage'), $workspaceDirectory, $requestDirectory, $streamDirectory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $runtimeExe = Join-Path $binDirectory (Split-Path -Leaf $resolvedBackend)
    Copy-Item -LiteralPath $resolvedBackend -Destination $runtimeExe
    Get-ChildItem -LiteralPath (Split-Path -Parent $resolvedBackend) -Filter '*.dll' -File | Copy-Item -Destination $binDirectory
    Get-ChildItem -LiteralPath $resolvedConfig -Filter '*.json' -File | Where-Object Name -notlike '*.local.json' | Copy-Item -Destination $configDirectory
    $agentConfig = Join-Path $configDirectory 'agent.json'
    if (-not (Test-Path -LiteralPath $agentConfig -PathType Leaf)) { throw "Required configuration was not copied: $agentConfig" }
    [IO.File]::WriteAllText((Join-Path $configDirectory 'llm.json'), '{"default":"","providers":{}}', [Text.UTF8Encoding]::new($false))

    $backendProcessId = [CodePilotSseTestProcess]::Start($runtimeExe, ('--config "{0}"' -f $agentConfig), $runDirectory)
    Write-Host "Started backend PID $backendProcessId in isolated runtime: $runDirectory"
    if (-not (Wait-TcpPort 8080 $true 20)) { throw 'Backend did not open TCP port 8080 within 20 seconds.' }

    $health = Invoke-JsonRequest GET '/api/v1/health' $null
    Assert-True ($health.StatusCode -eq 200 -and $health.Json.success -eq $true) 'Backend health check failed.'
    $session = Invoke-JsonRequest POST '/api/v1/sessions' '{"title":"SSE regression session"}' $true
    Assert-True ($session.StatusCode -eq 200) "Session creation failed: $($session.Body)"; $sessionId = [string]$session.Json.data.id
    $workspaceJson = [ordered]@{ name = 'SSE regression workspace'; path = $workspaceDirectory.Replace('\', '/') } | ConvertTo-Json -Compress
    $workspace = Invoke-JsonRequest POST '/api/v1/workspaces' $workspaceJson $true
    Assert-True ($workspace.StatusCode -eq 200) "Workspace creation failed: $($workspace.Body)"; $workspaceId = [string]$workspace.Json.data.id

    Invoke-Scenario 'basic connection, history replay, failure termination, and fast completion' {
        $task = New-Task 'SSE regression safe direct answer' 'answer'; $terminal = Wait-TaskTerminal $task
        Assert-True ($terminal.Json.data.status -eq 'failed') "Expected provider-free task failure, got $($terminal.Json.data.status)."
        $client = Start-SseClient 'normal-late-connect' $task; Wait-SseClient $client
        $result = Read-SseResult $client; Assert-SseContract $result $task
        $names = @($result.Events | ForEach-Object Event)
        Assert-True ($names -contains 'task_created') 'Late connection did not replay task_created.'
        Assert-True ($names -contains 'task_failed') 'Provider-free task did not emit task_failed.'
        Assert-True (@($names | Where-Object { $_ -eq 'task_failed' }).Count -eq 1) 'task_failed was duplicated.'
        $ids = @($result.Events | Where-Object Event -ne 'stream_end' | ForEach-Object { $_.Data.id })
        Assert-True (($ids | Select-Object -Unique).Count -eq $ids.Count) 'History/live boundary produced duplicate event IDs.'
        Add-ScenarioResult $script:currentScenario $task $result $true
        $script:terminalTaskId = $task
    }

    Invoke-Scenario 'interrupted database status terminates without duplicate terminal event' {
        $task = 'sse-interrupted-no-event'
        $sql = "INSERT INTO tasks (id,session_id,global_id,workspace_id,goal,status,plan,current_step,created_at,updated_at) VALUES ('$task','$sessionId','','$workspaceId','interrupted SSE regression','interrupted','','',CURRENT_TIMESTAMP,CURRENT_TIMESTAMP);"
        Invoke-SqliteExec $sql
        Assert-True ((Invoke-SqliteScalar "SELECT COUNT(*) FROM task_events WHERE task_id='$task' AND type IN ('task_completed','task_failed','task_cancelled');") -eq '0') 'Interrupted fixture unexpectedly had a terminal event.'

        $client = Start-SseClient 'interrupted-status-only' $task 5
        Wait-SseClient $client 8
        $result = Read-SseResult $client
        Assert-SseContract $result $task
        Assert-True (@($result.Events | Where-Object Event -in @('task_completed', 'task_failed', 'task_cancelled')).Count -eq 0) 'SSE synthesized a duplicate terminal event for interrupted status.'

        $cancel = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
        Assert-True ($cancel.StatusCode -eq 200 -and $cancel.Json.data.status -eq 'interrupted') 'Cancellation changed interrupted task status.'
        Assert-True ((Get-Task $task).Json.data.status -eq 'interrupted') 'Interrupted task was restarted or modified.'
        Assert-True ((Invoke-SqliteScalar "SELECT COUNT(*) FROM task_events WHERE task_id='$task' AND type IN ('task_completed','task_failed','task_cancelled');") -eq '0') 'Cancellation inserted a terminal event for interrupted task.'

        $replay = Start-SseClient 'interrupted-status-only-replay' $task 5
        Wait-SseClient $replay 8
        $replayResult = Read-SseResult $replay
        Assert-SseContract $replayResult $task
        Assert-True (@($replayResult.Events | Where-Object Event -in @('task_completed', 'task_failed', 'task_cancelled')).Count -eq 0) 'Repeated SSE connection inserted a terminal event.'
        Add-ScenarioResult $script:currentScenario $task $result $true $false 'Persisted interrupted status ended both streams without a terminal business event.'
    }

    Invoke-Scenario 'same task reconnect after disconnect' {
        $first = Start-SseClient 'reconnect-first' $script:terminalTaskId; Wait-SseClient $first
        $firstResult = Read-SseResult $first; Assert-SseContract $firstResult $script:terminalTaskId
        $second = Start-SseClient 'reconnect-second' $script:terminalTaskId; Wait-SseClient $second
        $secondResult = Read-SseResult $second; Assert-SseContract $secondResult $script:terminalTaskId
        Assert-True ($secondResult.Events.Count -gt 0) 'Second connection received no replayed events.'
        Add-ScenarioResult $script:currentScenario $script:terminalTaskId $secondResult $true $false 'Both sequential connections completed from replay.'
    }

    Invoke-Scenario 'same task two clients' {
        $one = Start-SseClient 'dual-one' $script:terminalTaskId
        $two = Start-SseClient 'dual-two' $script:terminalTaskId
        Wait-SseClient $one; Wait-SseClient $two
        $oneResult = Read-SseResult $one; $twoResult = Read-SseResult $two
        Assert-SseContract $oneResult $script:terminalTaskId; Assert-SseContract $twoResult $script:terminalTaskId
        Assert-True (@($oneResult.Events | ForEach-Object Event) -contains 'task_failed') 'First client missed task_failed.'
        Assert-True (@($twoResult.Events | ForEach-Object Event) -contains 'task_failed') 'Second client missed task_failed.'
        Add-ScenarioResult $script:currentScenario $script:terminalTaskId $twoResult $true $false 'Both concurrent clients independently received terminal replay and stream_end.'
    }

    Invoke-Scenario 'different task isolation' {
        $taskA = New-Task 'SSE isolation task A' 'answer'; $taskB = New-Task 'SSE isolation task B' 'answer'
        Wait-TaskTerminal $taskA | Out-Null; Wait-TaskTerminal $taskB | Out-Null
        $clientA = Start-SseClient 'isolation-a' $taskA; $clientB = Start-SseClient 'isolation-b' $taskB
        Wait-SseClient $clientA; Wait-SseClient $clientB
        $resultA = Read-SseResult $clientA; $resultB = Read-SseResult $clientB
        Assert-SseContract $resultA $taskA; Assert-SseContract $resultB $taskB
        Assert-True (-not ($resultA.Raw -match [regex]::Escape($taskB))) 'Task B ID appeared in task A stream.'
        Assert-True (-not ($resultB.Raw -match [regex]::Escape($taskA))) 'Task A ID appeared in task B stream.'
        Add-ScenarioResult $script:currentScenario "$taskA,$taskB" $resultA $true $false 'Both streams validated; contract entry summarizes stream A.'
    }

    Invoke-Scenario 'client active disconnect and backend reuse' {
        $unknownTask = 'disconnect-' + [Guid]::NewGuid().ToString('N')
        $client = Start-SseClient 'active-disconnect' $unknownTask 25
        Assert-True (Wait-ClientOutput $client '^HTTP/\S+\s+200' 8) 'SSE connection did not establish before active disconnect.'
        Stop-SseClient $client; Start-Sleep -Seconds 1
        $result = Read-SseResult $client
        Assert-True ($result.StatusCode -eq 200) 'Actively disconnected SSE client did not establish with HTTP 200.'
        $healthAfter = Invoke-JsonRequest GET '/api/v1/health' $null
        Assert-True ($healthAfter.StatusCode -eq 200 -and $healthAfter.Json.success -eq $true) 'Backend health failed after client disconnect.'
        $reuseTask = New-Task 'SSE post-disconnect task' 'answer'; Wait-TaskTerminal $reuseTask | Out-Null
        $reuse = Start-SseClient 'post-disconnect-reuse' $reuseTask; Wait-SseClient $reuse
        $reuseResult = Read-SseResult $reuse; Assert-SseContract $reuseResult $reuseTask
        Add-ScenarioResult $script:currentScenario "$unknownTask->$reuseTask" $reuseResult $true $false 'The first connection was intentionally terminated; backend then created, queried, and streamed a new task.'
    }

    Invoke-Scenario 'provider-free failure termination' {
        $task = New-Task 'SSE workspace task without an LLM' 'workspace'; $terminal = Wait-TaskTerminal $task 20
        Assert-True ($terminal.Json.data.status -eq 'failed') "Expected a failed provider-free workspace task, got $($terminal.Json.data.status)."
        $client = Start-SseClient 'failed-terminal' $task; Wait-SseClient $client
        $result = Read-SseResult $client; Assert-SseContract $result $task
        Assert-True (@($result.Events | ForEach-Object Event) -contains 'task_failed') 'Provider-free workspace task did not emit task_failed.'
        $cancelAfterCompletion = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
        Assert-True ($cancelAfterCompletion.StatusCode -eq 200 -and $cancelAfterCompletion.Json.data.status -eq 'failed') 'Cancellation overwrote a task that failed first.'
        $afterCancel = Get-Task $task
        Assert-True ($afterCancel.Json.data.status -eq 'failed') 'Failed task database status changed after cancellation.'
        Add-ScenarioResult $script:currentScenario $task $result $true
    }

    Invoke-Scenario 'cancel-complete race repeated 20 times with duplicate cancellation' {
        $cancelledCount = 0; $completedCount = 0
        for ($iteration = 1; $iteration -le 20; $iteration++) {
            $task = New-Task "SSE immediate cancellation race $iteration without tools" 'workspace'
            $client = Start-SseClient "cancel-race-$iteration" $task
            $cancel = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
            Wait-SseClient $client
            $result = Read-SseResult $client; Assert-SseContract $result $task
            $terminal = Get-Task $task
            $status = Assert-TerminalConsistency $cancel $terminal $result $task
            if ($status -eq 'cancelled') { $cancelledCount++ } elseif ($status -eq 'completed') { $completedCount++ }

            $repeatCancel = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
            Assert-True ($repeatCancel.StatusCode -eq 200 -and $repeatCancel.Json.data.status -eq $status) "Repeated cancellation changed status for $task."
            $replay = Start-SseClient "cancel-race-$iteration-replay" $task; Wait-SseClient $replay
            $replayResult = Read-SseResult $replay; Assert-SseContract $replayResult $task
            $afterRepeat = Get-Task $task
            Assert-TerminalConsistency $repeatCancel $afterRepeat $replayResult $task | Out-Null
            Add-ScenarioResult "$script:currentScenario iteration $iteration" $task $result $true $false "cancel_status=$status; repeat_status=$($repeatCancel.Json.data.status)"
        }
        Write-Host "[SSE] 20-race summary: cancelled=$cancelledCount; completed=$completedCount"
    }

    Invoke-Scenario 'successful cancellation with local delayed LLM stub' {
        [CodePilotSseTestProcess]::StartLocalLlmStub(18081, 1500); $localLlmStubStarted = $true
        $llmConfig = [ordered]@{ default = 'localstub'; providers = [ordered]@{ localstub = [ordered]@{ name = 'Local test stub'; base_url = 'http://127.0.0.1:18081/v1'; model = 'local-test'; api_key_env = ''; timeout_seconds = 10 } } } | ConvertTo-Json -Depth 5 -Compress
        $setLlm = Invoke-JsonRequest PUT '/api/v1/config/llm' $llmConfig $true
        Assert-True ($setLlm.StatusCode -eq 200 -and $setLlm.Json.success -eq $true) 'Unable to install isolated local LLM test configuration.'
        $setLocalKey = Invoke-JsonRequest PUT '/api/v1/config/llm/local' '{"providers":{"localstub":{"api_key":"local-test-key"}}}' $true
        Assert-True ($setLocalKey.StatusCode -eq 200 -and $setLocalKey.Json.success -eq $true) 'Unable to install isolated local LLM test key.'

        $task = New-Task 'SSE deterministic successful cancellation' 'workspace'
        Assert-True ([CodePilotSseTestProcess]::WaitForLocalLlmRequest(5000)) 'Local LLM stub did not receive the task request.'
        $client = Start-SseClient 'successful-cancel' $task
        $cancel = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
        Assert-True ($cancel.Json.data.status -eq 'cancelled') "Expected successful cancellation, got '$($cancel.Json.data.status)'."
        Wait-SseClient $client; $result = Read-SseResult $client; Assert-SseContract $result $task
        $terminal = Get-Task $task
        $status = Assert-TerminalConsistency $cancel $terminal $result $task
        Assert-True ($status -eq 'cancelled') 'Deterministic cancellation did not remain cancelled.'
        $repeatCancel = Invoke-JsonRequest POST "/api/v1/tasks/$task/cancel" '{}' $true
        Assert-True ($repeatCancel.Json.data.status -eq 'cancelled') 'Repeated successful cancellation changed terminal status.'
        $replay = Start-SseClient 'successful-cancel-replay' $task; Wait-SseClient $replay
        $replayResult = Read-SseResult $replay; Assert-SseContract $replayResult $task
        Assert-TerminalConsistency $repeatCancel (Get-Task $task) $replayResult $task | Out-Null
        Add-ScenarioResult $script:currentScenario $task $result $true $false 'Local-only delayed provider; no external LLM request; repeated cancellation remained idempotent.'
        [CodePilotSseTestProcess]::StopLocalLlmStub(); $localLlmStubStarted = $false
    }

    Assert-True (-not (Test-ProcessExited $backendProcessId)) 'Backend exited before SSE regression scenarios completed.'
    [ordered]@{ generated_at_utc = [DateTime]::UtcNow.ToString('o'); build_type = $BuildType; external_llm_enabled = $false; limitations = @('Provider-free tasks complete quickly, so the 20-run race accepts either cancelled or completed only when HTTP, database, and SSE agree.'); known_production_defects = @(); results = $contractResults } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $contractFile -Encoding UTF8
    Write-Host "Protocol result summary: $contractFile"

    $gracefulStopRequested = $true
    if (-not [CodePilotSseTestProcess]::SendCtrlC($backendProcessId)) { throw 'Unable to send Ctrl+C to the backend process.' }
    if (-not (Wait-ProcessExit $backendProcessId 10)) { throw 'Backend did not exit within 10 seconds after Ctrl+C.' }
} catch {
    $failure = $_
    try {
        [ordered]@{ generated_at_utc = [DateTime]::UtcNow.ToString('o'); build_type = $BuildType; external_llm_enabled = $false; failed_scenario = $currentScenario; error = [string]$_; results = $contractResults } |
            ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $contractFile -Encoding UTF8
    } catch {}
} finally {
    if ($localLlmStubStarted) { [CodePilotSseTestProcess]::StopLocalLlmStub(); $localLlmStubStarted = $false }
    foreach ($client in $sseClients) { Stop-SseClient $client }
    if ($null -ne $backendProcessId -and -not (Test-ProcessExited $backendProcessId)) {
        Stop-Process -Id $backendProcessId -Force -ErrorAction SilentlyContinue
        Wait-ProcessExit $backendProcessId 5 | Out-Null
        if ($gracefulStopRequested -and $null -eq $failure) { $failure = 'Backend required forced termination after a graceful stop request.' }
    }
    if (-not (Wait-TcpPort 8080 $false 10)) { if ($null -eq $failure) { $failure = 'TCP port 8080 was not released after backend shutdown.' } }
}

if ($null -ne $failure) {
    Write-Host "Failed scenario: $currentScenario"
    foreach ($client in $sseClients) {
        Write-Host "SSE client '$($client.Name)': taskId=$($client.TaskId); URL=$($client.Url)"
        if (Test-Path -LiteralPath $client.Stdout) {
            $raw = Get-Content -Raw -LiteralPath $client.Stdout
            Write-Host "Raw SSE output ($($client.Stdout)):`n$raw"
            try {
                $parsed = Read-SseResult $client
                $parsedNames = @($parsed.Events | ForEach-Object Event) -join ', '
                Write-Host "Parsed events: [$parsedNames]"
            } catch { Write-Host "SSE parse diagnostic: $_" }
        }
        if (Test-Path -LiteralPath $client.Stderr) {
            Write-Host "curl stderr ($($client.Stderr)):`n$(Get-Content -Raw -LiteralPath $client.Stderr)"
        }
    }
    Write-Host "Backend log path: $(Join-Path $runDirectory 'logs')"
    Write-Error ("SSE regression failed in scenario '{0}': {1}" -f $currentScenario, $failure)
    Write-Host "Preserved failed test runtime: $runDirectory"
    exit 1
}

Remove-Item -LiteralPath $runDirectory -Recurse -Force
if ((Test-Path -LiteralPath $RuntimeRoot) -and -not (Get-ChildItem -LiteralPath $RuntimeRoot -Force | Select-Object -First 1)) { Remove-Item -LiteralPath $RuntimeRoot -Force }
Write-Host 'Backend SSE regression test passed; isolated runtime removed, no external LLM request was made, clients and backend exited, and TCP port 8080 was released.'
exit 0
