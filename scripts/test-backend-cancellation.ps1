[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateScript({Test-Path -LiteralPath $_ -PathType Leaf})][string]$BackendExe,
    [ValidateSet('Debug','Release')][string]$BuildType='Debug',
    [string]$SourceConfigDirectory,
    [string]$RuntimeRoot,
    [int]$TimeoutSeconds=240
)

$ErrorActionPreference='Stop'
if(-not $SourceConfigDirectory){$SourceConfigDirectory=Join-Path $PSScriptRoot '..\config'}
if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $PSScriptRoot '..\test-runtime'}
$resolvedBackend=(Resolve-Path $BackendExe).Path
$resolvedConfig=(Resolve-Path $SourceConfigDirectory).Path
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot)
$runDirectory=Join-Path $RuntimeRoot ("$BuildType-cancellation-"+[Guid]::NewGuid().ToString('N'))
$stubPort=18087;$baseUri='http://127.0.0.1:8080';$backend=$null;$failure=$null
$deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$env:TASK7_STUB_KEY='local-cancellation-only'
$results=[Collections.Generic.List[object]]::new()

Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.IO; using System.Net;
using System.Net.Sockets; using System.Text; using System.Threading;
public static class CodePilotCancellationStub {
  static TcpListener listener; static Thread acceptThread; static volatile bool stop;
  static volatile string scenario="success"; static int count,active,interrupted;
  static readonly ManualResetEvent requested=new ManualResetEvent(false);
  static readonly object workersLock=new object(); static readonly List<Thread> workers=new List<Thread>();
  public static int Count { get { return count; } } public static int Active { get { return active; } }
  public static int Interrupted { get { return interrupted; } }
  public static void SetScenario(string value){scenario=value;Interlocked.Exchange(ref count,0);Interlocked.Exchange(ref interrupted,0);requested.Reset();}
  public static bool WaitForRequest(int ms){return requested.WaitOne(ms);}
  public static void Start(int port){stop=false;listener=new TcpListener(IPAddress.Loopback,port);listener.Start();acceptThread=new Thread(()=>{
    while(!stop){try{TcpClient c=listener.AcceptTcpClient();string selected=scenario;int n=Interlocked.Increment(ref count);requested.Set();
      Thread worker=new Thread(()=>Handle(c,selected,n));worker.IsBackground=true;lock(workersLock)workers.Add(worker);worker.Start();
    }catch(SocketException){if(!stop)throw;}catch(ObjectDisposedException){}}
  });acceptThread.IsBackground=true;acceptThread.Start();}
  static void Handle(TcpClient c,string selected,int n){Interlocked.Increment(ref active);try{using(c)using(NetworkStream s=c.GetStream()){
    var r=new StreamReader(s,Encoding.UTF8,false,4096,true);string line;
    while(!String.IsNullOrEmpty(line=r.ReadLine())){}
    int delay=0,status=200;string content;
    if(selected=="long"){delay=30000;content="<done>late response</done>";}
    else if(selected=="cancel500"){delay=400;status=500;content="cancel race failure";}
    else if(selected=="permission"){
      if(n==1)content="<plan><add priority=\"1\">write marker</add></plan>";
      else content="<cmd>file.write {\"path\":\"CANCEL_TOOL_MARKER.txt\",\"content\":\"must not execute\"}</cmd>";
    } else {
      if(n==1)content="<plan><add priority=\"1\">safe step</add></plan>";
      else if(n==2)content="<plan><complete index=\"0\"/></plan><done>executor complete</done>";
      else if(n==3)content="<done>review passed</done>";
      else content="<done>post cancellation healthy</done>";
    }
    int waited=0;while(waited<delay){Thread.Sleep(25);waited+=25;if(c.Client.Poll(0,SelectMode.SelectRead)&&c.Client.Available==0){Interlocked.Increment(ref interrupted);return;}}
    string responseBody=status==200?"{\"choices\":[{\"message\":{\"content\":"+Quote(content)+"}}]}":"{\"error\":\"stub HTTP failure\"}";
    byte[] bb=Encoding.UTF8.GetBytes(responseBody);byte[] hh=Encoding.ASCII.GetBytes("HTTP/1.1 "+status+(status==200?" OK":" Internal Server Error")+"\r\nContent-Type: application/json\r\nContent-Length: "+bb.Length+"\r\nConnection: close\r\n\r\n");
    try{s.Write(hh,0,hh.Length);s.Write(bb,0,bb.Length);s.Flush();}catch(IOException){Interlocked.Increment(ref interrupted);}
  }}finally{Interlocked.Decrement(ref active);}}
  static string Quote(string v){return "\""+v.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r","\\r").Replace("\n","\\n")+"\"";}
  public static bool WaitIdle(int ms){int waited=0;while(waited<ms){if(Active==0)return true;Thread.Sleep(25);waited+=25;}return Active==0;}
  public static void Stop(){stop=true;if(listener!=null)listener.Stop();if(acceptThread!=null)acceptThread.Join(3000);Thread[] copy;lock(workersLock)copy=workers.ToArray();foreach(Thread t in copy)t.Join(3000);listener=null;acceptThread=null;}
}
'@

function Assert-True([bool]$Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Test-Port([int]$Port) {
  $c=[Net.Sockets.TcpClient]::new()
  try {
    $a=$c.BeginConnect('127.0.0.1',$Port,$null,$null)
    if (-not $a.AsyncWaitHandle.WaitOne(200)) { return $false }
    $c.EndConnect($a); return $true
  } catch { return $false } finally { $c.Dispose() }
}
function Wait-Port([int]$Port,[bool]$Open,[int]$Seconds) {
  $end=[DateTime]::UtcNow.AddSeconds($Seconds)
  do { if ((Test-Port $Port) -eq $Open) { return $true }; Start-Sleep -Milliseconds 100 } while ([DateTime]::UtcNow -lt $end)
  return $false
}
function Call([string]$Method,[string]$Path,$Body=$null){
  if ([DateTime]::UtcNow -ge $deadline) { throw 'Cancellation regression exceeded its deadline' }
  $p=@{Uri="$baseUri$Path";Method=$Method;TimeoutSec=10;UseBasicParsing=$true}
  if ($null -ne $Body) { $p.ContentType='application/json';$p.Body=($Body | ConvertTo-Json -Depth 8 -Compress) }
  ((Invoke-WebRequest @p).Content | ConvertFrom-Json)
}
function New-Task([string]$Goal) { (Call POST '/api/v1/tasks' @{session_id=$script:sessionId;workspace_id=$script:workspaceId;input=$Goal;options=@{execution_mode='workspace';auto_run_safe_commands=$false;require_permission_for_file_write=$true;max_steps=5}}).data.id }
function Wait-Terminal([string]$Id,[int]$Seconds=15) {
  $end=[DateTime]::UtcNow.AddSeconds($Seconds)
  do { $t=Call GET "/api/v1/tasks/$Id"; if ($t.data.status -in @('completed','failed','cancelled')) { return $t }; Start-Sleep -Milliseconds 50 } while ([DateTime]::UtcNow -lt $end)
  throw "Task $Id did not terminate"
}
function Wait-Permission([string]$Id,[int]$Seconds=10) {
  $end=[DateTime]::UtcNow.AddSeconds($Seconds)
  do { $p=Call GET "/api/v1/permissions?task_id=$Id"; if (@($p.data.items).Count -gt 0) { return $p.data.items[0] }; Start-Sleep -Milliseconds 50 } while ([DateTime]::UtcNow -lt $end)
  throw "Task $Id did not enter permission wait"
}
function Read-Events([string]$Id,[string]$Name){
  $out=Join-Path $runDirectory "test-results\$Name.sse";$err=Join-Path $runDirectory "test-results\$Name.err"
  $p=Start-Process curl.exe -ArgumentList '--silent','--show-error','--no-buffer','--max-time','12',"$baseUri/api/v1/tasks/$Id/events" -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit(15000)) { Stop-Process $p.Id -Force -ErrorAction SilentlyContinue; throw "SSE $Name timed out" }
  $raw=Get-Content -Raw $out;$events=@();foreach($m in [regex]::Matches($raw,'(?ms)^event: ([^\r\n]+)\r?\ndata: (.+?)\r?\n\r?\n')) { try { $events+=[pscustomobject]@{Event=$m.Groups[1].Value;Data=($m.Groups[2].Value | ConvertFrom-Json)} } catch { throw "Invalid SSE in $Name" } }
  [pscustomobject]@{Raw=$raw;Events=$events}
}
function Assert-Cancelled([string]$Id,[string]$Name,[double]$MaxSeconds=5){
  $sw=[Diagnostics.Stopwatch]::StartNew();$cancel=Call POST "/api/v1/tasks/$Id/cancel" @{};Assert-True ($cancel.data.status -eq 'cancelled') "$Name cancel did not win"
  $sse=Read-Events $Id $Name;$sw.Stop();$terminal=Wait-Terminal $Id
  $terms=@($sse.Events | Where-Object Event -in @('task_completed','task_failed','task_cancelled'));$ends=@($sse.Events | Where-Object Event -eq 'stream_end');$dialogs=@($sse.Events | Where-Object {$_.Event -eq 'agent_message' -and $_.Data.metadata.channel -eq 'dialog'})
  Assert-True ($terminal.data.status -eq 'cancelled') "$Name database status changed";Assert-True ($terms.Count -eq 1 -and $terms[0].Event -eq 'task_cancelled') "$Name terminal event mismatch";Assert-True ($ends.Count -eq 1) "$Name stream_end count $($ends.Count)";Assert-True ($dialogs.Count -eq 0) "$Name emitted final dialog";Assert-True ($sw.Elapsed.TotalSeconds -lt $MaxSeconds) "$Name took $($sw.Elapsed.TotalSeconds)s"
  [pscustomobject]@{Elapsed=$sw.Elapsed.TotalSeconds;Events=$sse.Events}
}

try {
  Assert-True (-not (Test-Port 8080)) 'Port 8080 is in use';Assert-True (-not (Test-Port $stubPort)) "Stub port $stubPort is in use"
  foreach($d in @('bin','config','logs','storage','workspace','test-results')) { New-Item -ItemType Directory -Force (Join-Path $runDirectory $d) | Out-Null }
  $runtimeExe=Join-Path $runDirectory 'bin\codepilot-agent-server.exe';Copy-Item $resolvedBackend $runtimeExe;Get-ChildItem (Split-Path $resolvedBackend) -Filter *.dll | Copy-Item -Destination (Join-Path $runDirectory 'bin')
  Get-ChildItem $resolvedConfig -Filter *.json | Where-Object Name -NotLike '*.local.json' | Copy-Item -Destination (Join-Path $runDirectory 'config')
  @{default='stub';providers=@{stub=@{name='Local cancellation stub';base_url="http://127.0.0.1:$stubPort/v1";model='stub';api_key_env='TASK7_STUB_KEY';timeout_seconds=60}}} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runDirectory 'config\llm.json') -Encoding UTF8
  $experts=Get-Content -Raw (Join-Path $runDirectory 'config\experts.json') | ConvertFrom-Json;foreach($expert in $experts.experts) { $expert | Add-Member -NotePropertyName llm_timeout -NotePropertyValue 60 -Force };$experts | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $runDirectory 'config\experts.json') -Encoding UTF8
  [CodePilotCancellationStub]::Start($stubPort);$backend=Start-Process $runtimeExe -ArgumentList '--config','config/agent.json' -WorkingDirectory $runDirectory -RedirectStandardOutput (Join-Path $runDirectory 'logs\backend.stdout.log') -RedirectStandardError (Join-Path $runDirectory 'logs\backend.stderr.log') -PassThru -WindowStyle Hidden
  Assert-True (Wait-Port 8080 $true 20) 'Backend did not start';Assert-True ((Call GET '/api/v1/health').success) 'Initial health failed'
  $script:sessionId=(Call POST '/api/v1/sessions' @{title='Cancellation regression'}).data.id;$script:workspaceId=(Call POST '/api/v1/workspaces' @{name='Cancellation regression';path='./workspace'}).data.id

  [CodePilotCancellationStub]::SetScenario('long');$id=New-Task 'cancel long LLM request';Assert-True ([CodePilotCancellationStub]::WaitForRequest(5000)) 'Long stub saw no request';$long=Assert-Cancelled $id 'long-request';Assert-True ([CodePilotCancellationStub]::WaitIdle(3000)) 'Long stub connection remained active';Assert-True ([CodePilotCancellationStub]::Count -eq 1) 'Cancelled LLM request retried';Assert-True ([CodePilotCancellationStub]::Interrupted -ge 1) 'Stub did not observe client disconnect';$results.Add([ordered]@{name='long-request';seconds=$long.Elapsed;requests=1;interrupted=$true})

  [CodePilotCancellationStub]::SetScenario('permission');$id=New-Task 'cancel permission wait';$perm=Wait-Permission $id;Assert-True ($perm.status -eq 'pending') 'Permission was not pending';$permission=Assert-Cancelled $id 'permission-wait';Assert-True (-not (Test-Path (Join-Path $runDirectory 'workspace\CANCEL_TOOL_MARKER.txt'))) 'Cancelled permission tool executed';$results.Add([ordered]@{name='permission-wait';seconds=$permission.Elapsed;tool_executed=$false})

  [CodePilotCancellationStub]::SetScenario('cancel500');$id=New-Task 'cancel versus HTTP 500';Assert-True ([CodePilotCancellationStub]::WaitForRequest(5000)) '500 stub saw no request';$race=Assert-Cancelled $id 'cancel-http500-race';Assert-True ([CodePilotCancellationStub]::Count -eq 1) 'Cancel/500 race retried';$results.Add([ordered]@{name='cancel-http500-race';seconds=$race.Elapsed;requests=1})

  [CodePilotCancellationStub]::SetScenario('success');$id=New-Task 'post cancellation completion';$healthy=Wait-Terminal $id 20;Assert-True ($healthy.data.status -eq 'completed') 'Post-cancellation task did not complete';$healthySse=Read-Events $id 'post-cancel-complete';Assert-True (@($healthySse.Events | Where-Object Event -eq 'task_completed').Count -eq 1) 'Post-cancellation completion event missing';Assert-True ((Call GET '/api/v1/health').success) 'Backend unhealthy after cancellation';$results.Add([ordered]@{name='post-cancel-health';status='completed'})

  for($i=1;$i -le 10;$i++) { [CodePilotCancellationStub]::SetScenario('long');$id=New-Task "repeat cancellation $i";Assert-True ([CodePilotCancellationStub]::WaitForRequest(5000)) "Repeat $i stub saw no request";$repeat=Assert-Cancelled $id "repeat-$i";Assert-True ([CodePilotCancellationStub]::WaitIdle(3000)) "Repeat $i left active stub request";Assert-True ([CodePilotCancellationStub]::Count -eq 1) "Repeat $i retried";$results.Add([ordered]@{name="repeat-$i";seconds=$repeat.Elapsed;requests=1}) }
  Assert-True ((Call GET '/api/v1/health').success) 'Final health failed';Assert-True ([CodePilotCancellationStub]::Active -eq 0) 'Stub has active requests'
  $report=[ordered]@{build_type=$BuildType;long_cancel_seconds=$long.Elapsed;permission_cancel_seconds=$permission.Elapsed;cancel_retried=$false;permission_tool_executed=$false;repeat_count=10;external_llm_requests=$false;scenarios=$results;passed=$true};$report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $runDirectory 'test-results\backend-cancellation-report.json') -Encoding UTF8
} catch { $failure=$_ } finally {
  if ($backend) { try { if (-not $backend.HasExited) { Stop-Process $backend.Id -Force -ErrorAction SilentlyContinue;$backend.WaitForExit(5000) | Out-Null } } catch {} }
  [CodePilotCancellationStub]::Stop();Remove-Item Env:TASK7_STUB_KEY -ErrorAction SilentlyContinue
  $portsReleased=(-not (Test-Port 8080)) -and (-not (Test-Port $stubPort))
}
if ($failure -or -not $portsReleased) { Write-Host "Preserved failed runtime: $runDirectory";Write-Error "Backend cancellation regression failed: $failure";exit 1 }
Remove-Item $runDirectory -Recurse -Force;if ((Test-Path $RuntimeRoot) -and -not (Get-ChildItem $RuntimeRoot -Force | Select-Object -First 1)) { Remove-Item $RuntimeRoot -Force }
Write-Host ("Backend cancellation regression passed: long={0:N3}s; permission={1:N3}s; repeats=10; ports released; no external LLM used." -f $long.Elapsed,$permission.Elapsed);exit 0
