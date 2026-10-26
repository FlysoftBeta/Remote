param([int]$WaitPID = 0, [string[]]$Stages = @('detail-initial','hardware','software'))
$taskRoot='C:/src/tmp/t1-encoder-tuning'
$env:TEMP=$taskRoot
$env:TMP=$taskRoot
$self=Get-Process -Id $PID
$self.ProcessorAffinity=255
$self.PriorityClass='BelowNormal'
if ($WaitPID) { Wait-Process -Id $WaitPID -ErrorAction SilentlyContinue }
foreach($stage in $Stages) {
  node tools/encoder-tuning/sweep.mjs $stage
  if($LASTEXITCODE) { throw "Stage $stage failed: $LASTEXITCODE" }
}
node tools/encoder-tuning/analyze.mjs
node tools/encoder-tuning/inspect.mjs
