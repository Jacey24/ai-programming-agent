param(
    [Parameter(Mandatory = $true)]
    [string]$BackendExe,
    [string]$SourceConfigDirectory = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
if (-not $SourceConfigDirectory) {
    $SourceConfigDirectory = Join-Path $PSScriptRoot '..\config'
}

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Test-Port([int]$Port) {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $pending = $client.BeginConnect('127.0.0.1', $Port, $null, $null)
        if (-not $pending.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($pending)
        return $true
    } catch { return $false } finally { $client.Dispose() }
}

function Invoke-Api([string]$Method, [string]$Path, [object]$Body = $null) {
    $request = [System.Net.Http.HttpRequestMessage]::new(
        [System.Net.Http.HttpMethod]::new($Method), "http://127.0.0.1:8080$Path")
    try {
        if ($null -ne $Body) {
            $json = $Body | ConvertTo-Json -Compress -Depth 8
            $request.Content = [System.Net.Http.StringContent]::new(
                $json, [Text.Encoding]::UTF8, 'application/json')
        }
        $response = $script:httpClient.SendAsync($request).GetAwaiter().GetResult()
        $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $parsed = if ($text) { $text | ConvertFrom-Json } else { $null }
        return [pscustomobject]@{
            Status = [int]$response.StatusCode
            Json = $parsed
            Text = $text
        }
    } finally {
        $request.Dispose()
    }
}

$resolvedBackend = (Resolve-Path -LiteralPath $BackendExe).Path
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$runtime = Join-Path $tempBase ("codepilot-file-save-" + [Guid]::NewGuid().ToString('N'))
$bin = Join-Path $runtime 'bin'
$config = Join-Path $runtime 'config'
$workspace = Join-Path $runtime 'workspace\中文 目录'
$workspaceB = Join-Path $runtime 'workspace-b'
$logs = Join-Path $runtime 'logs'
$storage = Join-Path $runtime 'storage'
$backend = $null
$script:httpClient = [System.Net.Http.HttpClient]::new()

try {
    Assert-True (-not (Test-Port 8080)) 'TCP port 8080 is already in use.'
    foreach ($directory in @($bin, $config, $workspace, $workspaceB, $logs, $storage)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $runtimeExe = Join-Path $bin (Split-Path -Leaf $resolvedBackend)
    Copy-Item -LiteralPath $resolvedBackend -Destination $runtimeExe
    Get-ChildItem -LiteralPath (Split-Path -Parent $resolvedBackend) -Filter '*.dll' -File |
        Copy-Item -Destination $bin
    Get-ChildItem -LiteralPath $SourceConfigDirectory -Filter '*.json' -File |
        Where-Object { $_.Name -notlike '*.local.json' } |
        Copy-Item -Destination $config
    [IO.File]::WriteAllText((Join-Path $config 'llm.json'),
        '{"default":"","providers":{}}', [Text.UTF8Encoding]::new($false))

    $original = "int main() {`r`n  return 0;`r`n}`r`n"
    $helloPath = Join-Path $workspace 'hello.cpp'
    [IO.File]::WriteAllText($helloPath, $original, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllBytes((Join-Path $workspace 'program.exe'), [byte[]](77, 90, 0, 1))
    [IO.File]::WriteAllText((Join-Path $workspace 'bom.txt'), '初始', [Text.UTF8Encoding]::new($true))
    $outsidePath = Join-Path $runtime 'outside.txt'
    [IO.File]::WriteAllText($outsidePath, 'sentinel', [Text.UTF8Encoding]::new($false))

    $backend = Start-Process -FilePath $runtimeExe -ArgumentList '--config', 'config/agent.json' `
        -WorkingDirectory $runtime -RedirectStandardOutput (Join-Path $logs 'stdout.log') `
        -RedirectStandardError (Join-Path $logs 'stderr.log') -PassThru -WindowStyle Hidden
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while (-not (Test-Port 8080) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    Assert-True (Test-Port 8080) 'Backend did not open port 8080.'

    $create = Invoke-Api POST '/api/v1/workspaces' @{ name = '中文 Workspace'; path = $workspace }
    Assert-True ($create.Status -eq 200 -and $create.Json.success) "Workspace create failed: $($create.Text)"
    $workspaceId = [string]$create.Json.data.id
    $encodedHello = [Uri]::EscapeDataString('hello.cpp')

    $read = Invoke-Api GET "/api/v1/workspaces/$workspaceId/files/content?path=$encodedHello"
    Assert-True ($read.Status -eq 200) "File read failed: $($read.Text)"
    Assert-True ($read.Json.data.content -ceq $original) 'Read changed original CRLF or trailing newline.'
    Assert-True ($read.Json.data.encoding -eq 'utf-8') 'UTF-8 encoding metadata was not preserved.'

    $updated = "// 中文 保存`r`nint main() { return 1; }`r`n"
    $save = Invoke-Api PUT "/api/v1/workspaces/$workspaceId/files/content?path=$encodedHello" @{
        content = $updated
        encoding = 'utf-8'
    }
    Assert-True ($save.Status -eq 200 -and $save.Json.success) "File save failed: $($save.Text)"
    Assert-True ([IO.File]::ReadAllText($helloPath) -ceq $updated) 'Saved bytes were not written to the original file.'

    $binary = Invoke-Api GET "/api/v1/workspaces/$workspaceId/files/content?path=program.exe"
    Assert-True ($binary.Status -eq 415 -and $binary.Json.error.code -eq 'BINARY_FILE') 'Binary file was not rejected.'

    $traversal = Invoke-Api PUT "/api/v1/workspaces/$workspaceId/files/content?path=..%2Foutside.txt" @{
        content = 'overwritten'
        encoding = 'utf-8'
    }
    Assert-True ($traversal.Status -eq 400 -and $traversal.Json.error.code -eq 'INVALID_PATH') 'Traversal path was not rejected.'
    Assert-True ([IO.File]::ReadAllText($outsidePath) -ceq 'sentinel') 'Traversal request changed an external file.'

    $bomRead = Invoke-Api GET "/api/v1/workspaces/$workspaceId/files/content?path=bom.txt"
    Assert-True ($bomRead.Json.data.encoding -eq 'utf-8-bom') 'UTF-8 BOM was not detected.'
    $bomSave = Invoke-Api PUT "/api/v1/workspaces/$workspaceId/files/content?path=bom.txt" @{
        content = '更新'
        encoding = 'utf-8-bom'
    }
    Assert-True ($bomSave.Status -eq 200) "BOM file save failed: $($bomSave.Text)"
    $bomBytes = [IO.File]::ReadAllBytes((Join-Path $workspace 'bom.txt'))
    Assert-True ($bomBytes[0] -eq 239 -and $bomBytes[1] -eq 187 -and $bomBytes[2] -eq 191) 'UTF-8 BOM was lost.'

    $createB = Invoke-Api POST '/api/v1/workspaces' @{ name = 'Workspace B'; path = $workspaceB }
    $workspaceBId = [string]$createB.Json.data.id
    $wrongWorkspace = Invoke-Api PUT "/api/v1/workspaces/$workspaceBId/files/content?path=$encodedHello" @{
        content = 'wrong workspace'
        encoding = 'utf-8'
    }
    Assert-True ($wrongWorkspace.Status -eq 404) 'Old relative path unexpectedly saved into a new Workspace.'
    Assert-True ([IO.File]::ReadAllText($helloPath) -ceq $updated) 'Wrong-Workspace request changed the old file.'

    Write-Host 'Workspace file save interface test passed.'
} finally {
    $script:httpClient.Dispose()
    if ($backend -and -not $backend.HasExited) {
        Stop-Process -Id $backend.Id -Force -ErrorAction SilentlyContinue
        $backend.WaitForExit(5000) | Out-Null
    }
    if (Test-Path -LiteralPath $runtime) {
        $resolvedRuntime = [IO.Path]::GetFullPath($runtime)
        Assert-True ($resolvedRuntime.StartsWith($tempBase + [IO.Path]::DirectorySeparatorChar) -and
            (Split-Path -Leaf $resolvedRuntime) -like 'codepilot-file-save-*') `
            "Refusing to remove unexpected runtime path: $resolvedRuntime"
        Remove-Item -LiteralPath $runtime -Recurse -Force
    }
}
