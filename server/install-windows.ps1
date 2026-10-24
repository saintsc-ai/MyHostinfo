# Windows 에 설치: 로그인하면 창 없이(pythonw) 늘 돌며, NodeMCU 화면 보드가 꽂히면 이 PC 상태를 보낸다.
#   powershell -ExecutionPolicy Bypass -File hosts\hostinfo\install-windows.ps1
# 지우기: Unregister-ScheduledTask -TaskName hostinfo-serial -Confirm:$false
$ErrorActionPreference = "Stop"
$bin = Join-Path $env:USERPROFILE "bin"
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item (Join-Path $PSScriptRoot "hostinfo_serial.py") (Join-Path $bin "hostinfo_serial.py") -Force

$py = (Get-Command python -ErrorAction Stop).Source
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
