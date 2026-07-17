[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$BackendExe,

    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',

    [string]$SourceConfigDirectory,

    [string]$RuntimeRoot
)

$ErrorActionPreference = 'Stop'

if (-not $SourceConfigDirectory) {
    $SourceConfigDirectory = Join-Path $PSScriptRoot '..\config'
}

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class CodePilotStartupProcess
{
    [StructLayout(LayoutKind.Sequential)]
    private struct SECURITY_ATTRIBUTES
    {
        public int nLength;
        public IntPtr lpSecurityDescriptor;
        public int bInheritHandle;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop;
        public string lpTitle; public int dwX; public int dwY; public int dwXSize;
        public int dwYSize; public int dwXCountChars; public int dwYCountChars;
        public int dwFillAttribute; public int dwFlags; public short wShowWindow;
        public short cbReserved2; public IntPtr lpReserved2; public IntPtr hStdInput;
        public IntPtr hStdOutput; public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public IntPtr hProcess; public IntPtr hThread;
        public int dwProcessId; public int dwThreadId;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(string fileName, uint desiredAccess,
        uint shareMode, ref SECURITY_ATTRIBUTES securityAttributes,
        uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);
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
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

    private const uint GENERIC_READ = 0x80000000;
    private const uint GENERIC_WRITE = 0x40000000;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint CREATE_ALWAYS = 2;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
    private const int CREATE_NEW_CONSOLE = 0x00000010;
    private const int CREATE_NEW_PROCESS_GROUP = 0x00000200;
    private const int CREATE_UNICODE_ENVIRONMENT = 0x00000400;
    private const int STARTF_USESHOWWINDOW = 0x00000001;
    private const int STARTF_USESTDHANDLES = 0x00000100;
    private const short SW_HIDE = 0;
    private const uint CTRL_C_EVENT = 0;
    private const uint ATTACH_PARENT_PROCESS = 0xFFFFFFFF;
    private static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);
    private static readonly object ProcessHandlesLock = new object();
    private static readonly Dictionary<int, IntPtr> ProcessHandles =
        new Dictionary<int, IntPtr>();

    public static int Start(string executable, string arguments,
        string workingDirectory, string stdoutPath, string stderrPath)
    {
        var security = new SECURITY_ATTRIBUTES();
        security.nLength = Marshal.SizeOf(security);
        security.bInheritHandle = 1;
        IntPtr stdoutHandle = CreateFileW(stdoutPath, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, ref security, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
        IntPtr stderrHandle = CreateFileW(stderrPath, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, ref security, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
        IntPtr stdinHandle = CreateFileW("NUL", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, ref security, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
        if (stdoutHandle == INVALID_HANDLE_VALUE ||
            stderrHandle == INVALID_HANDLE_VALUE || stdinHandle == INVALID_HANDLE_VALUE)
            throw new Win32Exception(Marshal.GetLastWin32Error(),
                "Unable to create redirected process handles");

        try
        {
            var startupInfo = new STARTUPINFO();
            startupInfo.cb = Marshal.SizeOf(startupInfo);
            startupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startupInfo.wShowWindow = SW_HIDE;
            startupInfo.hStdInput = stdinHandle;
            startupInfo.hStdOutput = stdoutHandle;
            startupInfo.hStdError = stderrHandle;
            PROCESS_INFORMATION processInformation;
            var commandLine = new StringBuilder("\"" + executable + "\" " + arguments);
            bool created = CreateProcessW(executable, commandLine, IntPtr.Zero,
                IntPtr.Zero, true, CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP |
                CREATE_UNICODE_ENVIRONMENT, IntPtr.Zero, workingDirectory,
                ref startupInfo, out processInformation);
            if (!created)
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to start backend process");
            CloseHandle(processInformation.hThread);
            lock (ProcessHandlesLock)
                ProcessHandles[processInformation.dwProcessId] = processInformation.hProcess;
            return processInformation.dwProcessId;
        }
        finally
        {
            CloseHandle(stdinHandle);
            CloseHandle(stdoutHandle);
            CloseHandle(stderrHandle);
        }
    }

    public static int GetExitCode(int processId)
    {
        IntPtr handle;
        lock (ProcessHandlesLock)
        {
            if (!ProcessHandles.TryGetValue(processId, out handle))
                throw new InvalidOperationException("Process handle is unavailable");
            ProcessHandles.Remove(processId);
        }
        try
        {
            uint exitCode;
            if (!GetExitCodeProcess(handle, out exitCode))
                throw new Win32Exception(Marshal.GetLastWin32Error(),
                    "Unable to read backend exit code");
            return unchecked((int)exitCode);
        }
        finally { CloseHandle(handle); }
    }

    public static void Release(int processId)
    {
        IntPtr handle;
        lock (ProcessHandlesLock)
        {
            if (!ProcessHandles.TryGetValue(processId, out handle)) return;
            ProcessHandles.Remove(processId);
        }
        CloseHandle(handle);
    }

    public static bool SendCtrlC(int processId)
    {
        FreeConsole();
        bool attached = AttachConsole((uint)processId);
        if (!attached) { AttachConsole(ATTACH_PARENT_PROCESS); return false; }
        SetConsoleCtrlHandler(IntPtr.Zero, true);
        bool sent = GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        Thread.Sleep(250);
        FreeConsole();
        AttachConsole(ATTACH_PARENT_PROCESS);
        SetConsoleCtrlHandler(IntPtr.Zero, false);
        return sent;
    }
}
'@

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Write-TestMessage {
    param([string]$Message)
    try { Write-Host $Message } catch { }
}

function Test-TcpPort {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $pending = $client.BeginConnect('127.0.0.1', 8080, $null, $null)
        if (-not $pending.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($pending)
        return $true
    }
    catch { return $false }
    finally { $client.Dispose() }
}

function Wait-TcpPort {
    param([bool]$ExpectedOpen, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-TcpPort) -eq $ExpectedOpen) { return $true }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Wait-ProcessExit {
    param([Diagnostics.Process]$Process, [int]$TimeoutSeconds)
    return $Process.WaitForExit($TimeoutSeconds * 1000)
}

function Stop-OwnedProcess {
    param([AllowNull()][Diagnostics.Process]$Process)
    if ($null -eq $Process) { return }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(5000) | Out-Null
        }
    }
    catch { }
    try { [CodePilotStartupProcess]::Release($Process.Id) } catch { }
}

function Assert-RunChild {
    param([string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $script:runDirectory.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify path outside isolated runtime: $full"
    }
}

function Reset-Runtime {
    param([switch]$CreateDirectories)
    foreach ($path in @($script:configDirectory, $script:logsDirectory,
            $script:storageDirectory, $script:workspaceDirectory)) {
        Assert-RunChild $path
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Path $script:configDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $script:resolvedConfig -Filter '*.json' -File |
        Where-Object { $_.Name -notlike '*.local.json' } |
        Copy-Item -Destination $script:configDirectory
    Assert-True (-not (Get-ChildItem -LiteralPath $script:configDirectory -Filter '*.local.json' -File)) `
        'A *.local.json file entered the isolated runtime.'
    if ($CreateDirectories) {
        New-Item -ItemType Directory -Path $script:logsDirectory,
            $script:storageDirectory, $script:workspaceDirectory -Force | Out-Null
    }
}

function Start-Backend {
    param([string]$Scenario, [string]$WorkingDirectory)
    $scenarioDirectory = Join-Path $script:resultsDirectory $Scenario
    New-Item -ItemType Directory -Path $scenarioDirectory -Force | Out-Null
    $stdout = Join-Path $scenarioDirectory 'stdout.txt'
    $stderr = Join-Path $scenarioDirectory 'stderr.txt'
    $pidValue = [CodePilotStartupProcess]::Start(
        $script:runtimeExe, '--config "config/agent.json"',
        $WorkingDirectory, $stdout, $stderr)
    $process = [Diagnostics.Process]::GetProcessById($pidValue)
    $script:ownedBackendIds.Add($pidValue)
    return [pscustomobject]@{
        Process = $process; Stdout = $stdout; Stderr = $stderr
        ExitCodeFile = (Join-Path $scenarioDirectory 'exit-code.txt')
    }
}

function Get-CombinedOutput {
    param($Backend)
    $text = ''
    foreach ($path in @($Backend.Stdout, $Backend.Stderr)) {
        if (Test-Path -LiteralPath $path) {
            $text += [IO.File]::ReadAllText($path)
        }
    }
    return $text
}

function Save-ExitCode {
    param($Backend)
    $Backend.Process.Refresh()
    Assert-True $Backend.Process.HasExited 'Cannot save an exit code for a running backend.'
    $exitCode = [CodePilotStartupProcess]::GetExitCode($Backend.Process.Id)
    [IO.File]::WriteAllText($Backend.ExitCodeFile,
        [string]$exitCode, [Text.UTF8Encoding]::new($false))
    return [int]$exitCode
}

function Wait-Health {
    param($Backend, [int]$TimeoutSeconds = 15)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $Backend.Process.Refresh()
        if ($Backend.Process.HasExited) { return 0 }
        try {
            $response = Invoke-WebRequest -Uri 'http://127.0.0.1:8080/api/v1/health' `
                -UseBasicParsing -TimeoutSec 2
            if ($response.StatusCode -eq 200) { return 200 }
        }
        catch { }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    return 0
}

function Stop-Gracefully {
    param($Backend)
    Assert-True ([CodePilotStartupProcess]::SendCtrlC($Backend.Process.Id)) `
        'Unable to send Ctrl+C to the backend.'
    Assert-True (Wait-ProcessExit $Backend.Process 12) `
        'Backend did not exit within 12 seconds after Ctrl+C.'
    $exitCode = Save-ExitCode $Backend
    Assert-True ($exitCode -eq 0) "Graceful backend exit code was $exitCode."
    Assert-True (Wait-TcpPort $false 8) 'Port 8080 was not released after graceful shutdown.'
}

function Add-Scenario {
    param(
        [string]$Name, [bool]$StartupSucceeded, [AllowNull()]$ExitCode,
        [int]$HealthResult, [bool]$DirectoriesCreated,
        [bool]$ExpectedErrorDetected, [bool]$TimedOut,
        [bool]$ResidualProcess, [bool]$PortReleased, $Details
    )
    $script:scenarios.Add([ordered]@{
        Name = $Name
        StartupSucceeded = $StartupSucceeded
        ExitCode = $ExitCode
        HealthResult = $HealthResult
        NecessaryDirectoriesCreated = $DirectoriesCreated
        ExpectedErrorDetected = $ExpectedErrorDetected
        TimedOut = $TimedOut
        ResidualProcess = $ResidualProcess
        Port8080Released = $PortReleased
        Details = $Details
    })
    Write-TestMessage "[startup] PASS: $Name"
}

function Assert-NoSensitiveOutput {
    param([string]$Output)
    Assert-True (-not $Output.Contains('TASK2A-SECRET-SENTINEL')) `
        'Sensitive configuration content appeared in process output.'
    Assert-True (-not ($Output -match '(?i)Authorization\s*:')) `
        'Authorization header appeared in process output.'
}

function Start-PortPlaceholder {
    param([string]$ScenarioDirectory)
    $command = '$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,8080);' +
        '$listener.Start();try{while($true){Start-Sleep -Seconds 1}}finally{$listener.Stop()}'
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    return Start-Process -FilePath (Get-Command powershell).Source `
        -ArgumentList '-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded `
        -RedirectStandardOutput (Join-Path $ScenarioDirectory 'placeholder-stdout.txt') `
        -RedirectStandardError (Join-Path $ScenarioDirectory 'placeholder-stderr.txt') `
        -PassThru -WindowStyle Hidden
}

function Invoke-Json {
    param([string]$Method, [string]$Path, [AllowNull()]$Body)
    $parameters = @{
        Uri = "http://127.0.0.1:8080$Path"
        Method = $Method
        TimeoutSec = 5
        ContentType = 'application/json'
    }
    if ($null -ne $Body) { $parameters.Body = $Body }
    return Invoke-RestMethod @parameters
}

$script:resolvedBackend = (Resolve-Path -LiteralPath $BackendExe).Path
$script:resolvedConfig = (Resolve-Path -LiteralPath $SourceConfigDirectory).Path
if (-not $RuntimeRoot) {
    $RuntimeRoot = Join-Path (Split-Path -Parent $script:resolvedBackend) 'test-runtime'
}
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$script:runDirectory = Join-Path $RuntimeRoot `
    (('{0}-startup-{1}' -f $BuildType, [Guid]::NewGuid().ToString('N')))
$script:binDirectory = Join-Path $script:runDirectory 'bin'
$script:configDirectory = Join-Path $script:runDirectory 'config'
$script:logsDirectory = Join-Path $script:runDirectory 'logs'
$script:storageDirectory = Join-Path $script:runDirectory 'storage'
$script:workspaceDirectory = Join-Path $script:runDirectory 'workspace'
$script:resultsDirectory = Join-Path $script:runDirectory 'test-results'
$reportFile = Join-Path $script:resultsDirectory 'backend-startup-report.json'
$script:scenarios = [Collections.Generic.List[object]]::new()
$script:ownedBackendIds = [Collections.Generic.List[int]]::new()
$backend = $null
$placeholder = $null
$unrelatedDirectory = $null
$failure = $null
$currentScenario = 'setup'
$timedOut = $false

try {
    Assert-True (-not (Test-TcpPort)) 'TCP port 8080 is already in use before the test starts.'
    New-Item -ItemType Directory -Path $script:binDirectory,
        $script:resultsDirectory -Force | Out-Null
    $script:runtimeExe = Join-Path $script:binDirectory `
        (Split-Path -Leaf $script:resolvedBackend)
    Copy-Item -LiteralPath $script:resolvedBackend -Destination $script:runtimeExe
    Get-ChildItem -LiteralPath (Split-Path -Parent $script:resolvedBackend) `
        -Filter '*.dll' -File | Copy-Item -Destination $script:binDirectory

    $currentScenario = 'normal-startup-and-shutdown'
    Reset-Runtime -CreateDirectories
    $backend = Start-Backend $currentScenario $script:binDirectory
    $health = Wait-Health $backend
    Assert-True ($health -eq 200) 'Normal startup did not return health 200.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $true $false $false $false $true `
        ([ordered]@{ WorkingDirectory = 'bin'; GracefulShutdown = $true })
    $backend = $null

    $currentScenario = 'unrelated-working-directory'
    Reset-Runtime -CreateDirectories
    $unrelatedDirectory = Join-Path ([IO.Path]::GetTempPath()) `
        ('codepilot-startup-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $unrelatedDirectory | Out-Null
    $backend = Start-Backend $currentScenario $unrelatedDirectory
    $health = Wait-Health $backend
    Assert-True ($health -eq 200) 'Startup from an unrelated CWD did not return health 200.'
    Assert-True (Test-Path (Join-Path $script:storageDirectory 'agent.db') -PathType Leaf) `
        'Database was not created under the executable runtime root.'
    Assert-True (-not (Test-Path (Join-Path $unrelatedDirectory 'storage'))) `
        'Backend incorrectly created runtime data under the caller CWD.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $true $false $false $false $true `
        ([ordered]@{ RuntimeAnchoredToExecutable = $true })
    $backend = $null
    Remove-Item -LiteralPath $unrelatedDirectory -Recurse -Force
    $unrelatedDirectory = $null

    $currentScenario = 'missing-runtime-directories'
    Reset-Runtime
    $backend = Start-Backend $currentScenario $script:runDirectory
    $health = Wait-Health $backend
    $created = (Test-Path $script:logsDirectory -PathType Container) -and
        (Test-Path $script:storageDirectory -PathType Container) -and
        (Test-Path $script:workspaceDirectory -PathType Container)
    Assert-True ($health -eq 200 -and $created) `
        'Backend did not create all required runtime directories.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $created $false $false $false $true @{}
    $backend = $null

    $currentScenario = 'storage-path-is-file'
    Reset-Runtime
    New-Item -ItemType File -Path $script:storageDirectory | Out-Null
    New-Item -ItemType Directory -Path $script:logsDirectory,
        $script:workspaceDirectory | Out-Null
    $backend = Start-Backend $currentScenario $script:runDirectory
    $exited = Wait-ProcessExit $backend.Process 8
    Assert-True $exited 'Backend hung when storage path was a file.'
    $exitCode = Save-ExitCode $backend
    $output = Get-CombinedOutput $backend
    Assert-NoSensitiveOutput $output
    $expected = $output -match '(?i)storage.+(not a directory|failed to create)'
    Assert-True ($exitCode -ne 0 -and $expected -and -not (Test-TcpPort)) `
        'Storage path failure was not diagnosed with a nonzero exit code.'
    Add-Scenario $currentScenario $false $exitCode 0 $false $expected $false $false $true @{}
    $backend = $null

    $currentScenario = 'sqlite-initialization-failure'
    Reset-Runtime -CreateDirectories
    New-Item -ItemType Directory -Path (Join-Path $script:storageDirectory 'agent.db') | Out-Null
    $backend = Start-Backend $currentScenario $script:runDirectory
    $exited = Wait-ProcessExit $backend.Process 8
    Assert-True $exited 'Backend hung after SQLite initialization failed.'
    $exitCode = Save-ExitCode $backend
    $output = Get-CombinedOutput $backend
    Assert-NoSensitiveOutput $output
    $expected = $output -match '(?i)SQLite.+agent\.db'
    Assert-True ($exitCode -eq 5 -and $expected -and -not (Test-TcpPort)) `
        'SQLite failure did not return exit code 5 with a path-aware diagnostic.'
    Add-Scenario $currentScenario $false $exitCode 0 $true $expected $false $false $true @{}
    $backend = $null

    $currentScenario = 'broken-required-config-json'
    Reset-Runtime -CreateDirectories
    [IO.File]::WriteAllText((Join-Path $script:configDirectory 'agent.json'),
        '{"api_key":"TASK2A-SECRET-SENTINEL",', [Text.UTF8Encoding]::new($false))
    $backend = Start-Backend $currentScenario $script:runDirectory
    $exited = Wait-ProcessExit $backend.Process 8
    Assert-True $exited 'Backend hung after required configuration JSON was corrupted.'
    $exitCode = Save-ExitCode $backend
    $output = Get-CombinedOutput $backend
    Assert-NoSensitiveOutput $output
    $expected = ($output -match '(?i)Configuration') -and
        ($output -match '(?i)agent\.json') -and ($output -match '(?i)invalid JSON')
    Assert-True ($exitCode -eq 2 -and $expected -and -not (Test-TcpPort)) `
        'Broken required JSON did not return exit code 2 with the config path.'
    Add-Scenario $currentScenario $false $exitCode 0 $true $expected $false $false $true `
        ([ordered]@{ SensitiveContentDetected = $false })
    $backend = $null

    $currentScenario = 'no-llm-provider'
    Reset-Runtime -CreateDirectories
    [IO.File]::WriteAllText((Join-Path $script:configDirectory 'llm.json'),
        '{"default":"","providers":{}}', [Text.UTF8Encoding]::new($false))
    $backend = Start-Backend $currentScenario $script:runDirectory
    $health = Wait-Health $backend
    Assert-True ($health -eq 200) 'Backend did not start without an LLM provider.'
    $workspaceBody = [ordered]@{
        name = 'Startup validation'; path = './workspace'
    } | ConvertTo-Json -Compress
    $workspace = Invoke-Json POST '/api/v1/workspaces' $workspaceBody
    $sessionBody = [ordered]@{
        title = 'Startup validation'; workspace_id = [string]$workspace.data.id
    } | ConvertTo-Json -Compress
    $session = Invoke-Json POST '/api/v1/sessions' $sessionBody
    $taskBody = [ordered]@{
        session_id = [string]$session.data.id
        workspace_id = [string]$workspace.data.id
        input = 'Local startup validation; do not access a model.'
        options = [ordered]@{ execution_mode = 'answer'; max_steps = 1 }
    } | ConvertTo-Json -Depth 5 -Compress
    $task = Invoke-Json POST '/api/v1/tasks' $taskBody
    $taskId = [string]$task.data.id
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $taskState = Invoke-Json GET "/api/v1/tasks/$taskId" $null
        if ($taskState.data.status -in @('completed', 'failed', 'cancelled')) { break }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    Assert-True ($taskState.data.status -eq 'failed') `
        'A model-dependent task did not fail clearly without an LLM provider.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $true $true $false $false $true `
        ([ordered]@{ ModelTaskStatus = 'failed'; ExternalModelRequests = $false })
    $backend = $null

    $currentScenario = 'existing-database-restart'
    $backend = Start-Backend $currentScenario $script:runDirectory
    $health = Wait-Health $backend
    Assert-True ($health -eq 200) `
        'Backend could not restart with its existing compatibility database.'
    $persistedTask = Invoke-Json GET "/api/v1/tasks/$taskId" $null
    Assert-True ($persistedTask.success -eq $true -and
        $persistedTask.data.status -eq 'failed') `
        'Existing task data was not preserved across database restart.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $true $true $false $false $true `
        ([ordered]@{ ExistingTaskPreserved = $true })
    $backend = $null

    $currentScenario = 'port-8080-occupied'
    Reset-Runtime -CreateDirectories
    $scenarioDirectory = Join-Path $script:resultsDirectory $currentScenario
    New-Item -ItemType Directory -Path $scenarioDirectory -Force | Out-Null
    $placeholder = Start-PortPlaceholder $scenarioDirectory
    Assert-True (Wait-TcpPort $true 8) 'Test placeholder did not bind localhost:8080.'
    $backend = Start-Backend $currentScenario $script:runDirectory
    $exited = Wait-ProcessExit $backend.Process 8
    Assert-True $exited 'Backend did not exit after HTTP port binding failed.'
    $exitCode = Save-ExitCode $backend
    $output = Get-CombinedOutput $backend
    Assert-NoSensitiveOutput $output
    $placeholder.Refresh()
    $expected = $output -match '(?i)HttpServer.+(bind|port)'
    Assert-True ($exitCode -eq 6 -and $expected -and -not $placeholder.HasExited) `
        'Port binding failure was not diagnosed or the placeholder was stopped.'
    Stop-Process -Id $placeholder.Id -Force
    $placeholder.WaitForExit(5000) | Out-Null
    $placeholder = $null
    Assert-True (Wait-TcpPort $false 8) 'Port 8080 remained occupied after placeholder cleanup.'
    Add-Scenario $currentScenario $false $exitCode 0 $true $expected $false $false $true `
        ([ordered]@{ PlaceholderSurvivedBackendFailure = $true })
    $backend = $null

    $currentScenario = 'recovery-after-failure'
    Reset-Runtime -CreateDirectories
    $backend = Start-Backend $currentScenario $script:runDirectory
    $health = Wait-Health $backend
    Assert-True ($health -eq 200) 'Backend did not recover after failed startup environments were repaired.'
    Stop-Gracefully $backend
    Add-Scenario $currentScenario $true 0 $health $true $false $false $false $true @{}
    $backend = $null

    $currentScenario = 'five-consecutive-start-stop-cycles'
    Reset-Runtime -CreateDirectories
    for ($cycle = 1; $cycle -le 5; $cycle++) {
        $backend = Start-Backend ("$currentScenario-$cycle") $script:runDirectory
        $health = Wait-Health $backend
        Assert-True ($health -eq 200) "Health failed during startup cycle $cycle."
        Stop-Gracefully $backend
        $backend = $null
    }
    Add-Scenario $currentScenario $true 0 200 $true $false $false $false $true `
        ([ordered]@{ Cycles = 5 })
}
catch {
    $failure = $_
    if ([string]$_ -match '(?i)timed out|timeout|hung') { $timedOut = $true }
}
finally {
    if ($null -ne $backend) { Stop-OwnedProcess $backend.Process }
    if ($null -ne $placeholder) {
        try {
            $placeholder.Refresh()
            if (-not $placeholder.HasExited) { Stop-Process -Id $placeholder.Id -Force }
            $placeholder.WaitForExit(5000) | Out-Null
        }
        catch { }
    }
    if ($unrelatedDirectory -and (Test-Path -LiteralPath $unrelatedDirectory)) {
        Remove-Item -LiteralPath $unrelatedDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }

    $residual = 0
    foreach ($ownedId in $script:ownedBackendIds) {
        try {
            $process = [Diagnostics.Process]::GetProcessById($ownedId)
            if (-not $process.HasExited) { $residual++ }
        }
        catch { }
    }
    $portReleased = Wait-TcpPort $false 8
    $safeError = if ($failure) {
        ([string]$failure + "`n" + [string]$failure.ScriptStackTrace).Trim()
    } else { $null }
    foreach ($path in @($script:runDirectory, $RuntimeRoot,
            $script:resolvedBackend, $script:resolvedConfig)) {
        if ($safeError -and $path) { $safeError = $safeError.Replace($path, '<test-path>') }
    }
    if ($failure -and -not ($script:scenarios | Where-Object Name -eq $currentScenario)) {
        $script:scenarios.Add([ordered]@{
            Name = $currentScenario; StartupSucceeded = $false; ExitCode = $null
            HealthResult = 0; NecessaryDirectoriesCreated = $false
            ExpectedErrorDetected = $false; TimedOut = $timedOut
            ResidualProcess = ($residual -gt 0); Port8080Released = $portReleased
            Details = [ordered]@{ FailedAssertion = $true }
        })
    }
    $passed = ($null -eq $failure) -and ($residual -eq 0) -and $portReleased
    $report = [ordered]@{
        BuildType = $BuildType
        Scenarios = $script:scenarios
        TimedOut = $timedOut
        ResidualBackendProcesses = $residual
        Port8080Released = $portReleased
        ExternalModelRequests = $false
        SensitiveOutputDetected = $false
        Passed = $passed
        Error = $safeError
    }
    try {
        New-Item -ItemType Directory -Path $script:resultsDirectory -Force | Out-Null
        $report | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath $reportFile -Encoding UTF8
    }
    catch { }
}

if ($failure -or -not $portReleased -or $residual -ne 0) {
    Write-TestMessage "Preserved failed startup runtime: $script:runDirectory"
    Write-TestMessage "Report: $reportFile"
    try {
        Write-Error "Backend startup validation failed in '$currentScenario': $failure"
    }
    catch { }
    exit 1
}

Remove-Item -LiteralPath $script:runDirectory -Recurse -Force
if ((Test-Path -LiteralPath $RuntimeRoot) -and
    -not (Get-ChildItem -LiteralPath $RuntimeRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $RuntimeRoot -Force
}

Write-TestMessage ('Backend startup validation passed ({0} scenarios); temporary runtime removed, ' +
    'all owned processes stopped, port 8080 released, and no external model was used.' -f
    $script:scenarios.Count)
exit 0
