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
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class CodePilotTestProcess
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(
        string applicationName,
        StringBuilder commandLine,
        IntPtr processAttributes,
        IntPtr threadAttributes,
        bool inheritHandles,
        int creationFlags,
        IntPtr environment,
        string currentDirectory,
        ref STARTUPINFO startupInfo,
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

    private const int CREATE_NEW_CONSOLE = 0x00000010;
    private const int CREATE_NEW_PROCESS_GROUP = 0x00000200;
    private const int CREATE_UNICODE_ENVIRONMENT = 0x00000400;
    private const int STARTF_USESHOWWINDOW = 0x00000001;
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
        bool created = CreateProcessW(
            executable,
            commandLine,
            IntPtr.Zero,
            IntPtr.Zero,
            false,
            CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
            IntPtr.Zero,
            workingDirectory,
            ref startupInfo,
            out processInformation);

        if (!created)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Unable to start backend process");

        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);
        return processInformation.dwProcessId;
    }

    public static bool SendCtrlC(int processId)
    {
        FreeConsole();
        bool attached = AttachConsole((uint)processId);
        if (!attached)
        {
            AttachConsole(ATTACH_PARENT_PROCESS);
            return false;
        }

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

function Test-TcpPort {
    param([int]$Port)

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $asyncResult = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $asyncResult.AsyncWaitHandle.WaitOne(250)) {
            return $false
        }
        $client.EndConnect($asyncResult)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Wait-TcpPort {
    param(
        [int]$Port,
        [bool]$ExpectedOpen,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-TcpPort -Port $Port) -eq $ExpectedOpen) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Test-ProcessExited {
    param([int]$ProcessId)

    try {
        $process = [System.Diagnostics.Process]::GetProcessById($ProcessId)
        return $process.HasExited
    }
    catch [System.ArgumentException] {
        return $true
    }
}

function Wait-ProcessExit {
    param(
        [int]$ProcessId,
        [int]$TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if (Test-ProcessExited -ProcessId $ProcessId) {
            return $true
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

$resolvedBackend = (Resolve-Path -LiteralPath $BackendExe).Path
$resolvedConfig = (Resolve-Path -LiteralPath $SourceConfigDirectory).Path
if (-not $RuntimeRoot) {
    $RuntimeRoot = Join-Path (Split-Path -Parent $resolvedBackend) 'test-runtime'
}

$runDirectory = Join-Path $RuntimeRoot (('{0}-{1}' -f $BuildType, [Guid]::NewGuid().ToString('N')))
$binDirectory = Join-Path $runDirectory 'bin'
$configDirectory = Join-Path $runDirectory 'config'
$responseFile = Join-Path $runDirectory 'health-response.json'
$backendProcessId = $null
$failure = $null
$gracefulStopRequested = $false

try {
    if (Test-TcpPort -Port 8080) {
        throw 'TCP port 8080 is already in use before the test starts.'
    }

    foreach ($directory in @($binDirectory, $configDirectory,
            (Join-Path $runDirectory 'logs'),
            (Join-Path $runDirectory 'storage'),
            (Join-Path $runDirectory 'workspace'))) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $runtimeExe = Join-Path $binDirectory (Split-Path -Leaf $resolvedBackend)
    Copy-Item -LiteralPath $resolvedBackend -Destination $runtimeExe
    Get-ChildItem -LiteralPath (Split-Path -Parent $resolvedBackend) -Filter '*.dll' -File |
        Copy-Item -Destination $binDirectory
    Get-ChildItem -LiteralPath $resolvedConfig -Filter '*.json' -File |
        Where-Object { $_.Name -notlike '*.local.json' } |
        Copy-Item -Destination $configDirectory

    $agentConfig = Join-Path $configDirectory 'agent.json'
    if (-not (Test-Path -LiteralPath $agentConfig -PathType Leaf)) {
        throw "Required test configuration was not copied: $agentConfig"
    }

    $arguments = '--config "{0}"' -f $agentConfig
    $backendProcessId = [CodePilotTestProcess]::Start($runtimeExe, $arguments, $runDirectory)
    Write-Host "Started backend PID $backendProcessId in isolated runtime: $runDirectory"

    if (-not (Wait-TcpPort -Port 8080 -ExpectedOpen $true -TimeoutSeconds 20)) {
        throw 'Backend did not open TCP port 8080 within 20 seconds.'
    }

    $healthUri = 'http://127.0.0.1:8080/api/v1/health'
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        $httpCode = & $curl.Source --silent --show-error --max-time 5 --output $responseFile --write-out '%{http_code}' $healthUri
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed with exit code $LASTEXITCODE."
        }
        if (($httpCode | Out-String).Trim() -ne '200') {
            throw "Health endpoint returned HTTP $httpCode instead of 200."
        }
        $payload = Get-Content -LiteralPath $responseFile -Raw | ConvertFrom-Json
    }
    else {
        $payload = Invoke-RestMethod -Uri $healthUri -Method Get -TimeoutSec 5
        $payload | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $responseFile -Encoding UTF8
    }

    if ($payload.success -ne $true) {
        throw 'Health response did not contain success=true.'
    }
    if ($payload.data.database.connected -ne $true) {
        throw 'Health response did not contain data.database.connected=true.'
    }

    $gracefulStopRequested = $true
    if (-not [CodePilotTestProcess]::SendCtrlC($backendProcessId)) {
        throw 'Unable to send Ctrl+C to the backend process.'
    }
    if (-not (Wait-ProcessExit -ProcessId $backendProcessId -TimeoutSeconds 10)) {
        throw 'Backend did not exit within 10 seconds after Ctrl+C.'
    }
}
catch {
    $failure = $_
}
finally {
    if ($null -ne $backendProcessId -and -not (Test-ProcessExited -ProcessId $backendProcessId)) {
        Stop-Process -Id $backendProcessId -Force -ErrorAction SilentlyContinue
        Wait-ProcessExit -ProcessId $backendProcessId -TimeoutSeconds 5 | Out-Null
        if ($gracefulStopRequested -and $null -eq $failure) {
            $failure = 'Backend required forced termination after a graceful stop request.'
        }
    }

    if (-not (Wait-TcpPort -Port 8080 -ExpectedOpen $false -TimeoutSeconds 10)) {
        if ($null -eq $failure) {
            $failure = 'TCP port 8080 was not released after backend shutdown.'
        }
    }
}

if ($null -ne $failure) {
    Write-Error $failure
    Write-Host "Preserved failed test runtime: $runDirectory"
    exit 1
}

Remove-Item -LiteralPath $runDirectory -Recurse -Force
if ((Test-Path -LiteralPath $RuntimeRoot) -and
    -not (Get-ChildItem -LiteralPath $RuntimeRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $RuntimeRoot -Force
}

Write-Host 'Backend health integration test passed; process exited and TCP port 8080 was released.'
exit 0
