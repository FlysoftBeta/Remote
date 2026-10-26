param([int]$WaitPID=0)
$env:TEMP='C:/src/tmp/t1-encoder-tuning'
$env:TMP=$env:TEMP
$env:TUNING_EXE=if(Test-Path 'C:/src/tmp/t1-encoder-tuning/build/tune-next.exe'){'C:/src/tmp/t1-encoder-tuning/build/tune-next.exe'}else{'C:/src/tmp/t1-encoder-tuning/build/tune.exe'}
$self=Get-Process -Id $PID
$self.ProcessorAffinity=255
$self.PriorityClass='BelowNormal'
if($WaitPID){Wait-Process -Id $WaitPID -ErrorAction SilentlyContinue}
node tools/encoder-tuning/followup.mjs software-skip
if($LASTEXITCODE){throw 'Software skip stage failed'}
node tools/encoder-tuning/repeat.mjs hardware
if($LASTEXITCODE){throw 'Hardware repeats failed'}
node tools/encoder-tuning/repeat.mjs software
if($LASTEXITCODE){throw 'Software repeats failed'}
node tools/encoder-tuning/repeat.mjs extra
if($LASTEXITCODE){throw 'Extra controls failed'}
node tools/encoder-tuning/analyze.mjs
node tools/encoder-tuning/inspect.mjs
node tools/encoder-tuning/audit.mjs
node tools/encoder-tuning/aggregate.mjs
