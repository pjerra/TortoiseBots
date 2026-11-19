# Starts the dashboard for a ForeverCore realm (Windows). Every setting can be overridden by the environment variable of the same name.
#   TBDASH_DATA_DIR = the folder the module writes its data tables into (the realm's LogsDir)
#   TBDASH_ART_DIR  = the folder scripts/gen_forever_art.py wrote the client's maps and icons into
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin = Join-Path $here 'tortoise-observability.exe'
if (-not (Test-Path $bin)) { Write-Error "missing executable $bin" }
function Get-Setting($name, $default) { $v = [Environment]::GetEnvironmentVariable($name); if ([string]::IsNullOrEmpty($v)) { $default } else { $v } }
$dashArgs = @(
  '-core', (Get-Setting 'TBDASH_CORE' 'forever'),
  '-db-host', (Get-Setting 'DB_HOST' '127.0.0.1'),
  '-db-port', (Get-Setting 'DB_PORT' '3306'),
  '-db-user', (Get-Setting 'DB_USER' 'trinity'),
  '-db-pass', (Get-Setting 'DB_PASSWORD' 'trinity'),
  '-db-name', (Get-Setting 'DB_LOGIN' 'auth'),
  '-db-char', (Get-Setting 'DB_CHAR' 'characters'),
  '-db-world', (Get-Setting 'DB_WORLD' 'world'),
  '-http-host', (Get-Setting 'HTTP_HOST' '127.0.0.1'),
  '-http-port', (Get-Setting 'HTTP_PORT' '8095'),
  '-udp-host', (Get-Setting 'UDP_HOST' '127.0.0.1'),
  '-udp-port', (Get-Setting 'UDP_PORT' '9195'),
  '-min-gm-level', (Get-Setting 'MIN_GM_LEVEL' '2')
)
& $bin @dashArgs
