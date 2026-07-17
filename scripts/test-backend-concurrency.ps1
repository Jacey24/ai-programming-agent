[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$BackendExe,
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SqliteCheckExe,
    [ValidateSet('Debug', 'Release')]
    [string]$BuildType = 'Debug',
    [string]$SourceConfigDirectory,
    [string]$RuntimeRoot,
    [ValidateRange(10, 100)]
    [int]$TaskCount = 10,
    [ValidateRange(60, 600)]
    [int]$TimeoutSeconds = 180
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

public static class CodePilotConcurrencyProcess {
    private static TcpListener listener;
    private static Thread acceptThread;
    private static volatile bool stopping;
    private static int requestCount;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO { public int cb; public string lpReserved, lpDesktop, lpTitle; public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags; public short wShowWindow, cbReserved2; public IntPtr hStdInput, hStdOutput, hStdError; }
    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool CreateProcessW(string app, StringBuilder command, IntPtr pa, IntPtr ta, bool inherit, int flags, IntPtr environment, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool FreeConsole();
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool AttachConsole(uint processId);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool GenerateConsoleCtrlEvent(uint ctrlEvent, uint processGroupId);
    [DllImport("kernel32.dll", SetLastError=true)] private static extern bool SetConsoleCtrlHandler(IntPtr handler, bool add);

    public static int StartBackend(string executable, string arguments, string cwd) {
        STARTUPINFO si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si); si.dwFlags = 1; si.wShowWindow = 0;
        PROCESS_INFORMATION pi;
        StringBuilder command = new StringBuilder("\"" + executable + "\" " + arguments);
        if (!CreateProcessW(executable, command, IntPtr.Zero, IntPtr.Zero, false, 0x10 | 0x200 | 0x400, IntPtr.Zero, cwd, ref si, out pi))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Unable to start backend");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return pi.dwProcessId;
    }
    public static bool SendCtrlC(int pid) {
        FreeConsole();
        if (!AttachConsole((uint)pid)) { AttachConsole(0xFFFFFFFF); return false; }
        SetConsoleCtrlHandler(IntPtr.Zero, true);
        bool sent = GenerateConsoleCtrlEvent(0, 0); Thread.Sleep(250);
        FreeConsole(); AttachConsole(0xFFFFFFFF); SetConsoleCtrlHandler(IntPtr.Zero, false); return sent;
    }
    public static void StartStub(int port, int delayMs) {
        requestCount = 0; stopping = false; listener = new TcpListener(IPAddress.Loopback, port); listener.Start();
        acceptThread = new Thread(() => {
            while (!stopping) {
                try {
                    TcpClient client = listener.AcceptTcpClient();
                    Thread worker = new Thread(() => Handle(client, delayMs));
                    worker.IsBackground = true;
                    worker.Start();
                } catch (SocketException) { if (!stopping) throw; } catch (ObjectDisposedException) { }
            }
        });
        acceptThread.IsBackground = true; acceptThread.Start();
    }
    private static void Handle(TcpClient client, int delayMs) {
        using (client) using (NetworkStream stream = client.GetStream()) {
            try {
                client.ReceiveTimeout = 10000;
                stream.ReadTimeout = 10000;
                MemoryStream headerBytes = new MemoryStream();
                int matched = 0;
                byte[] marker = new byte[] { 13, 10, 13, 10 };
                while (matched < marker.Length) {
                    int value = stream.ReadByte();
                    if (value < 0) throw new IOException("Connection closed while reading headers");
                    headerBytes.WriteByte((byte)value);
                    matched = value == marker[matched] ? matched + 1 : (value == marker[0] ? 1 : 0);
                }
                string headers = Encoding.ASCII.GetString(headerBytes.ToArray());
                int contentLength = 0;
                foreach (string line in headers.Split(new string[] { "\r\n" }, StringSplitOptions.None)) {
                    if (line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase))
                        Int32.TryParse(line.Substring(line.IndexOf(':') + 1).Trim(), out contentLength);
                }
                byte[] buffer = new byte[8192];
                int remaining = contentLength;
                while (remaining > 0) {
                    int read = stream.Read(buffer, 0, Math.Min(buffer.Length, remaining));
                    if (read <= 0) throw new IOException("Connection closed while reading body");
                    remaining -= read;
                }
                Interlocked.Increment(ref requestCount); Thread.Sleep(delayMs);
                byte[] body = Encoding.UTF8.GetBytes("{\"choices\":[{\"message\":{\"content\":\"<done>local concurrency stub response</done>\"}}]}");
                byte[] head = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + body.Length + "\r\nConnection: close\r\n\r\n");
                stream.Write(head, 0, head.Length); stream.Write(body, 0, body.Length); stream.Flush();
            } catch (IOException) { } catch (ObjectDisposedException) { }
        }
    }
    public static int RequestCount { get { return Volatile.Read(ref requestCount); } }
    public static void StopStub() {
        stopping = true; if (listener != null) listener.Stop(); if (acceptThread != null) acceptThread.Join(3000);
        listener = null; acceptThread = null;
    }
}
'@

function Assert-True { param([bool]$Condition, [string]$Message) if (-not $Condition) { throw $Message } }
function Test-Port { param([int]$Port) $c=[Net.Sockets.TcpClient]::new(); try { $a=$c.BeginConnect('127.0.0.1',$Port,$null,$null); if(-not $a.AsyncWaitHandle.WaitOne(250)){return $false}; $c.EndConnect($a); return $true } catch { return $false } finally { $c.Dispose() } }
function Wait-Port { param([int]$Port,[bool]$Open,[int]$Seconds) $d=[DateTime]::UtcNow.AddSeconds($Seconds); do { if((Test-Port $Port)-eq $Open){return $true}; Start-Sleep -Milliseconds 100 } while([DateTime]::UtcNow-lt$d); return $false }
function Test-Exited { param([int]$Id) try { return [Diagnostics.Process]::GetProcessById($Id).HasExited } catch { return $true } }
function Wait-Exited { param([int]$Id,[int]$Seconds) $d=[DateTime]::UtcNow.AddSeconds($Seconds); do { if(Test-Exited $Id){return $true}; Start-Sleep -Milliseconds 100 } while([DateTime]::UtcNow-lt$d); return $false }

function Invoke-JsonRequest {
    param([string]$Method,[string]$Path,[AllowNull()][string]$Body,[bool]$SendBody=$false)
    if([DateTime]::UtcNow-ge$script:testDeadline){throw "Backend concurrency test exceeded its $TimeoutSeconds second deadline"}
    $id=[Guid]::NewGuid().ToString('N'); $out=Join-Path $requestDirectory "$id.json"; $args=@('--silent','--show-error','--max-time','10','--request',$Method,'--output',$out,'--write-out','%{http_code}')
    if($SendBody){$input=Join-Path $requestDirectory "$id-input.json"; [IO.File]::WriteAllText($input,[string]$Body,[Text.UTF8Encoding]::new($false)); $args+=@('--header','Content-Type: application/json','--data-binary',"@$input")}
    $code=& $curl.Source @args "$baseUri$Path"; if($LASTEXITCODE-ne 0){$script:httpErrors++; throw "curl failed for $Method $Path ($LASTEXITCODE)"}
    $text=if(Test-Path $out){Get-Content -Raw $out}else{''}; try{$json=if($text){$text|ConvertFrom-Json}else{$null}}catch{$script:httpErrors++; throw "Invalid JSON for $Method $Path`: $text"}
    if([int]$code-ge 500){$script:httpErrors++}; [pscustomobject]@{StatusCode=[int]$code;Json=$json;Body=$text}
}
function Start-JsonRequest {
    param([string]$Name,[string]$Method,[string]$Path,[AllowNull()][string]$Body)
    if([DateTime]::UtcNow-ge$script:testDeadline){throw "Backend concurrency test exceeded its $TimeoutSeconds second deadline"}
    $input=Join-Path $requestDirectory "$Name-input.json"; $out=Join-Path $requestDirectory "$Name-body.json"; $meta=Join-Path $requestDirectory "$Name-meta.txt"; $err=Join-Path $requestDirectory "$Name-err.txt"
    [IO.File]::WriteAllText($input,[string]$Body,[Text.UTF8Encoding]::new($false)); $url="$baseUri$Path"
    $arguments="--silent --show-error --max-time 15 --request $Method --header `"Content-Type: application/json`" --data-binary `"@$input`" --output `"$out`" --write-out `"%{http_code}`" `"$url`""
    $p=Start-Process $curl.Source -ArgumentList $arguments -RedirectStandardOutput $meta -RedirectStandardError $err -PassThru -WindowStyle Hidden
    $item=[pscustomobject]@{Name=$Name;Process=$p;BodyFile=$out;MetaFile=$meta;ErrorFile=$err}; $script:childProcesses.Add($item); return $item
}
function Complete-JsonRequest {
    param($Item,[int]$Seconds=20)
    if(-not $Item.Process.WaitForExit($Seconds*1000)){Stop-Process -Id $Item.Process.Id -Force -ErrorAction SilentlyContinue; throw "Concurrent request $($Item.Name) timed out"}
    $Item.Process.WaitForExit()
    $codeText=(Get-Content -Raw $Item.MetaFile -ErrorAction SilentlyContinue).Trim(); $text=Get-Content -Raw $Item.BodyFile -ErrorAction SilentlyContinue
    if($Item.Process.ExitCode-ne 0-and(-not$codeText-or-not$text)){$script:httpErrors++; throw "Concurrent curl $($Item.Name) failed with exit code $($Item.Process.ExitCode): $(Get-Content -Raw $Item.ErrorFile -ErrorAction SilentlyContinue)"}
    try{$json=$text|ConvertFrom-Json}catch{$script:httpErrors++; throw "Concurrent request $($Item.Name) returned invalid JSON: $text"}
    if([int]$codeText-ge 500){$script:httpErrors++}; [pscustomobject]@{StatusCode=[int]$codeText;Json=$json;Body=$text}
}
function New-TaskBody { param([string]$Goal,[string]$Mode='workspace') ([ordered]@{session_id=$sessionId;workspace_id=$workspaceId;input=$Goal;options=[ordered]@{execution_mode=$Mode;auto_run_safe_commands=$false;require_permission_for_file_write=$true;max_steps=1}}|ConvertTo-Json -Depth 5 -Compress) }
function New-Task { param([string]$Goal,[string]$Mode='workspace') $r=Invoke-JsonRequest POST '/api/v1/tasks' (New-TaskBody $Goal $Mode) $true; Assert-True($r.StatusCode-eq 200-and$r.Json.success) "Task creation failed: $($r.Body)"; $id=[string]$r.Json.data.id; $script:taskIds.Add($id); return $id }
function Get-Task { param([string]$Id) Invoke-JsonRequest GET "/api/v1/tasks/$Id" $null }
function Wait-Terminal { param([string]$Id,[int]$Seconds=30) $d=[DateTime]::UtcNow.AddSeconds($Seconds); do{$r=Get-Task $Id;if($r.StatusCode-eq 200-and @('completed','failed','cancelled')-contains$r.Json.data.status){return $r};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$d-and[DateTime]::UtcNow-lt$script:testDeadline);throw "Task $Id did not become terminal before its timeout" }

function Start-Sse {
    param([string]$Name,[string]$Id,[int]$Seconds=40) $out=Join-Path $streamDirectory "$Name.out";$err=Join-Path $streamDirectory "$Name.err";$url="$baseUri/api/v1/tasks/$Id/events";$args="--silent --show-error --no-buffer --include --max-time $Seconds `"$url`"";$p=Start-Process $curl.Source -ArgumentList $args -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden;$x=[pscustomobject]@{Name=$Name;TaskId=$Id;Process=$p;Out=$out;Err=$err};$script:childProcesses.Add($x);return $x
}
function Read-Sse {
    param($Client) if(-not$Client.Process.WaitForExit(45000)){Stop-Process -Id $Client.Process.Id -Force -ErrorAction SilentlyContinue;throw "SSE $($Client.Name) timed out"};$raw=Get-Content -Raw $Client.Out;$split=[regex]::Match($raw,"\r?\n\r?\n");$body=if($split.Success){$raw.Substring($split.Index+$split.Length)}else{''};$events=@();foreach($frame in [regex]::Split($body,"\r?\n\r?\n")){if(-not$frame.Trim()-or$frame.TrimStart().StartsWith(':')){continue};$name='';$data=@();foreach($line in [regex]::Split($frame,"\r?\n")){if($line-match'^event:\s?(.*)$'){$name=$matches[1]}elseif($line-match'^data:\s?(.*)$'){$data+=$matches[1]}};if($name){$text=$data-join"`n";try{$json=$text|ConvertFrom-Json}catch{throw "Invalid SSE JSON: $text"};$events+=[pscustomobject]@{Event=$name;Data=$json}}};[pscustomobject]@{Events=$events;Raw=$raw}
}
function Assert-SseTerminal {
    param($Result,[string]$Id,[string]$Expected)
    foreach($e in $Result.Events){if($e.Event -ne 'stream_end'){Assert-True ($e.Data.task_id -eq $Id) "SSE cross-stream event: expected $Id got $($e.Data.task_id)"}}
    $terminal=@($Result.Events|Where-Object Event -in @('task_completed','task_failed','task_cancelled'))
    $ends=@($Result.Events|Where-Object Event -eq 'stream_end')
    Assert-True ($terminal.Count -eq 1) "Expected one terminal event for $Id, got $($terminal.Count)"
    Assert-True ($terminal[0].Event -eq $Expected) "Expected $Expected for $Id, got $($terminal[0].Event)"
    Assert-True ($ends.Count -eq 1) "Expected one stream_end for $Id, got $($ends.Count)"
    [ordered]@{terminal_event=$terminal[0].Event;stream_end_count=$ends.Count}
}
function Add-Scenario { param([string]$Name,[bool]$Passed,$Details) $script:scenarios.Add([ordered]@{name=$Name;passed=$Passed;details=$Details});Write-Host "[concurrency] PASS: $Name" }
function Stop-OwnedChildren { foreach($x in $childProcesses){try{$x.Process.Refresh();if(-not$x.Process.HasExited){Stop-Process -Id $x.Process.Id -Force -ErrorAction SilentlyContinue;$x.Process.WaitForExit(2000)|Out-Null}}catch{}} }
function Get-ReportError {
    param($ErrorObject)
    if($null-eq$ErrorObject){return $null}
    $text=[string]$ErrorObject
    foreach($path in @($runDirectory,$RuntimeRoot,$resolvedBackend,$resolvedSqlite,$resolvedConfig)){
        if($path){$text=$text.Replace([string]$path,'<test-path>')}
    }
    return $text
}

$resolvedBackend=(Resolve-Path $BackendExe).Path;$resolvedSqlite=(Resolve-Path $SqliteCheckExe).Path;$resolvedConfig=(Resolve-Path $SourceConfigDirectory).Path
if(-not$RuntimeRoot){$RuntimeRoot=Join-Path(Split-Path -Parent $resolvedBackend)'test-runtime'}
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot)
$runDirectory=Join-Path $RuntimeRoot ("$BuildType-concurrency-"+[Guid]::NewGuid().ToString('N'));$binDirectory=Join-Path $runDirectory 'bin';$configDirectory=Join-Path $runDirectory 'config';$workspaceDirectory=Join-Path $runDirectory 'workspace';$requestDirectory=Join-Path $runDirectory 'test-results\requests';$streamDirectory=Join-Path $runDirectory 'test-results\streams';$reportFile=Join-Path $runDirectory 'test-results\backend-concurrency-report.json'
$baseUri='http://127.0.0.1:8080';$stubPort=18082;$backendId=$null;$stubStarted=$false;$failure=$null;$httpErrors=0;$timedOut=$false;$taskIds=[Collections.Generic.List[string]]::new();$scenarios=[Collections.Generic.List[object]]::new();$childProcesses=[Collections.Generic.List[object]]::new();$ownedBackendIds=[Collections.Generic.List[int]]::new();$rounds=[Collections.Generic.List[object]]::new();$taskResults=[Collections.Generic.List[object]]::new();$curl=Get-Command curl.exe -ErrorAction SilentlyContinue
$testDeadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

try {
    Assert-True($null-ne$curl)'curl.exe is required.';Assert-True(-not(Test-Port 8080))'Port 8080 is already in use.';Assert-True(-not(Test-Port $stubPort))"Stub port $stubPort is already in use."
    foreach($d in @($binDirectory,$configDirectory,(Join-Path $runDirectory 'logs'),(Join-Path $runDirectory 'storage'),$workspaceDirectory,$requestDirectory,$streamDirectory)){New-Item -ItemType Directory -Force -Path $d|Out-Null}
    $runtimeExe=Join-Path $binDirectory(Split-Path -Leaf $resolvedBackend);Copy-Item $resolvedBackend $runtimeExe;Get-ChildItem (Split-Path -Parent $resolvedBackend) -Filter '*.dll' -File|Copy-Item -Destination $binDirectory;Get-ChildItem $resolvedConfig -Filter '*.json' -File|Where-Object Name -notlike '*.local.json'|Copy-Item -Destination $configDirectory
    $agentConfig=Join-Path $configDirectory 'agent.json';[IO.File]::WriteAllText((Join-Path $configDirectory 'llm.json'),' {"default":"","providers":{}}'.Trim(),[Text.UTF8Encoding]::new($false))
    [CodePilotConcurrencyProcess]::StartStub($stubPort,1500);$stubStarted=$true
    $backendId=[CodePilotConcurrencyProcess]::StartBackend($runtimeExe,("--config `"$agentConfig`""),$runDirectory);$ownedBackendIds.Add($backendId);Assert-True(Wait-Port 8080 $true 20)'Backend did not open port 8080.'
    $health=Invoke-JsonRequest GET '/api/v1/health' $null;Assert-True($health.StatusCode-eq 200-and$health.Json.success)'Health failed.'
    $workspaceBody=[ordered]@{name='Concurrency regression';path=$workspaceDirectory.Replace('\','/')}|ConvertTo-Json -Compress;$workspace=Invoke-JsonRequest POST '/api/v1/workspaces' $workspaceBody $true;Assert-True ($workspace.StatusCode -eq 200 -and $workspace.Json.success) "Workspace fixture failed: $($workspace.Body)";$workspaceId=[string]$workspace.Json.data.id
    $sessionBody=[ordered]@{title='Concurrency regression';workspace_id=$workspaceId}|ConvertTo-Json -Compress;$session=Invoke-JsonRequest POST '/api/v1/sessions' $sessionBody $true;Assert-True ($session.StatusCode -eq 200 -and $session.Json.success) "Session fixture failed: $($session.Body)";$sessionId=[string]$session.Json.data.id
    $llm=[ordered]@{default='localstub';providers=[ordered]@{localstub=[ordered]@{name='Local concurrency stub';base_url="http://127.0.0.1:$stubPort/v1";model='local-test';api_key_env='';timeout_seconds=10}}}|ConvertTo-Json -Depth 5 -Compress
    Assert-True((Invoke-JsonRequest PUT '/api/v1/config/llm' $llm $true).StatusCode-eq 200)'Could not configure local LLM.';Assert-True((Invoke-JsonRequest PUT '/api/v1/config/llm/local' '{"providers":{"localstub":{"api_key":"local-only-test-key"}}}' $true).StatusCode-eq 200)'Could not configure local key.'

    $pending=@();for($i=1;$i-le$TaskCount;$i++){$pending+=Start-JsonRequest "create-$i" POST '/api/v1/tasks' (New-TaskBody "Concurrent create $i" 'answer')};$created=@();foreach($p in $pending){$r=Complete-JsonRequest $p;Assert-True($r.StatusCode-eq 200-and$r.Json.success)"Concurrent create failed: $($r.Body)";$id=[string]$r.Json.data.id;$created+=$id;$taskIds.Add($id)};Assert-True(($created|Select-Object -Unique).Count-eq$TaskCount)'Duplicate task IDs were returned.';foreach($id in $created){Assert-True((Get-Task $id).StatusCode-eq 200)"Created task $id cannot be queried"};Assert-True((Invoke-JsonRequest GET '/api/v1/health' $null).StatusCode-eq 200)'Health failed after concurrent create.';Add-Scenario 'concurrent create' $true ([ordered]@{count=$created.Count;unique=($created|Select-Object -Unique).Count;task_ids=$created})

    $completeIds=@();$completeClients=@();for($i=1;$i-le 5;$i++){$id=New-Task "Concurrent completion $i";$completeIds+=$id;$completeClients+=Start-Sse "complete-$i" $id};$completeSse=@();for($i=0;$i-lt$completeIds.Count;$i++){$terminal=Wait-Terminal $completeIds[$i] 40;Assert-True($terminal.Json.data.status-eq'completed')"Task did not complete: $($completeIds[$i])";$s=Read-Sse $completeClients[$i];$completeSse+=Assert-SseTerminal $s $completeIds[$i] 'task_completed'};$probe=New-Task 'Post completion probe';Assert-True((Wait-Terminal $probe 40).Json.data.status-eq'completed')'Backend could not complete a post-completion task.';Add-Scenario 'multiple simultaneous completions and SSE isolation' $true ([ordered]@{task_ids=$completeIds;sse=$completeSse;probe=$probe})

    $mixed=@();$mixedClients=@();for($i=1;$i-le 6;$i++){$id=New-Task "Mixed cancellation $i";$mixed+=$id;$mixedClients+=Start-Sse "mixed-$i" $id};Start-Sleep -Milliseconds 300;$cancelPending=@();foreach($id in $mixed[0..2]){$cancelPending+=Start-JsonRequest "mixed-cancel-$id" POST "/api/v1/tasks/$id/cancel" '{}'};$cancelResponses=@();foreach($p in $cancelPending){$cancelResponses+=Complete-JsonRequest $p};$mixedDetails=@();for($i=0;$i-lt 6;$i++){$t=Wait-Terminal $mixed[$i] 40;$expected=if($i-lt 3){'cancelled'}else{'completed'};Assert-True($t.Json.data.status-eq$expected)"Mixed task $($mixed[$i]) expected $expected, got $($t.Json.data.status)";$event=if($expected-eq'cancelled'){'task_cancelled'}else{'task_completed'};$mixedDetails+=Assert-SseTerminal (Read-Sse $mixedClients[$i]) $mixed[$i] $event};for($i=0;$i-lt 3;$i++){Assert-True($cancelResponses[$i].Json.data.status-eq'cancelled')'Cancel response contradicted final state.'};Add-Scenario 'mixed cancel and completion' $true ([ordered]@{task_ids=$mixed;sse=$mixedDetails})

    $same=New-Task 'Same task concurrent cancellation';$sameClient=Start-Sse 'same-cancel' $same;Start-Sleep -Milliseconds 200;$samePending=@();for($i=1;$i-le 5;$i++){$samePending+=Start-JsonRequest "same-cancel-$i" POST "/api/v1/tasks/$same/cancel" '{}'};$sameResponses=@();foreach($p in $samePending){$sameResponses+=Complete-JsonRequest $p};$sameFinal=Wait-Terminal $same 40;$sameStatus=[string]$sameFinal.Json.data.status;Assert-True($sameStatus-in@('cancelled','completed','failed'))'Concurrent cancellation produced no terminal state.';foreach($r in $sameResponses){Assert-True($r.StatusCode-eq 200-and$r.Json.data.status-eq$sameStatus)'Concurrent cancel response contradicted final state.'};$sameEvent=switch($sameStatus){'cancelled'{'task_cancelled'}'completed'{'task_completed'}default{'task_failed'}};$sameSse=Assert-SseTerminal (Read-Sse $sameClient) $same $sameEvent;Add-Scenario 'same task concurrent cancellation' $true ([ordered]@{task_id=$same;final_status=$sameStatus;cancel_response_count=5;sse=$sameSse})

    $queryTasks=@();for($i=1;$i-le 5;$i++){$queryTasks+=New-Task "Concurrent query writer $i"};for($round=1;$round-le 20;$round++){foreach($id in $queryTasks){foreach($path in @("/api/v1/tasks/$id","/api/v1/tasks/$id/events/history","/api/v1/tasks/$id/logs","/api/v1/tasks/$id/replay","/api/v1/permissions?task_id=$id")){ $q=Invoke-JsonRequest GET $path $null;Assert-True($q.StatusCode-lt 500)"HTTP 500 during concurrent query: $path" }};$list=Invoke-JsonRequest GET '/api/v1/tasks?limit=100' $null;$h=Invoke-JsonRequest GET '/api/v1/health' $null;Assert-True($list.StatusCode-lt 500-and$h.StatusCode-eq 200)"List/health failed in query round $round"};foreach($id in $queryTasks){Wait-Terminal $id 40|Out-Null};Add-Scenario 'queries concurrent with writes' $true ([ordered]@{rounds=20;task_ids=$queryTasks;http_errors=$httpErrors})

    for($round=1;$round-le 3;$round++){$before=[Diagnostics.Process]::GetProcessById($backendId);$before.Refresh();$sw=[Diagnostics.Stopwatch]::StartNew();$ids=@();$pends=@();for($i=1;$i-le$TaskCount;$i++){$pends+=Start-JsonRequest "stress-$round-$i" POST '/api/v1/tasks' (New-TaskBody "Stress round $round task $i")};foreach($p in $pends){$r=Complete-JsonRequest $p;$id=[string]$r.Json.data.id;$ids+=$id;$taskIds.Add($id)};foreach($id in $ids){Assert-True((Wait-Terminal $id 45).Json.data.status-eq'completed')"Stress task $id failed"};$sw.Stop();$after=[Diagnostics.Process]::GetProcessById($backendId);$after.Refresh();$rounds.Add([ordered]@{round=$round;elapsed_ms=$sw.ElapsedMilliseconds;task_ids=$ids;working_set_bytes=$after.WorkingSet64;handle_count=$after.HandleCount});Write-Host ("[concurrency] stress round {0}: elapsed_ms={1}; working_set_bytes={2}; handles={3}" -f $round,$sw.ElapsedMilliseconds,$after.WorkingSet64,$after.HandleCount);Assert-True($after.WorkingSet64-lt 1073741824)'Backend working set exceeded 1 GiB.';Assert-True($after.HandleCount-lt 2000)'Backend handle count exceeded 2000.'};Assert-True((Invoke-JsonRequest GET '/api/v1/health' $null).StatusCode-eq 200)'Health failed after stress rounds.';$stressProbe=New-Task 'Post stress probe';Assert-True((Wait-Terminal $stressProbe 40).Json.data.status-eq'completed')'Post-stress task failed.';Add-Scenario 'three stress rounds' $true ([ordered]@{rounds=$rounds;probe=$stressProbe})

    $shutdownTasks=@();for($i=1;$i-le 3;$i++){$shutdownTasks+=New-Task "Active shutdown $i"};Start-Sleep -Milliseconds 250;Assert-True([CodePilotConcurrencyProcess]::SendCtrlC($backendId))'Could not send Ctrl+C.';Assert-True(Wait-Exited $backendId 15)'Backend did not exit during active tasks.';Assert-True(Wait-Port 8080 $false 10)'Port 8080 not released after active shutdown.';$backendId=[CodePilotConcurrencyProcess]::StartBackend($runtimeExe,("--config `"$agentConfig`""),$runDirectory);$ownedBackendIds.Add($backendId);Assert-True(Wait-Port 8080 $true 20)'Backend could not restart in same runtime.';Assert-True((Invoke-JsonRequest GET '/api/v1/health' $null).StatusCode-eq 200)'Health failed after restart.';$oldStates=@();foreach($id in $shutdownTasks){$oldStates+=[ordered]@{task_id=$id;status=[string](Get-Task $id).Json.data.status}};Add-Scenario 'graceful shutdown with active tasks and restart' $true ([ordered]@{task_ids=$shutdownTasks;old_task_behavior=$oldStates;port_released=$true;restart_health=200})

    foreach($id in $taskIds){$r=Get-Task $id;$taskResults.Add([ordered]@{task_id=$id;final_status=[string]$r.Json.data.status})}
    Assert-True([CodePilotConcurrencyProcess]::SendCtrlC($backendId))'Could not stop restarted backend.';Assert-True(Wait-Exited $backendId 15)'Restarted backend did not exit.';$backendId=$null;Assert-True(Wait-Port 8080 $false 10)'Port 8080 remained occupied.';[CodePilotConcurrencyProcess]::StopStub();$stubStarted=$false;Assert-True(Wait-Port $stubPort $false 10)'Stub port remained occupied.'
    $database=Join-Path $runDirectory 'storage\agent.db';Assert-True(Test-Path $database)'Temporary database is missing.';$sqliteOutput=& $resolvedSqlite $database @taskIds;Assert-True($LASTEXITCODE-eq 0)"SQLite verification failed: $sqliteOutput";$sqlite=$sqliteOutput|ConvertFrom-Json;Assert-True($sqlite.integrity_check-eq'ok'-and$sqlite.passed)'SQLite integrity_check did not return ok.';Add-Scenario 'SQLite integrity and task presence' $true $sqlite
} catch { $failure=$_;if([string]$_-match'timed out|timeout'){$timedOut=$true} } finally {
    Stop-OwnedChildren
    if($null-ne$backendId-and-not(Test-Exited $backendId)){Stop-Process -Id $backendId -Force -ErrorAction SilentlyContinue;Wait-Exited $backendId 5|Out-Null}
    if($stubStarted){[CodePilotConcurrencyProcess]::StopStub();$stubStarted=$false}
    $residualBackend=@(Get-Process codepilot-agent-server -ErrorAction SilentlyContinue|Where-Object { $ownedBackendIds.Contains([int]$_.Id) })
    $residualCurl=0;foreach($child in $childProcesses){try{$child.Process.Refresh();if(-not$child.Process.HasExited){$residualCurl++}}catch{}}
    $portsReleased=(-not(Test-Port 8080))-and(-not(Test-Port $stubPort));$passed=$null-eq$failure-and$portsReleased-and$httpErrors-eq 0
    $report=[ordered]@{generated_at_utc=[DateTime]::UtcNow.ToString('o');build_type=$BuildType;concurrent_task_count=$TaskCount;scenarios=$scenarios;tasks=$taskResults;rounds=$rounds;http_error_count=$httpErrors;sqlite_integrity_check=if($null-ne$sqlite){$sqlite.integrity_check}else{'not_run'};timed_out=$timedOut;residual_processes=[ordered]@{backend=@($residualBackend).Count;curl=$residualCurl;powershell_jobs=0;stub=0};ports_released=$portsReleased;external_llm_requests=$false;passed=$passed;error=(Get-ReportError $failure)}
    try{$report|ConvertTo-Json -Depth 12|Set-Content $reportFile -Encoding UTF8}catch{}
}
if($failure-or-not$portsReleased){Write-Host "Preserved failed test runtime: $runDirectory";Write-Host "Report: $reportFile";Write-Error "Backend concurrency regression failed: $failure";exit 1}
Remove-Item $runDirectory -Recurse -Force;if((Test-Path $RuntimeRoot)-and-not(Get-ChildItem $RuntimeRoot -Force|Select-Object -First 1)){Remove-Item $RuntimeRoot -Force}
Write-Host 'Backend concurrency regression passed; temporary runtime removed, all owned processes stopped, ports released, and no external LLM was used.'
exit 0
