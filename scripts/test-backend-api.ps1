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

public static class CodePilotApiTestProcess
{
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
        bool created = CreateProcessW(executable, commandLine, IntPtr.Zero,
            IntPtr.Zero, false, CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP |
            CREATE_UNICODE_ENVIRONMENT, IntPtr.Zero, workingDirectory,
            ref startupInfo, out processInformation);
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

function Test-TcpPort {
    param([int]$Port)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $asyncResult = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $asyncResult.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($asyncResult)
        return $true
    }
    catch { return $false }
    finally { $client.Dispose() }
}

function Wait-TcpPort {
    param([int]$Port, [bool]$ExpectedOpen, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-TcpPort -Port $Port) -eq $ExpectedOpen) { return $true }
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
        if (Test-ProcessExited -ProcessId $ProcessId) { return $true }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Fail-Assertion {
    param([string]$Message)
    $response = $script:CurrentResponse
    throw ("{0}`nRequest: {1} {2}`nActual status: {3}`nResponse body:`n{4}" -f
        $Message, $response.Method, $response.Url, $response.StatusCode, $response.Body)
}

function Assert-StatusCode {
    param($Response, [int]$Expected)
    if ($Response.StatusCode -ne $Expected) {
        Fail-Assertion "Expected HTTP status $Expected; actual status was $($Response.StatusCode)."
    }
}

function Assert-ContentType {
    param($Response)
    if ($Response.ContentType -notmatch '^application/json(?:\s*;|$)') {
        Fail-Assertion "Expected a JSON Content-Type; actual value was '$($Response.ContentType)'."
    }
}

function Get-JsonField {
    param($Json, [string]$Path)
    $value = $Json
    foreach ($segment in $Path.Split('.')) {
        if ($null -eq $value -or $null -eq $value.PSObject.Properties[$segment]) {
            Fail-Assertion "Expected JSON field '$Path' to exist."
        }
        $value = $value.$segment
    }
    # Prevent PowerShell from unrolling JSON arrays (especially empty arrays)
    # while returning the field value to the type assertion.
    return ,$value
}

function Assert-JsonField {
    param($Json, [string]$Path, $Expected)
    $actual = Get-JsonField -Json $Json -Path $Path
    if ($actual -ne $Expected) {
        Fail-Assertion "Expected JSON field '$Path' to equal '$Expected'; actual value was '$actual'."
    }
}

function Assert-JsonType {
    param($Json, [string]$Path, [ValidateSet('Boolean', 'String', 'Object', 'Array', 'Number')] [string]$Expected)
    $actual = Get-JsonField -Json $Json -Path $Path
    $matches = switch ($Expected) {
        'Boolean' { $actual -is [bool] }
        'String' { $actual -is [string] }
        'Array' { $actual -is [array] }
        'Number' { $actual -is [byte] -or $actual -is [int16] -or $actual -is [int32] -or $actual -is [int64] -or $actual -is [decimal] -or $actual -is [double] }
        'Object' { $null -ne $actual -and $actual -isnot [string] -and $actual -isnot [array] -and $actual -isnot [ValueType] }
    }
    if (-not $matches) {
        $typeName = if ($null -eq $actual) { 'null' } else { $actual.GetType().FullName }
        Fail-Assertion "Expected JSON field '$Path' to have type $Expected; actual type was $typeName."
    }
}

function Assert-Success {
    param($Response, [bool]$Expected)
    Assert-JsonField -Json $Response.Json -Path 'success' -Expected $Expected
    Assert-JsonType -Json $Response.Json -Path 'success' -Expected Boolean
}

function Assert-ErrorCode {
    param($Response, [string]$Expected)
    Assert-Success -Response $Response -Expected $false
    Assert-JsonField -Json $Response.Json -Path 'error.code' -Expected $Expected
    Assert-JsonType -Json $Response.Json -Path 'error.code' -Expected String
    Assert-JsonType -Json $Response.Json -Path 'error.message' -Expected String
}

function Get-Headers {
    param([string]$Path)
    $headers = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([^:]+):\s*(.*)$') {
            $headers[$matches[1].Trim().ToLowerInvariant()] = $matches[2].Trim()
        }
    }
    return $headers
}

function Invoke-JsonRequest {
    param(
        [string]$Name,
        [string]$Method,
        [string]$Path,
        [AllowNull()][string]$Body,
        [bool]$SendBody = $false
    )
    $requestId = [Guid]::NewGuid().ToString('N')
    $bodyFile = Join-Path $requestDirectory "$requestId-body.txt"
    $headerFile = Join-Path $requestDirectory "$requestId-headers.txt"
    $url = "$baseUri$Path"
    $arguments = @('--silent', '--show-error', '--max-time', '10', '--request', $Method,
        '--dump-header', $headerFile, '--output', $bodyFile, '--write-out', '%{http_code}|%{content_type}')
    if ($SendBody) {
        $requestBodyFile = Join-Path $requestDirectory "$requestId-request.json"
        [System.IO.File]::WriteAllText($requestBodyFile, [string]$Body, [System.Text.UTF8Encoding]::new($false))
        $arguments += @('--header', 'Content-Type: application/json', '--data-binary', "@$requestBodyFile")
    }
    $arguments += $url
    $metadata = & $curl.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "curl.exe failed for $Method $url with exit code $LASTEXITCODE."
    }
    $metadataParts = (($metadata | Out-String).Trim()).Split('|', 2)
    $responseBody = if (Test-Path -LiteralPath $bodyFile) { Get-Content -LiteralPath $bodyFile -Raw } else { '' }
    $json = $null
    if ($responseBody) {
        try { $json = $responseBody | ConvertFrom-Json }
        catch {
            throw "Response was not valid JSON.`nRequest: $Method $url`nActual status: $($metadataParts[0])`nResponse body:`n$responseBody"
        }
    }
    $response = [pscustomobject]@{
        Name = $Name; Method = $Method; Path = $Path; Url = $url
        StatusCode = [int]$metadataParts[0]; ContentType = $metadataParts[1]
        Headers = Get-Headers -Path $headerFile; Body = $responseBody; Json = $json
    }
    $script:CurrentResponse = $response
    return $response
}

function Add-ContractResult {
    param($Response, [string[]]$KeyFields = @())
    $successValue = $null
    if ($null -ne $Response.Json -and $null -ne $Response.Json.PSObject.Properties['success']) {
        $successValue = $Response.Json.success
    }
    $contractResults.Add([ordered]@{
        name = $Response.Name; method = $Response.Method; path = $Response.Path
        status_code = $Response.StatusCode; content_type = $Response.ContentType
        success = $successValue; key_fields = $KeyFields; passed = $true
    })
}

$resolvedBackend = (Resolve-Path -LiteralPath $BackendExe).Path
$resolvedConfig = (Resolve-Path -LiteralPath $SourceConfigDirectory).Path
if (-not $RuntimeRoot) { $RuntimeRoot = Join-Path (Split-Path -Parent $resolvedBackend) 'test-runtime' }

$runDirectory = Join-Path $RuntimeRoot (('{0}-api-{1}' -f $BuildType, [Guid]::NewGuid().ToString('N')))
$binDirectory = Join-Path $runDirectory 'bin'
$configDirectory = Join-Path $runDirectory 'config'
$workspaceDirectory = Join-Path $runDirectory 'workspace'
$workspaceWithSpaces = Join-Path $workspaceDirectory 'API workspace with spaces'
$secondWorkspace = Join-Path $workspaceDirectory 'Updated workspace'
$workspaceBDirectory = Join-Path $workspaceDirectory 'Workspace B'
$requestDirectory = Join-Path $runDirectory 'test-results\requests'
$contractFile = Join-Path $runDirectory 'test-results\backend-api-contract.json'
$backendProcessId = $null
$failure = $null
$gracefulStopRequested = $false
$baseUri = 'http://127.0.0.1:8080'
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
$contractResults = [System.Collections.Generic.List[object]]::new()

try {
    if (-not $curl) { throw 'curl.exe is required for the backend API regression test.' }
    if (Test-TcpPort -Port 8080) { throw 'TCP port 8080 is already in use before the test starts.' }

    foreach ($directory in @($binDirectory, $configDirectory, (Join-Path $runDirectory 'logs'),
            (Join-Path $runDirectory 'storage'), $workspaceDirectory, $workspaceWithSpaces,
            $secondWorkspace, $workspaceBDirectory, $requestDirectory)) {
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
    # A provider-free runtime config guarantees the test cannot contact an LLM,
    # even when the parent process has API-key environment variables configured.
    [System.IO.File]::WriteAllText((Join-Path $configDirectory 'llm.json'),
        '{"default":"","providers":{}}', [System.Text.UTF8Encoding]::new($false))

    $arguments = '--config "{0}"' -f $agentConfig
    $backendProcessId = [CodePilotApiTestProcess]::Start($runtimeExe, $arguments, $runDirectory)
    Write-Host "Started backend PID $backendProcessId in isolated runtime: $runDirectory"
    if (-not (Wait-TcpPort -Port 8080 -ExpectedOpen $true -TimeoutSeconds 20)) {
        throw 'Backend did not open TCP port 8080 within 20 seconds.'
    }

    $response = Invoke-JsonRequest -Name 'health' -Method GET -Path '/api/v1/health'
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.status' 'ok'; Assert-JsonType $response.Json 'data.status' String
    Assert-JsonField $response.Json 'data.service' 'codepilot-agent-server'
    Assert-JsonField $response.Json 'data.database.connected' $true
    Add-ContractResult $response @('data.status:string', 'data.service:string', 'data.database.connected:boolean')

    $response = Invoke-JsonRequest -Name 'unknown route' -Method GET -Path '/api/v1/does-not-exist'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    foreach ($invalid in @(
            @{ Name = 'task empty body'; Body = ''; Code = 'INVALID_REQUEST' },
            @{ Name = 'task invalid JSON'; Body = '{invalid'; Code = 'INVALID_REQUEST' },
            @{ Name = 'task missing fields'; Body = '{}'; Code = 'INVALID_REQUEST' },
            @{ Name = 'task wrong field types'; Body = '{"workspace_id":42,"input":[]}'; Code = 'INVALID_REQUEST' })) {
        $response = Invoke-JsonRequest -Name $invalid.Name -Method POST -Path '/api/v1/tasks' -Body $invalid.Body -SendBody $true
        Assert-StatusCode $response 400; Assert-ContentType $response; Assert-ErrorCode $response $invalid.Code
        Add-ContractResult $response @('error.code:string', 'error.message:string')
    }

    $response = Invoke-JsonRequest -Name 'session missing workspace id' -Method POST -Path '/api/v1/sessions' -Body '{"title":"Missing workspace"}' -SendBody $true
    Assert-StatusCode $response 400; Assert-ContentType $response; Assert-ErrorCode $response 'INVALID_REQUEST'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $response = Invoke-JsonRequest -Name 'session unknown workspace' -Method POST -Path '/api/v1/sessions' -Body '{"title":"Unknown workspace","workspace_id":"workspace-does-not-exist"}' -SendBody $true
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'WORKSPACE_NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $response = Invoke-JsonRequest -Name 'missing session' -Method GET -Path '/api/v1/sessions/session-does-not-exist'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'SESSION_NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $workspacePathJson = $workspaceWithSpaces.Replace('\', '/') | ConvertTo-Json -Compress
    $workspaceBody = '{"name":"API workspace","path":' + $workspacePathJson + '}'
    $response = Invoke-JsonRequest -Name 'create workspace with spaces' -Method POST -Path '/api/v1/workspaces' -Body $workspaceBody -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonType $response.Json 'data.id' String; Assert-JsonType $response.Json 'data.path' String
    $workspaceId = $response.Json.data.id; Add-ContractResult $response @('data.id:string', 'data.path:string', 'data.permissions_config:object')

    $updatedPathJson = $secondWorkspace.Replace('\', '/') | ConvertTo-Json -Compress
    $workspaceUpdateBody = '{"name":"Updated workspace","path":' + $updatedPathJson + '}'
    $response = Invoke-JsonRequest -Name 'update workspace' -Method PUT -Path "/api/v1/workspaces/$workspaceId" -Body $workspaceUpdateBody -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.id' $workspaceId; Assert-JsonField $response.Json 'data.name' 'Updated workspace'
    Add-ContractResult $response @('data.id:string', 'data.name:string', 'data.path:string')

    $response = Invoke-JsonRequest -Name 'list workspaces' -Method GET -Path '/api/v1/workspaces'
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonType $response.Json 'data.items' Array; Add-ContractResult $response @('data.items:array')

    $missingPath = (Join-Path $workspaceDirectory 'missing directory').Replace('\', '/') | ConvertTo-Json -Compress
    $response = Invoke-JsonRequest -Name 'workspace missing path' -Method POST -Path '/api/v1/workspaces' -Body ('{"name":"Missing","path":' + $missingPath + '}') -SendBody $true
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'WORKSPACE_PATH_NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $response = Invoke-JsonRequest -Name 'missing workspace' -Method GET -Path '/api/v1/workspaces/workspace-does-not-exist'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'WORKSPACE_NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $workspaceBPathJson = $workspaceBDirectory.Replace('\', '/') | ConvertTo-Json -Compress
    $response = Invoke-JsonRequest -Name 'create workspace B' -Method POST -Path '/api/v1/workspaces' -Body ('{"name":"Workspace B","path":' + $workspaceBPathJson + '}') -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    $workspaceBId = $response.Json.data.id

    $sessionABody = @{ title = 'Workspace A session'; workspace_id = $workspaceId } | ConvertTo-Json -Compress
    $response = Invoke-JsonRequest -Name 'create workspace A session' -Method POST -Path '/api/v1/sessions' -Body $sessionABody -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.workspace_id' $workspaceId
    Assert-JsonType $response.Json 'data.alias' String; Assert-JsonType $response.Json 'data.created_at' String; Assert-JsonType $response.Json 'data.updated_at' String
    $sessionAId = $response.Json.data.id
    Add-ContractResult $response @('data.id:string', 'data.workspace_id:string', 'data.title:string', 'data.alias:string')

    $sessionBBody = @{ title = 'Workspace B session'; workspace_id = $workspaceBId } | ConvertTo-Json -Compress
    $response = Invoke-JsonRequest -Name 'create workspace B session' -Method POST -Path '/api/v1/sessions' -Body $sessionBBody -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.workspace_id' $workspaceBId
    $sessionBId = $response.Json.data.id

    $response = Invoke-JsonRequest -Name 'get workspace A session' -Method GET -Path "/api/v1/sessions/$sessionAId"
    Assert-StatusCode $response 200; Assert-Success $response $true; Assert-JsonField $response.Json 'data.workspace_id' $workspaceId

    $response = Invoke-JsonRequest -Name 'list all sessions' -Method GET -Path '/api/v1/sessions'
    Assert-StatusCode $response 200; Assert-Success $response $true; Assert-JsonType $response.Json 'data.items' Array
    foreach ($session in @($response.Json.data.items)) {
        if ([string]::IsNullOrEmpty([string]$session.workspace_id)) { Fail-Assertion 'Every session response must include workspace_id.' }
    }

    $response = Invoke-JsonRequest -Name 'list workspace A sessions' -Method GET -Path "/api/v1/workspaces/$workspaceId/sessions"
    Assert-StatusCode $response 200; Assert-Success $response $true; Assert-JsonField $response.Json 'data.workspace_id' $workspaceId
    $workspaceASessions = @($response.Json.data.items)
    if (@($workspaceASessions | Where-Object { $_.id -eq $sessionAId }).Count -ne 1) { Fail-Assertion 'Workspace A session list did not include its own session.' }
    if (@($workspaceASessions | Where-Object { $_.id -eq $sessionBId }).Count -ne 0) { Fail-Assertion 'Workspace A session list included a Workspace B session.' }
    foreach ($session in $workspaceASessions) { if ($session.workspace_id -ne $workspaceId) { Fail-Assertion 'Workspace A session list contained a mismatched workspace_id.' } }

    $response = Invoke-JsonRequest -Name 'list missing workspace sessions' -Method GET -Path '/api/v1/workspaces/workspace-does-not-exist/sessions'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'WORKSPACE_NOT_FOUND'

    $response = Invoke-JsonRequest -Name 'list missing session messages' -Method GET -Path '/api/v1/sessions/session-does-not-exist/messages'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'SESSION_NOT_FOUND'

    $response = Invoke-JsonRequest -Name 'update session' -Method PUT -Path "/api/v1/sessions/$sessionAId" -Body '{"title":"Updated API session","alias":"api-test"}' -SendBody $true
    Assert-StatusCode $response 200; Assert-Success $response $true; Assert-JsonField $response.Json 'data.workspace_id' $workspaceId

    $tasksBefore = Invoke-JsonRequest -Name 'list tasks before rejected creates' -Method GET -Path '/api/v1/tasks?page=1&page_size=100'
    Assert-StatusCode $tasksBefore 200; Assert-Success $tasksBefore $true
    $taskCountBefore = @($tasksBefore.Json.data.items).Count
    $sessionCountBefore = @((Invoke-JsonRequest -Name 'list sessions before rejected creates' -Method GET -Path '/api/v1/sessions').Json.data.items).Count

    foreach ($invalidTask in @(
            @{ Name = 'task unknown workspace'; Body = (@{ session_id = $sessionAId; workspace_id = 'workspace-does-not-exist'; input = 'must fail' } | ConvertTo-Json -Compress); Status = 404; Code = 'WORKSPACE_NOT_FOUND' },
            @{ Name = 'task unknown session'; Body = (@{ session_id = 'session-does-not-exist'; workspace_id = $workspaceId; input = 'must fail' } | ConvertTo-Json -Compress); Status = 404; Code = 'SESSION_NOT_FOUND' },
            @{ Name = 'task session workspace mismatch'; Body = (@{ session_id = $sessionAId; workspace_id = $workspaceBId; input = 'must fail' } | ConvertTo-Json -Compress); Status = 409; Code = 'SESSION_WORKSPACE_MISMATCH' })) {
        $response = Invoke-JsonRequest -Name $invalidTask.Name -Method POST -Path '/api/v1/tasks' -Body $invalidTask.Body -SendBody $true
        Assert-StatusCode $response $invalidTask.Status; Assert-ContentType $response; Assert-ErrorCode $response $invalidTask.Code
    }

    $tasksAfter = Invoke-JsonRequest -Name 'list tasks after rejected creates' -Method GET -Path '/api/v1/tasks?page=1&page_size=100'
    Assert-StatusCode $tasksAfter 200; Assert-Success $tasksAfter $true
    if (@($tasksAfter.Json.data.items).Count -ne $taskCountBefore) { Fail-Assertion 'Rejected task requests inserted task rows.' }
    $sessionsAfter = Invoke-JsonRequest -Name 'list sessions after rejected creates' -Method GET -Path '/api/v1/sessions'
    Assert-StatusCode $sessionsAfter 200; Assert-Success $sessionsAfter $true
    if (@($sessionsAfter.Json.data.items).Count -ne $sessionCountBefore) { Fail-Assertion 'Rejected task requests auto-created a session.' }

    $taskSessionBody = @{ title = 'Task API session'; workspace_id = $workspaceId } | ConvertTo-Json -Compress
    $taskSessionResponse = Invoke-JsonRequest -Name 'create task session' -Method POST -Path '/api/v1/sessions' -Body $taskSessionBody -SendBody $true
    Assert-StatusCode $taskSessionResponse 200; Assert-ContentType $taskSessionResponse; Assert-Success $taskSessionResponse $true
    Assert-JsonField $taskSessionResponse.Json 'data.workspace_id' $workspaceId
    $taskSessionId = $taskSessionResponse.Json.data.id; Add-ContractResult $taskSessionResponse @('data.id:string', 'data.workspace_id:string')

    $taskBody = [ordered]@{
        session_id = $taskSessionId; workspace_id = $workspaceId; input = 'API regression: do not call tools.'
        options = [ordered]@{ execution_mode = 'answer'; auto_run_safe_commands = $false; require_permission_for_file_write = $true; max_steps = 1 }
    } | ConvertTo-Json -Depth 5 -Compress
    $response = Invoke-JsonRequest -Name 'create task' -Method POST -Path '/api/v1/tasks' -Body $taskBody -SendBody $true
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonType $response.Json 'data.id' String; Assert-JsonType $response.Json 'data.status' String
    Assert-JsonField $response.Json 'data.session_id' $taskSessionId
    Assert-JsonField $response.Json 'data.workspace_id' $workspaceId
    Assert-JsonField $response.Json 'data.user_message.session_id' $taskSessionId
    Assert-JsonField $response.Json 'data.user_message.role' 'user'
    Assert-JsonField $response.Json 'data.user_message.message_type' 'normal'
    $taskId = $response.Json.data.id; Add-ContractResult $response @('data.id:string', 'data.workspace_id:string', 'data.status:string')
    $userMessageId = [string]$response.Json.data.user_message.id

    $messages = Invoke-JsonRequest -Name 'list session messages' -Method GET -Path "/api/v1/sessions/$taskSessionId/messages"
    Assert-StatusCode $messages 200; Assert-ContentType $messages; Assert-Success $messages $true
    Assert-JsonField $messages.Json 'data.session_id' $taskSessionId
    $createdUserMessages = @($messages.Json.data.items | Where-Object { $_.id -eq $userMessageId -and $_.task_id -eq $taskId })
    if ($createdUserMessages.Count -ne 1) { Fail-Assertion 'Task user Message was missing or duplicated.' }

    $response = Invoke-JsonRequest -Name 'get task' -Method GET -Path "/api/v1/tasks/$taskId"
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.id' $taskId; Assert-JsonType $response.Json 'data.status' String
    Add-ContractResult $response @('data.id:string', 'data.status:string')

    $response = Invoke-JsonRequest -Name 'list tasks' -Method GET -Path '/api/v1/tasks?page=1&page_size=20'
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonType $response.Json 'data.items' Array; Add-ContractResult $response @('data.items:array')

    $response = Invoke-JsonRequest -Name 'missing task' -Method GET -Path '/api/v1/tasks/task-does-not-exist'
    Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'TASK_NOT_FOUND'
    Add-ContractResult $response @('error.code:string', 'error.message:string')

    $cancelStatus = $null
    foreach ($name in @('cancel task', 'cancel task again')) {
        $response = Invoke-JsonRequest -Name $name -Method POST -Path "/api/v1/tasks/$taskId/cancel" -Body '{}' -SendBody $true
        Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
        Assert-JsonField $response.Json 'data.id' $taskId
        if (@('completed', 'failed', 'cancelled') -notcontains $response.Json.data.status) {
            Fail-Assertion "Cancel returned non-terminal status '$($response.Json.data.status)'."
        }
        if ($null -eq $cancelStatus) { $cancelStatus = [string]$response.Json.data.status }
        else { Assert-JsonField $response.Json 'data.status' $cancelStatus }
        Add-ContractResult $response @('data.id:string', 'data.status:string')
    }
    $afterCancel = Invoke-JsonRequest -Name 'get task after cancellation' -Method GET -Path "/api/v1/tasks/$taskId"
    Assert-StatusCode $afterCancel 200; Assert-ContentType $afterCancel; Assert-Success $afterCancel $true
    Assert-JsonField $afterCancel.Json 'data.status' $cancelStatus
    Add-ContractResult $afterCancel @('data.id:string', 'data.status:string')

    $response = Invoke-JsonRequest -Name 'pending permissions' -Method GET -Path '/api/v1/permissions?status=pending'
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonType $response.Json 'data.items' Array; Add-ContractResult $response @('data.items:array')

    foreach ($action in @('approve', 'reject')) {
        $response = Invoke-JsonRequest -Name "missing permission $action" -Method POST -Path "/api/v1/permissions/permission-does-not-exist/$action" -Body '{}' -SendBody $true
        Assert-StatusCode $response 404; Assert-ContentType $response; Assert-ErrorCode $response 'PERMISSION_NOT_FOUND'
        Add-ContractResult $response @('error.code:string', 'error.message:string')
    }

    $response = Invoke-JsonRequest -Name 'CORS preflight' -Method OPTIONS -Path '/api/v1/health'
    Assert-StatusCode $response 204
    foreach ($header in @{
            'access-control-allow-origin' = '*'
            'access-control-allow-methods' = 'GET, POST, PUT, PATCH, DELETE, OPTIONS'
            'access-control-allow-headers' = 'Content-Type'
        }.GetEnumerator()) {
        if ($response.Headers[$header.Key] -ne $header.Value) {
            Fail-Assertion "Expected header '$($header.Key)' to equal '$($header.Value)'; actual value was '$($response.Headers[$header.Key])'."
        }
    }
    Add-ContractResult $response @('Access-Control-Allow-Origin:*', 'Access-Control-Allow-Methods', 'Access-Control-Allow-Headers')

    $deletePathJson = $workspaceWithSpaces.Replace('\', '/') | ConvertTo-Json -Compress
    $deleteBody = '{"name":"Disposable workspace","path":' + $deletePathJson + '}'
    $deleteCreate = Invoke-JsonRequest -Name 'create disposable workspace' -Method POST -Path '/api/v1/workspaces' -Body $deleteBody -SendBody $true
    Assert-StatusCode $deleteCreate 200; Assert-ContentType $deleteCreate; Assert-Success $deleteCreate $true
    $deleteWorkspaceId = $deleteCreate.Json.data.id; Add-ContractResult $deleteCreate @('data.id:string')
    $response = Invoke-JsonRequest -Name 'delete workspace' -Method DELETE -Path "/api/v1/workspaces/$deleteWorkspaceId"
    Assert-StatusCode $response 200; Assert-ContentType $response; Assert-Success $response $true
    Assert-JsonField $response.Json 'data.id' $deleteWorkspaceId; Add-ContractResult $response @('data.id:string')

    [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString('o')
        build_type = $BuildType
        external_llm_enabled = $false
        results = $contractResults
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $contractFile -Encoding UTF8
    Write-Host "Protocol result summary: $contractFile"

    $gracefulStopRequested = $true
    if (-not [CodePilotApiTestProcess]::SendCtrlC($backendProcessId)) {
        throw 'Unable to send Ctrl+C to the backend process.'
    }
    if (-not (Wait-ProcessExit -ProcessId $backendProcessId -TimeoutSeconds 10)) {
        throw 'Backend did not exit within 10 seconds after Ctrl+C.'
    }
}
catch { $failure = $_ }
finally {
    if ($null -ne $backendProcessId -and -not (Test-ProcessExited -ProcessId $backendProcessId)) {
        Stop-Process -Id $backendProcessId -Force -ErrorAction SilentlyContinue
        Wait-ProcessExit -ProcessId $backendProcessId -TimeoutSeconds 5 | Out-Null
        if ($gracefulStopRequested -and $null -eq $failure) {
            $failure = 'Backend required forced termination after a graceful stop request.'
        }
    }
    if (-not (Wait-TcpPort -Port 8080 -ExpectedOpen $false -TimeoutSeconds 10)) {
        if ($null -eq $failure) { $failure = 'TCP port 8080 was not released after backend shutdown.' }
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

Write-Host 'Backend API regression test passed; isolated runtime removed, no LLM provider was enabled, process exited, and TCP port 8080 was released.'
exit 0
