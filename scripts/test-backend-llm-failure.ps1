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
$runDirectory=Join-Path $RuntimeRoot ("$BuildType-llm-failure-"+[Guid]::NewGuid().ToString('N'))
$stubPort=18086;$baseUri='http://127.0.0.1:8080';$backend=$null;$failure=$null
$curlProcesses=[Collections.Generic.List[object]]::new();$results=[Collections.Generic.List[object]]::new()
$deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$env:TASK6_STUB_KEY='local-regression-only'

Add-Type -TypeDefinition @'
using System; using System.IO; using System.Net; using System.Net.Sockets;
using System.Text; using System.Threading;
public static class CodePilotLlmFailureStub {
  static TcpListener listener; static Thread thread; static volatile bool stop;
  static volatile string scenario="success"; static int count; static readonly ManualResetEvent requested=new ManualResetEvent(false);
  public static int Count { get { return count; } }
  public static void SetScenario(string value){scenario=value;Interlocked.Exchange(ref count,0);requested.Reset();}
  public static bool WaitForRequest(int ms){return requested.WaitOne(ms);}
  public static void Start(int port){stop=false;listener=new TcpListener(IPAddress.Loopback,port);listener.Start();thread=new Thread(()=>{
    while(!stop){try{using(var c=listener.AcceptTcpClient())using(var s=c.GetStream()){
      var r=new StreamReader(s,Encoding.UTF8,false,4096,true);string line;while(!String.IsNullOrEmpty(line=r.ReadLine())){}
      int n=Interlocked.Increment(ref count);requested.Set();int status=200;int delay=0;string content;
      if(scenario=="direct_stream"){delay=500;content="";}
      else if(scenario=="ordinary500"){status=500;content="ordinary failure";}
      else if(scenario=="dangerous"){content="<cmd>file.write {\"path\":\"DANGEROUS_MARKER.txt\",\"content\":\"must not run\"}</cmd>";}
      else if(scenario=="cancel500"){status=500;delay=1500;content="cancel failure";}
      else if(n==1){content="<plan><add priority=\"1\">safe test step</add></plan>";}
      else if(n==2){content="<plan><complete index=\"0\"/></plan><done>executor complete</done>";}
      else if(n==3){content="<done>review passed</done>";}
      else if(scenario=="summary_invalid"){content="SUMMARY_INVALID_MARKER readable summary";}
      else if(scenario=="summary500"){status=500;content="summary failure";}
      else if(scenario=="summary_empty"){content="";}
      else if(scenario=="summary_timeout"){delay=3500;content="<done>late summary</done>";}
      else {content="<done>SUMMARY_SUCCESS_MARKER</done>";}
      if(delay>0)Thread.Sleep(delay);
      string body=scenario=="direct_stream"?"data: {\"choices\":[{\"delta\":{\"content\":\"DIRECT_STREAM_\"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"MARKER\"}}]}\n\ndata: [DONE]\n\n":status==200?"{\"choices\":[{\"message\":{\"content\":"+Quote(content)+"}}]}":"{\"error\":\"stub HTTP failure\"}";
      string contentType=scenario=="direct_stream"?"text/event-stream":"application/json";byte[] bb=Encoding.UTF8.GetBytes(body);byte[] hh=Encoding.ASCII.GetBytes("HTTP/1.1 "+status+(status==200?" OK":" Internal Server Error")+"\r\nContent-Type: "+contentType+"\r\nContent-Length: "+bb.Length+"\r\nConnection: close\r\n\r\n");
      try{s.Write(hh,0,hh.Length);s.Write(bb,0,bb.Length);s.Flush();Thread.Sleep(30);}catch(IOException){}
    }}catch(SocketException){if(!stop)throw;}catch(ObjectDisposedException){}}});thread.IsBackground=true;thread.Start();}
  static string Quote(string v){return "\""+v.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r","\\r").Replace("\n","\\n")+"\"";}
  public static void Stop(){stop=true;if(listener!=null)listener.Stop();if(thread!=null)thread.Join(5000);listener=null;thread=null;}
}
'@

function Assert-True([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Test-Port([int]$Port){$c=[Net.Sockets.TcpClient]::new();try{$a=$c.BeginConnect('127.0.0.1',$Port,$null,$null);if(-not $a.AsyncWaitHandle.WaitOne(200)){return $false};$c.EndConnect($a);return $true}catch{return $false}finally{$c.Dispose()}}
function Wait-Port([int]$Port,[bool]$Open,[int]$Seconds){$end=[DateTime]::UtcNow.AddSeconds($Seconds);do{if((Test-Port $Port)-eq$Open){return $true};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);return $false}
function Call([string]$Method,[string]$Path,$Body=$null){
  if([DateTime]::UtcNow-ge$deadline){throw 'LLM failure regression exceeded its deadline'}
  $p=@{Uri="$baseUri$Path";Method=$Method;TimeoutSec=10;UseBasicParsing=$true}
  if($null-ne$Body){$p.ContentType='application/json';$p.Body=($Body|ConvertTo-Json -Depth 8 -Compress)}
  ((Invoke-WebRequest @p).Content|ConvertFrom-Json)
}
function New-Task([string]$Goal,[string]$Mode='workspace'){(Call POST '/api/v1/tasks' @{session_id=$script:sessionId;workspace_id=$script:workspaceId;input=$Goal;options=@{execution_mode=$Mode;auto_run_safe_commands=$false;require_permission_for_file_write=$true;max_steps=5}}).data.id}
function Wait-Terminal([string]$Id,[int]$Seconds=20){$end=[DateTime]::UtcNow.AddSeconds($Seconds);do{$t=Call GET "/api/v1/tasks/$Id";if($t.data.status -in @('completed','failed','cancelled')){return $t};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);throw "Task $Id did not terminate"}
function Read-Events([string]$Id,[string]$Name){
  $out=Join-Path $runDirectory "test-results\$Name.sse";$err=Join-Path $runDirectory "test-results\$Name.err"
  $p=Start-Process curl.exe -ArgumentList '--silent','--show-error','--no-buffer','--max-time','15',"$baseUri/api/v1/tasks/$Id/events" -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden
  $curlProcesses.Add($p);if(-not $p.WaitForExit(20000)){Stop-Process $p.Id -Force -ErrorAction SilentlyContinue;throw "SSE $Name timed out"}
  $raw=Get-Content -Raw $out;$events=@();foreach($m in [regex]::Matches($raw,'(?ms)^event: ([^\r\n]+)\r?\ndata: (.+?)\r?\n\r?\n')){try{$events+=[pscustomobject]@{Event=$m.Groups[1].Value;Data=($m.Groups[2].Value|ConvertFrom-Json)}}catch{throw "Invalid SSE in $Name"}}
  [pscustomobject]@{Raw=$raw;Events=$events}
}
function Verify([string]$Name,[string]$Id,[string]$Expected,[int]$ExpectedRequests,[string]$Marker=''){
  $terminal=Wait-Terminal $Id 25;$sse=Read-Events $Id $Name;$terms=@($sse.Events|Where-Object Event -in @('task_completed','task_failed','task_cancelled'));$ends=@($sse.Events|Where-Object Event -eq 'stream_end');$messages=@($sse.Events|Where-Object Event -eq 'agent_message');$dialogs=@($messages|Where-Object{$_.Data.metadata.channel -eq 'dialog'})
  Assert-True($terminal.data.status-eq$Expected) "$Name status was $($terminal.data.status)"
  Assert-True($terms.Count-eq1) "$Name terminal count $($terms.Count)";Assert-True($ends.Count-eq1) "$Name stream_end count $($ends.Count)"
  Assert-True($sse.Events[-1].Event -eq 'stream_end') "$Name emitted a business event after stream_end"
  Assert-True([CodePilotLlmFailureStub]::Count-eq$ExpectedRequests) "$Name request count $([CodePilotLlmFailureStub]::Count), expected $ExpectedRequests"
  if($Marker){Assert-True(@($dialogs|Where-Object{$_.Data.content-like"*$Marker*"}).Count-eq1) "$Name final marker dialog was not unique"}
  if($Name -eq 'summary-success'){
    $starts=@($messages|Where-Object{$_.Data.metadata.stage -eq 'expert_start'});$done=@($messages|Where-Object{$_.Data.metadata.stage -eq 'expert_done'});$waiting=@($messages|Where-Object{$_.Data.metadata.source -eq 'llm' -and $_.Data.metadata.stage -eq 'waiting'});$received=@($messages|Where-Object{$_.Data.metadata.source -eq 'llm' -and $_.Data.metadata.stage -eq 'received'});$raw=@($messages|Where-Object{$_.Data.metadata.source -eq 'llm_raw'})
    foreach($expert in @('planner','executor','summarizer')){Assert-True(@($starts|Where-Object{$_.Data.metadata.expert -eq $expert}).Count-ge1) "Missing expert_start for $expert";Assert-True(@($done|Where-Object{$_.Data.metadata.expert -eq $expert}).Count-ge1) "Missing expert_done for $expert"}
    Assert-True(@($done|Where-Object{$_.Data.metadata.expert -eq 'planner' -and $_.Data.metadata.next -eq 'executor'}).Count-eq1)'Missing planner to executor route event';Assert-True(@($done|Where-Object{$_.Data.metadata.expert -eq 'executor' -and $_.Data.metadata.next -eq 'summarizer'}).Count-eq1)'Missing executor to summarizer route event';Assert-True(@($done|Where-Object{$_.Data.metadata.expert -eq 'summarizer' -and $_.Data.metadata.next -eq '_done'}).Count-eq1)'Missing summarizer exit route event'
    Assert-True($waiting.Count-ge3)'Missing LLM waiting process events';Assert-True($received.Count-ge3)'Missing LLM received process events';Assert-True($raw.Count-ge3)'Missing LLM raw debug events'
  }
  $item=[ordered]@{name=$Name;status=$terminal.data.status;llm_requests=[CodePilotLlmFailureStub]::Count;agent_message_count=$messages.Count;dialog_count=$dialogs.Count;terminal_count=$terms.Count;stream_end_count=$ends.Count};$results.Add($item);Write-Host("[llm-failure] PASS {0}: status={1}; requests={2}; agent_message={3}; dialog={4}"-f$Name,$item.status,$item.llm_requests,$item.agent_message_count,$item.dialog_count);$item
}

try{
  Assert-True(-not(Test-Port 8080))'Port 8080 is in use';Assert-True(-not(Test-Port $stubPort))"Stub port $stubPort is in use"
  foreach($d in @('bin','config','logs','storage','workspace','test-results')){New-Item -ItemType Directory -Force (Join-Path $runDirectory $d)|Out-Null}
  $runtimeExe=Join-Path $runDirectory 'bin\codepilot-agent-server.exe';Copy-Item $resolvedBackend $runtimeExe;Get-ChildItem (Split-Path $resolvedBackend) -Filter *.dll|Copy-Item -Destination (Join-Path $runDirectory 'bin')
  Get-ChildItem $resolvedConfig -Filter *.json|Where-Object Name -NotLike '*.local.json'|Copy-Item -Destination (Join-Path $runDirectory 'config')
  $llm=@{default='stub';providers=@{stub=@{name='Local failure stub';base_url="http://127.0.0.1:$stubPort/v1";model='stub';api_key_env='TASK6_STUB_KEY';timeout_seconds=1}}};$llm|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDirectory 'config\llm.json') -Encoding UTF8
  $experts=Get-Content -Raw (Join-Path $runDirectory 'config\experts.json')|ConvertFrom-Json;foreach($expert in $experts.experts){$expert|Add-Member -NotePropertyName llm_timeout -NotePropertyValue 1 -Force};$experts|ConvertTo-Json -Depth 20|Set-Content (Join-Path $runDirectory 'config\experts.json') -Encoding UTF8
  [CodePilotLlmFailureStub]::Start($stubPort);$backend=Start-Process $runtimeExe -ArgumentList '--config','config/agent.json' -WorkingDirectory $runDirectory -RedirectStandardOutput (Join-Path $runDirectory 'logs\backend.stdout.log') -RedirectStandardError (Join-Path $runDirectory 'logs\backend.stderr.log') -PassThru -WindowStyle Hidden
  Assert-True(Wait-Port 8080 $true 20)'Backend did not start';Assert-True((Call GET '/api/v1/health').success)'Health failed'
  $script:sessionId=(Call POST '/api/v1/sessions' @{title='LLM failure regression'}).data.id;$script:workspaceId=(Call POST '/api/v1/workspaces' @{name='LLM failure regression';path='./workspace'}).data.id
  $graphResponse=Call GET '/api/v1/experts/graph';$positionsResponse=Call GET '/api/v1/experts/graph/positions';Assert-True($graphResponse.success -and $null-ne$graphResponse.data.nodes -and $null-ne$graphResponse.data.edges -and $null-ne$graphResponse.data.virtual_nodes)'Expert graph response structure changed';Assert-True($positionsResponse.success -and $positionsResponse.PSObject.Properties.Name -contains 'data')'Expert graph positions response structure changed';$graph=$graphResponse.data;$nodeIds=@($graph.nodes|ForEach-Object id);Assert-True(@($nodeIds|Where-Object{$_ -eq 'planner'}).Count-eq1)'Expert graph lost planner node';Assert-True(@($nodeIds|Where-Object{$_ -eq 'executor'}).Count-eq1)'Expert graph lost executor node';Assert-True(@($nodeIds|Where-Object{$_ -eq 'summarizer'}).Count-eq1)'Expert graph lost summarizer node';Assert-True(@($graph.edges|Where-Object{$_.source -eq 'planner' -and $_.target -eq 'executor'}).Count-ge1)'Expert graph lost planner route';Assert-True(@($graph.edges|Where-Object{$_.source -eq 'executor' -and $_.target -eq 'summarizer'}).Count-ge1)'Expert graph lost executor route';Assert-True(@($graph.edges|Where-Object{$_.source -eq 'summarizer' -and $_.target -eq '_done'}).Count-ge1)'Expert graph lost summarizer exit route';$results.Add([ordered]@{name='expert-graph-contract';status='passed';node_count=@($graph.nodes).Count;edge_count=@($graph.edges).Count;positions_response='valid';terminal_count=0;stream_end_count=0});Write-Host '[llm-failure] PASS expert-graph-contract'

  [CodePilotLlmFailureStub]::SetScenario('direct_stream');$directId=New-Task 'direct answer streaming contract' 'answer';$directSse=Read-Events $directId 'direct-answer-stream';$directTerminal=Wait-Terminal $directId 20;$directChunks=@($directSse.Events|Where-Object Event -eq 'agent_message_chunk');$directDialogs=@($directSse.Events|Where-Object{$_.Event -eq 'agent_message' -and $_.Data.metadata.channel -eq 'dialog'});$directTerms=@($directSse.Events|Where-Object Event -in @('task_completed','task_failed','task_cancelled'));$directEnds=@($directSse.Events|Where-Object Event -eq 'stream_end');Assert-True($directTerminal.data.status -eq 'completed')'Direct Answer did not complete';Assert-True($directChunks.Count-eq2)'Direct Answer chunks were not preserved';Assert-True($directDialogs.Count-eq1 -and $directDialogs[0].Data.content -eq 'DIRECT_STREAM_MARKER')'Direct Answer final stream message was missing or duplicated';Assert-True($directTerms.Count-eq1 -and $directTerms[0].Event -eq 'task_completed' -and $directEnds.Count-eq1 -and $directSse.Events[-1].Event -eq 'stream_end')'Direct Answer terminal stream contract failed';Assert-True([CodePilotLlmFailureStub]::Count-eq1)'Direct Answer made an unexpected LLM call count';$results.Add([ordered]@{name='direct-answer-stream';status='completed';llm_requests=1;agent_message_chunk_count=$directChunks.Count;dialog_count=$directDialogs.Count;terminal_count=$directTerms.Count;stream_end_count=$directEnds.Count});Write-Host '[llm-failure] PASS direct-answer-stream'

  [CodePilotLlmFailureStub]::SetScenario('success');Verify 'summary-success' (New-Task 'summary success') 'completed' 4 'SUMMARY_SUCCESS_MARKER'|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('summary_invalid');Verify 'summary-invalid-readable' (New-Task 'summary invalid readable') 'failed' 4 'SUMMARY_INVALID_MARKER'|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('summary500');Verify 'summary-http-500' (New-Task 'summary http 500') 'failed' 5|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('summary_empty');Verify 'summary-empty' (New-Task 'summary empty') 'failed' 5|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('summary_timeout');Verify 'summary-timeout' (New-Task 'summary timeout') 'failed' 5|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('ordinary500');Verify 'ordinary-http-500' (New-Task 'ordinary http 500') 'failed' 2|Out-Null
  [CodePilotLlmFailureStub]::SetScenario('dangerous');$danger=New-Task 'ordinary invalid dangerous tool';Verify 'ordinary-invalid-control' $danger 'failed' 3|Out-Null;Assert-True(-not(Test-Path (Join-Path $runDirectory 'workspace\DANGEROUS_MARKER.txt')))'Invalid response executed a tool'

  [CodePilotLlmFailureStub]::SetScenario('cancel500');$cancelId=New-Task 'cancel during llm failure';Assert-True([CodePilotLlmFailureStub]::WaitForRequest(5000))'Cancel stub saw no request';$cancel=Call POST "/api/v1/tasks/$cancelId/cancel" @{};Assert-True($cancel.data.status-eq'cancelled')'Cancel request did not win';Verify 'cancel-during-failure' $cancelId 'cancelled' 1|Out-Null

  for($i=1;$i-le10;$i++){[CodePilotLlmFailureStub]::SetScenario('summary_invalid');Verify "summary-invalid-repeat-$i" (New-Task "summary invalid repeat $i") 'failed' 4 'SUMMARY_INVALID_MARKER'|Out-Null}
  Assert-True((Call GET '/api/v1/health').success)'Backend unhealthy after scenarios'
  [CodePilotLlmFailureStub]::Stop();$refused=New-Task 'connection refused';$refusedTerminal=Wait-Terminal $refused 20;$refusedSse=Read-Events $refused 'connection-refused';$terms=@($refusedSse.Events|Where-Object Event -in @('task_completed','task_failed','task_cancelled'));$ends=@($refusedSse.Events|Where-Object Event -eq 'stream_end');$msgs=@($refusedSse.Events|Where-Object Event -eq 'agent_message');Assert-True($refusedTerminal.data.status -eq 'failed' -and $terms.Count -eq 1 -and $ends.Count -eq 1)'Connection refusal terminal contract failed';Assert-True($refusedSse.Raw -notmatch 'local-regression-only')'API key leaked';$results.Add([ordered]@{name='connection-refused';status='failed';llm_requests=2;agent_message_count=$msgs.Count;dialog_count=@($msgs|Where-Object{$_.Data.metadata.channel -eq 'dialog'}).Count;terminal_count=1;stream_end_count=1});Write-Host '[llm-failure] PASS connection-refused'
  Assert-True((Call GET '/api/v1/health').success)'Backend unhealthy after connection refusal'
  $report=[ordered]@{build_type=$BuildType;scenarios=$results;external_llm_requests=$false;ports_released=$true;passed=$true};$report|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDirectory 'test-results\backend-llm-failure-report.json') -Encoding UTF8
}catch{$failure=$_}finally{
  foreach($p in $curlProcesses){try{if(-not $p.HasExited){Stop-Process $p.Id -Force -ErrorAction SilentlyContinue}}catch{}}
  if($backend){try{if(-not $backend.HasExited){Stop-Process $backend.Id -Force -ErrorAction SilentlyContinue;$backend.WaitForExit(5000)|Out-Null}}catch{}}
  [CodePilotLlmFailureStub]::Stop();Remove-Item Env:TASK6_STUB_KEY -ErrorAction SilentlyContinue
  $portsReleased=(-not(Test-Port 8080))-and(-not(Test-Port $stubPort))
}
if($failure -or -not $portsReleased){Write-Host "Preserved failed runtime: $runDirectory";Write-Error "Backend LLM failure regression failed: $failure";exit 1}
Remove-Item $runDirectory -Recurse -Force;if((Test-Path $RuntimeRoot)-and-not(Get-ChildItem $RuntimeRoot -Force|Select-Object -First 1)){Remove-Item $RuntimeRoot -Force}
Write-Host("Backend LLM failure regression passed: {0} scenarios; ports released; no external LLM used."-f$results.Count);exit 0
