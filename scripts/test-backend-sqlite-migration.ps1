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

if (-not $SourceConfigDirectory) {
    $SourceConfigDirectory = Join-Path $PSScriptRoot '..\config'
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
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
    param([bool]$ExpectedOpen, [int]$TimeoutSeconds = 8)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        if ((Test-TcpPort) -eq $ExpectedOpen) { return $true }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Invoke-SqliteExec {
    param([string]$Database, [string]$Sql)
    $output = & $script:sqliteExe $Database exec $Sql 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "SQLite test setup failed: $($output -join ' ')"
    }
}

function Invoke-SqliteScalar {
    param([string]$Database, [string]$Sql)
    $output = & $script:sqliteExe $Database scalar $Sql 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "SQLite test query failed: $($output -join ' ')"
    }
    return (($output | Out-String).Trim())
}

function Set-DatabaseConfig {
    param([string]$Database, [string]$Workspace = $script:workspaceDirectory)
    $configPath = Join-Path $script:configDirectory 'agent.json'
    $config = Get-Content -LiteralPath $script:sourceAgentConfig -Raw |
        ConvertFrom-Json
    $config.storage.path = $Database
    $config.workspace.root = $Workspace
    [IO.File]::WriteAllText($configPath,
        ($config | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}

function New-ScenarioDatabase {
    param([string]$Name)
    $directory = Join-Path $script:dataDirectory $Name
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    return (Join-Path $directory 'agent.db')
}

function Start-Backend {
    param([string]$Scenario)
    Assert-True (-not (Test-TcpPort)) `
        "Port 8080 was already occupied before scenario '$Scenario'."
    $script:startCounter++
    $outputDirectory = Join-Path $script:resultsDirectory `
        (('{0:D2}-{1}' -f $script:startCounter, $Scenario))
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $stdout = Join-Path $outputDirectory 'stdout.txt'
    $stderr = Join-Path $outputDirectory 'stderr.txt'
    $process = Start-Process -FilePath $script:runtimeExe `
        -ArgumentList '--config', 'config/agent.json' `
        -WorkingDirectory $script:runDirectory `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
        -PassThru -WindowStyle Hidden
    $script:ownedProcessIds.Add($process.Id)
    return [pscustomobject]@{
        Process = $process; Stdout = $stdout; Stderr = $stderr
    }
}

function Wait-Health {
    param($Backend, [int]$TimeoutSeconds = 15)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $Backend.Process.Refresh()
        if ($Backend.Process.HasExited) { return $false }
        try {
            $response = Invoke-WebRequest `
                -Uri 'http://127.0.0.1:8080/api/v1/health' `
                -UseBasicParsing -TimeoutSec 2
            if ($response.StatusCode -eq 200) { return $true }
        }
        catch { }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Stop-Backend {
    param($Backend)
    if ($null -eq $Backend) { return }
    try {
        $Backend.Process.Refresh()
        if (-not $Backend.Process.HasExited) {
            Stop-Process -Id $Backend.Process.Id -Force
            $Backend.Process.WaitForExit(5000) | Out-Null
        }
    }
    finally {
        $Backend.Process.Dispose()
    }
    Assert-True (Wait-TcpPort $false) 'Port 8080 was not released.'
}

function Start-And-AssertHealthy {
    param([string]$Scenario)
    $backend = Start-Backend $Scenario
    try {
        Assert-True (Wait-Health $backend) `
            "Backend did not become healthy in scenario '$Scenario'."
        return $backend
    }
    catch {
        Stop-Backend $backend
        throw
    }
}

function Start-And-ExpectFailure {
    param([string]$Scenario)
    Assert-True (-not (Test-TcpPort)) `
        "Port 8080 was already occupied before scenario '$Scenario'."
    $script:startCounter++
    $outputDirectory = Join-Path $script:resultsDirectory `
        (('{0:D2}-{1}' -f $script:startCounter, $Scenario))
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    $stdoutPath = Join-Path $outputDirectory 'stdout.txt'
    $stderrPath = Join-Path $outputDirectory 'stderr.txt'
    $previousLocation = Get-Location
    $previousErrorAction = $ErrorActionPreference
    try {
        Set-Location -LiteralPath $script:runDirectory
        # Windows PowerShell promotes native stderr to NativeCommandError when
        # ErrorActionPreference is Stop. Keep the real process exit code as the
        # assertion source while redirecting diagnostics to the scenario log.
        $ErrorActionPreference = 'Continue'
        & $script:runtimeExe --config config/agent.json `
            1> $stdoutPath 2> $stderrPath
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorAction
        Assert-True ($exitCode -ne 0) `
            "Backend returned zero after expected failure in '$Scenario'."
        Assert-True (Wait-TcpPort $false) `
            "Port 8080 remained occupied after '$Scenario'."
        return $exitCode
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
        Set-Location -LiteralPath $previousLocation
    }
}

function Assert-MigrationsComplete {
    param([string]$Database)
    $summary = Invoke-SqliteScalar $Database @'
SELECT COUNT(*) || ':' || COUNT(DISTINCT version) || ':' ||
       COALESCE(group_concat(version, ','), '')
FROM (SELECT version FROM schema_migrations ORDER BY version);
'@
    Assert-True ($summary -eq '6:6:1,2,3,4,5,6') `
        "Unexpected migration records: '$summary'."
}

function Assert-RelationIndexes {
    param([string]$Database)
    foreach ($index in @('idx_sessions_workspace_id', 'idx_tasks_session_id',
            'idx_tasks_workspace_id', 'idx_messages_session_sequence',
            'idx_messages_task_id', 'idx_messages_source_event_id',
            'idx_messages_task_assistant_final')) {
        $count = Invoke-SqliteScalar $Database `
            "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='$index';"
        Assert-True ($count -eq '1') "Missing relation index $index."
    }
}

function Assert-Column {
    param([string]$Database, [string]$Table, [string]$Column)
    $count = Invoke-SqliteScalar $Database `
        "SELECT COUNT(*) FROM pragma_table_info('$Table') WHERE name='$Column';"
    Assert-True ($count -eq '1') "Missing column $Table.$Column."
}

function Assert-MigrationColumns {
    param([string]$Database)
    foreach ($item in @(
            @('workspaces', 'description'),
            @('workspaces', 'last_opened_at'),
            @('workspaces', 'permissions_config'),
            @('sessions', 'workspace_id'),
            @('sessions', 'summary'),
            @('sessions', 'summary_updated_at'),
            @('sessions', 'last_active_at'),
            @('tasks', 'user_message_id'),
            @('tasks', 'assistant_message_id'),
            @('messages', 'message_type'),
            @('messages', 'sequence_no'),
            @('messages', 'source_event_id'))) {
        Assert-Column $Database $item[0] $item[1]
    }
}

function Assert-CoreTables {
    param([string]$Database)
    $count = Invoke-SqliteScalar $Database @'
SELECT COUNT(*) FROM sqlite_master
WHERE type='table' AND name IN (
  'globals','global_context','sessions','workspaces','tasks','task_events',
  'tool_calls','permission_requests','file_changes','execution_logs',
  'task_contexts','system_health','schema_migrations','messages',
  'message_context_files','message_attachments'
);
'@
    Assert-True ($count -eq '16') "Expected 16 core tables, found $count."
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
$script:sqliteExe = (Resolve-Path -LiteralPath $SqliteTestExe).Path
$script:resolvedConfig = (Resolve-Path -LiteralPath $SourceConfigDirectory).Path
$script:sourceAgentConfig = Join-Path $script:resolvedConfig 'agent.json'
Assert-True (Test-Path -LiteralPath $script:sourceAgentConfig -PathType Leaf) `
    'Source agent.json is missing.'

if (-not $RuntimeRoot) {
    $RuntimeRoot = Join-Path (Split-Path -Parent $script:resolvedBackend) `
        'test-runtime'
}
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$script:runDirectory = Join-Path $RuntimeRoot `
    (('{0}-sqlite-migration-{1}' -f $BuildType,
        [Guid]::NewGuid().ToString('N')))
$script:binDirectory = Join-Path $script:runDirectory 'bin'
$script:configDirectory = Join-Path $script:runDirectory 'config'
$script:dataDirectory = Join-Path $script:runDirectory 'scenario data'
$script:workspaceDirectory = Join-Path $script:runDirectory 'workspace'
$script:resultsDirectory = Join-Path $script:runDirectory 'test-results'
$script:ownedProcessIds = [Collections.Generic.List[int]]::new()
$script:startCounter = 0
$failure = $null

try {
    Assert-True (-not (Test-TcpPort)) `
        'TCP port 8080 is already in use before migration regression testing.'
    New-Item -ItemType Directory -Path $script:binDirectory,
        $script:configDirectory, $script:dataDirectory,
        $script:workspaceDirectory, $script:resultsDirectory -Force | Out-Null
    $script:runtimeExe = Join-Path $script:binDirectory `
        (Split-Path -Leaf $script:resolvedBackend)
    Copy-Item -LiteralPath $script:resolvedBackend -Destination $script:runtimeExe
    Get-ChildItem -LiteralPath (Split-Path -Parent $script:resolvedBackend) `
        -Filter '*.dll' -File | Copy-Item -Destination $script:binDirectory
    Get-ChildItem -LiteralPath $script:resolvedConfig -Filter '*.json' -File |
        Where-Object { $_.Name -notlike '*.local.json' } |
        Copy-Item -Destination $script:configDirectory
    [IO.File]::WriteAllText((Join-Path $script:configDirectory 'llm.json'),
        '{"default":"","providers":{}}', [Text.UTF8Encoding]::new($false))

    # 1. Fresh database and core schema.
    $freshDatabase = New-ScenarioDatabase 'fresh'
    Set-DatabaseConfig $freshDatabase
    $backend = Start-And-AssertHealthy 'fresh-database'
    Stop-Backend $backend
    Assert-CoreTables $freshDatabase
    Assert-MigrationColumns $freshDatabase
    Assert-MigrationsComplete $freshDatabase
    Assert-RelationIndexes $freshDatabase

    # 2. Ten consecutive starts retain data and never duplicate records.
    Invoke-SqliteExec $freshDatabase @'
INSERT INTO execution_logs(task_id, type, content)
VALUES ('migration-cycle-sentinel', 'test', 'preserve-me');
'@
    for ($cycle = 1; $cycle -le 10; $cycle++) {
        Set-DatabaseConfig $freshDatabase
        $backend = Start-And-AssertHealthy ("ten-starts-$cycle")
        Stop-Backend $backend
        Assert-MigrationsComplete $freshDatabase
        $sentinel = Invoke-SqliteScalar $freshDatabase `
            "SELECT COUNT(*) FROM execution_logs WHERE task_id='migration-cycle-sentinel';"
        Assert-True ($sentinel -eq '1') `
            "Sentinel data changed during startup cycle $cycle."
    }

    # 3. Latest schema with an empty migration ledger.
    $latestDatabase = New-ScenarioDatabase 'latest schema empty ledger'
    Set-DatabaseConfig $latestDatabase
    $backend = Start-And-AssertHealthy 'prepare-latest-empty-ledger'
    Stop-Backend $backend
    Invoke-SqliteExec $latestDatabase 'DELETE FROM schema_migrations;'
    $backend = Start-And-AssertHealthy 'latest-schema-empty-ledger'
    Stop-Backend $backend
    Assert-MigrationColumns $latestDatabase
    Assert-MigrationsComplete $latestDatabase

    # 4. Old tables lack all incremental migration columns.
    $oldDatabase = New-ScenarioDatabase 'old schema'
    Invoke-SqliteExec $oldDatabase @'
CREATE TABLE workspaces (
  id TEXT PRIMARY KEY, name TEXT NOT NULL, path TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE sessions (
  id TEXT PRIMARY KEY, title TEXT NOT NULL, created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE TABLE tasks (
  id TEXT PRIMARY KEY, global_id TEXT NOT NULL, workspace_id TEXT NOT NULL,
  goal TEXT NOT NULL, status TEXT DEFAULT 'pending', plan TEXT DEFAULT '',
  current_step TEXT DEFAULT '', created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
INSERT INTO tasks(id, global_id, workspace_id, goal, created_at, updated_at)
VALUES ('old-task-sentinel', 'old-global', 'old-workspace', 'preserve old row',
        '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');
'@
    Set-DatabaseConfig $oldDatabase
    $backend = Start-And-AssertHealthy 'old-database-upgrade'
    Stop-Backend $backend
    Assert-MigrationColumns $oldDatabase
    Assert-MigrationsComplete $oldDatabase
    Assert-RelationIndexes $oldDatabase
    $oldSentinel = Invoke-SqliteScalar $oldDatabase `
        "SELECT COUNT(*) FROM tasks WHERE id='old-task-sentinel';"
    Assert-True ($oldSentinel -eq '1') 'Old database data was not preserved.'

    # 5. Partial columns and a missing ledger record are reconciled.
    $partialDatabase = New-ScenarioDatabase 'partial migration state'
    Set-DatabaseConfig $partialDatabase
    $backend = Start-And-AssertHealthy 'prepare-partial-migration'
    Stop-Backend $backend
    Invoke-SqliteExec $partialDatabase @'
DELETE FROM schema_migrations WHERE version=2;
ALTER TABLE sessions DROP COLUMN summary_updated_at;
ALTER TABLE tasks DROP COLUMN assistant_message_id;
'@
    $backend = Start-And-AssertHealthy 'partial-migration-state'
    Stop-Backend $backend
    Assert-MigrationColumns $partialDatabase
    Assert-MigrationsComplete $partialDatabase

    # 6. A genuine migration SQL error fails startup and is not recorded.
    $errorDatabase = New-ScenarioDatabase 'real migration error'
    Set-DatabaseConfig $errorDatabase
    $backend = Start-And-AssertHealthy 'prepare-real-migration-error'
    Stop-Backend $backend
    Invoke-SqliteExec $errorDatabase @'
DELETE FROM schema_migrations WHERE version=3;
DROP TABLE message_attachments;
DROP TABLE message_context_files;
DROP INDEX idx_messages_task;
DROP INDEX idx_messages_session;
DROP TABLE messages;
CREATE VIEW messages AS SELECT 'blocked' AS id;
'@
    $errorExit = Start-And-ExpectFailure 'real-migration-sql-error'
    Assert-True ($errorExit -eq 5) `
        "Migration SQL failure returned exit code $errorExit instead of 5."
    $missingRecord = Invoke-SqliteScalar $errorDatabase `
        'SELECT COUNT(*) FROM schema_migrations WHERE version=3;'
    Assert-True ($missingRecord -eq '0') `
        'Failed migration was incorrectly recorded.'
    $viewStillExists = Invoke-SqliteScalar $errorDatabase `
        "SELECT COUNT(*) FROM sqlite_master WHERE type='view' AND name='messages';"
    Assert-True ($viewStillExists -eq '1') `
        'Migration failure unexpectedly replaced the conflicting view.'
    Assert-True (Test-Path -LiteralPath $errorDatabase -PathType Leaf) `
        'Database was deleted after migration failure.'

    # 7. Corrupt input fails without replacing the original bytes.
    $corruptDatabase = New-ScenarioDatabase 'corrupt database'
    [IO.File]::WriteAllBytes($corruptDatabase,
        [Text.Encoding]::UTF8.GetBytes('not-a-sqlite-database-task-8'))
    $corruptHash = (Get-FileHash -LiteralPath $corruptDatabase -Algorithm SHA256).Hash
    Set-DatabaseConfig $corruptDatabase
    $corruptExit = Start-And-ExpectFailure 'corrupt-database'
    Assert-True ($corruptExit -eq 5) `
        "Corrupt database returned exit code $corruptExit instead of 5."
    Assert-True (Test-Path -LiteralPath $corruptDatabase -PathType Leaf) `
        'Corrupt database file was deleted.'
    Assert-True ((Get-FileHash -LiteralPath $corruptDatabase -Algorithm SHA256).Hash `
            -eq $corruptHash) 'Corrupt database file was overwritten.'

    # 8. Absolute database and workspace paths containing spaces work.
    $spaceDatabase = New-ScenarioDatabase 'database path contains spaces'
    $spaceWorkspace = Join-Path $script:runDirectory 'workspace path contains spaces'
    Set-DatabaseConfig $spaceDatabase $spaceWorkspace
    $backend = Start-And-AssertHealthy 'database-path-with-spaces'
    Stop-Backend $backend
    Assert-MigrationsComplete $spaceDatabase

    # 9. HTTP task write/read survives a migration-aware restart.
    $rwDatabase = New-ScenarioDatabase 'post migration read write'
    Set-DatabaseConfig $rwDatabase
    $backend = Start-And-AssertHealthy 'migration-read-write-create'
    $workspaceBody = [ordered]@{
        name = 'Migration persistence'; path = './workspace'
    } | ConvertTo-Json -Compress
    $workspace = Invoke-Json POST '/api/v1/workspaces' $workspaceBody
    $sessionBody = [ordered]@{
        title = 'Migration persistence'; workspace_id = [string]$workspace.data.id
    } | ConvertTo-Json -Compress
    $session = Invoke-Json POST '/api/v1/sessions' $sessionBody
    $taskBody = [ordered]@{
        session_id = [string]$session.data.id
        workspace_id = [string]$workspace.data.id
        input = 'Migration persistence test; no model call is required.'
        options = [ordered]@{ execution_mode = 'answer'; max_steps = 1 }
    } | ConvertTo-Json -Depth 5 -Compress
    $task = Invoke-Json POST '/api/v1/tasks' $taskBody
    $taskId = [string]$task.data.id
    $userMessageId = [string]$task.data.user_message.id
    $messages = Invoke-Json GET "/api/v1/sessions/$([string]$session.data.id)/messages" $null
    Assert-True (@($messages.data.items | Where-Object {
                $_.id -eq $userMessageId -and $_.task_id -eq $taskId }).Count -eq 1) `
        'Task user Message was not persisted exactly once.'
    $queried = Invoke-Json GET "/api/v1/tasks/$taskId" $null
    Assert-True ($queried.success -eq $true -and
        [string]$queried.data.id -eq $taskId) 'Created task could not be queried.'
    Stop-Backend $backend
    Set-DatabaseConfig $rwDatabase
    $backend = Start-And-AssertHealthy 'migration-read-write-restart'
    $persisted = Invoke-Json GET "/api/v1/tasks/$taskId" $null
    Assert-True ($persisted.success -eq $true -and
        [string]$persisted.data.id -eq $taskId) `
        'Task did not persist after restart.'
    $persistedMessages = Invoke-Json GET "/api/v1/sessions/$([string]$session.data.id)/messages" $null
    Assert-True (@($persistedMessages.data.items | Where-Object {
                $_.id -eq $userMessageId -and $_.task_id -eq $taskId }).Count -eq 1) `
        'Task user Message did not persist exactly once after restart.'
    Stop-Backend $backend
    Assert-MigrationsComplete $rwDatabase
}
catch {
    $failure = $_
}
finally {
    foreach ($ownedId in $script:ownedProcessIds) {
        try {
            $process = [Diagnostics.Process]::GetProcessById($ownedId)
            if (-not $process.HasExited) {
                Stop-Process -Id $ownedId -Force -ErrorAction SilentlyContinue
                $process.WaitForExit(5000) | Out-Null
            }
            $process.Dispose()
        }
        catch { }
    }
}

$residual = 0
foreach ($ownedId in $script:ownedProcessIds) {
    try {
        $process = [Diagnostics.Process]::GetProcessById($ownedId)
        if (-not $process.HasExited) { $residual++ }
        $process.Dispose()
    }
    catch { }
}
$portReleased = Wait-TcpPort $false

if ($failure -or $residual -ne 0 -or -not $portReleased) {
    Write-Host "Preserved failed SQLite migration runtime: $script:runDirectory"
    Write-Error "SQLite migration regression failed: $failure; residual=$residual; portReleased=$portReleased"
    exit 1
}

Remove-Item -LiteralPath $script:runDirectory -Recurse -Force
if ((Test-Path -LiteralPath $RuntimeRoot) -and
    -not (Get-ChildItem -LiteralPath $RuntimeRoot -Force | Select-Object -First 1)) {
    Remove-Item -LiteralPath $RuntimeRoot -Force
}

Write-Host ('SQLite migration regression passed: fresh startup, 10 restarts, ' +
    'empty ledger, old schema, partial state, real SQL failure, corrupt file, ' +
    'space-containing path, persistence, and cleanup verified.')
exit 0
