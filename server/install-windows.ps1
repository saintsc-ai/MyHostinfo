# Windows 에 설치: 로그인하면 창 없이(pythonw) 늘 돌며, NodeMCU 화면 보드가 꽂히면 이 PC 상태를 보낸다.
#   powershell -ExecutionPolicy Bypass -File hosts\hostinfo\install-windows.ps1
# 지우기: Unregister-ScheduledTask -TaskName hostinfo-serial -Confirm:$false
$ErrorActionPreference = "Stop"
$bin = Join-Path $env:USERPROFILE "bin"
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item (Join-Path $PSScriptRoot "hostinfo_serial.py") (Join-Path $bin "hostinfo_serial.py") -Force

# 파이썬 확인: 없거나, Microsoft Store 안내용 가짜 python(WindowsApps)이거나, 3.7 미만이면 멈춘다.
$cmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $cmd) { throw "python 을 찾을 수 없습니다. python.org 에서 파이썬 3 을 설치하고 'Add python.exe to PATH' 를 체크하세요." }
$py = $cmd.Source
$ver = & $py -c "import sys; print('%d.%d' % sys.version_info[:2]); sys.exit(0 if sys.version_info >= (3, 7) else 1)" 2>$null
if ($LASTEXITCODE -ne 0 -or -not $ver) {
  if ($py -like "*WindowsApps*") { throw "python 이 Microsoft Store 안내용입니다($py). python.org 에서 파이썬 3 을 설치하세요." }
  throw "Python 3.7 이상이 필요합니다($py, 버전 '$ver')."
}
& $py (Join-Path $PSScriptRoot "hostinfo_serial.py") --dry --once --no-usb --no-lists | Out-Null
if ($LASTEXITCODE -ne 0) { throw "hostinfo_serial.py 시험 실행이 실패했습니다." }
"python $ver ($py)"
$base = (& $py -c "import sys; print(sys.base_prefix)").Trim()   # 가상환경이 잡혀도 시스템 파이썬으로
$pyw = Join-Path $base "pythonw.exe"
if (-not (Test-Path $pyw)) { $pyw = $py }
$log = Join-Path $env:LOCALAPPDATA "hostinfo-serial.log"
# pythonw 로 바로(창 없음). 출력은 --log 파일에(1MB 넘으면 비움)
$action = New-ScheduledTaskAction -Execute $pyw -Argument "`"$bin\hostinfo_serial.py`" --broadcast --log `"$log`""
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) -Hidden
Register-ScheduledTask -TaskName "hostinfo-serial" -Action $action -Trigger $trigger -Settings $settings `
  -Description "화면 보드에 이 PC 상태 보내기 (MyHostinfo)" -Force | Out-Null
Start-ScheduledTask -TaskName "hostinfo-serial"
Start-Sleep 5
Get-ScheduledTask -TaskName "hostinfo-serial" | Select-Object TaskName, State
"기록: $log"
