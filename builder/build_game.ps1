# PLANET HARRIERS - RECOMP : builder
# Generates the recompiled game code from YOUR ROM and compiles it into ph_game.dll.
# Genera el código recompilado del juego a partir de TU ROM y lo compila en ph_game.dll.
#
# Nothing derived from the game is distributed: this runs on your PC with your own files.
# No se distribuye nada derivado del juego: todo se hace en tu PC con tus propios archivos.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$builder = $PSScriptRoot
$root = Split-Path -Parent $builder
$toolsDir = Join-Path $builder 'tools'
$tcName = 'llvm-mingw-20251104-ucrt-x86_64'
$tcUrl = "https://github.com/mstorsjo/llvm-mingw/releases/download/20251104/$tcName.zip"
$tcSha256 = '171b298f46fa4b5653dc7b01cc5e709caca645991be8e1b0724466c4a282a1ac'
$genDir = Join-Path $builder 'gen'
$objDir = Join-Path $builder 'obj'
$workDir = Join-Path $builder 'work'

function Say($es, $en) { Write-Host ""; Write-Host "== $es" -ForegroundColor Cyan; Write-Host "   $en" -ForegroundColor DarkCyan }
function Fail($es, $en) { Write-Host ""; Write-Host "ERROR: $es" -ForegroundColor Red; Write-Host "       $en" -ForegroundColor Red; exit 1 }
function Clean { foreach ($d in $genDir, $objDir, $workDir) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } } }

Say 'PLANET HARRIERS - RECOMP: construcción del código del juego' 'PLANET HARRIERS - RECOMP: building the game code'

# 1. What is there to build?
$arcadeExe = Join-Path $root 'PlanetHarriers-Recomp.exe'
$rom = @('roms\pharrier.zip', 'pharrier.zip') | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $rom) {
	Fail "No hay nada que construir. Copia pharrier.zip (romset de MAME) en la carpeta 'roms'." "Nothing to build. Copy pharrier.zip (MAME romset) to the 'roms' folder."
}
if (-not (Test-Path $arcadeExe)) { Fail "Falta PlanetHarriers-Recomp.exe junto a la carpeta 'builder'." "PlanetHarriers-Recomp.exe is missing next to the 'builder' folder." }

# 2. Compiler (llvm-mingw, free, ~180 MB, only once)
$tcBin = Join-Path $toolsDir "$tcName\bin"
if (-not (Test-Path (Join-Path $tcBin 'x86_64-w64-mingw32-clang.exe'))) {
	Say "Descargando el compilador llvm-mingw (180 MB, sólo la primera vez)..." "Downloading the llvm-mingw compiler (180 MB, first time only)..."
	New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
	$zip = Join-Path $toolsDir "$tcName.zip"
	Invoke-WebRequest -Uri $tcUrl -OutFile $zip -UseBasicParsing
	$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
	if ($hash -ne $tcSha256) { Remove-Item $zip; Fail "El archivo descargado no es correcto (SHA-256)." "Downloaded file is corrupt (SHA-256 mismatch)." }
	Say "Descomprimiendo..." "Extracting..."
	$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
	if ($tar) { & tar.exe -xf $zip -C $toolsDir } else { Expand-Archive -Path $zip -DestinationPath $toolsDir -Force }
	Remove-Item $zip
}
$env:PATH = "$tcBin;$env:PATH"
$jobs = [Math]::Max(1, [int]$env:NUMBER_OF_PROCESSORS)

# 3. The block generator (plain C, no game data)
$blockgen = Join-Path $toolsDir 'blockgen.exe'
& x86_64-w64-mingw32-clang.exe -O2 -static -o $blockgen (Join-Path $builder 'sdk\blockgen.c')
if ($LASTEXITCODE -ne 0) { Fail "No se pudo compilar el generador." "Could not compile the generator." }

function Build-Module($name, $exe, $envName, $images, $seeds, $machine, $out) {
	Clean
	New-Item -ItemType Directory -Force -Path $genDir, $objDir, $workDir | Out-Null
	Say "$name`: arrancando el juego para leer su programa (puede tardar un minuto)..." "$name`: booting the game to read its program (may take a minute)..."
	# relative to the game folder (the executable runs there): avoids problems with non-ASCII paths
	Set-Item "Env:$envName" 'builder\work'
	$p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -Wait
	Remove-Item "Env:$envName"
	$bgArgs = @('-o', $genDir, '-n', '16')
	foreach ($i in $images) {
		$f = Join-Path $workDir $i.File
		if (-not (Test-Path $f)) { Clean; Fail "$name`: no se pudo leer el programa del juego (código $($p.ExitCode))." "$name`: could not read the game program (code $($p.ExitCode))." }
		$bgArgs += '--image'; $bgArgs += "$($i.Cpu):$f`:$($i.Base)"
	}
	$bgArgs += '--seeds'; $bgArgs += (Join-Path $builder $seeds)
	Say "$name`: generando el código C..." "$name`: generating the C code..."
	& $blockgen @bgArgs
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $genDir 'blk_table.c'))) { Clean; Fail "$name`: la generación ha fallado." "$name`: generation failed." }
	Say "$name`: compilando ($jobs núcleos, unos minutos)..." "$name`: compiling ($jobs cores, a few minutes)..."
	Push-Location $builder
	try {
		& mingw32-make.exe -f module.mk "-j$jobs" --no-print-directory "MACHINE=$machine" "OUT=../$out"
		if ($LASTEXITCODE -ne 0) { Clean; Fail "$name`: error de compilación." "$name`: compilation error." }
	} finally { Pop-Location }
	Clean   # the generated sources and the RAM images contain code derived from the game
}

Build-Module 'Planet Harriers' $arcadeExe 'PH_BUILDER_DUMP' @(@{Cpu=0; File='ram_master.bin'; Base='0c000000'}, @{Cpu=1; File='ram_slave.bin'; Base='0c000000'}) 'seeds_hikaru.txt' 'hikaru' 'ph_game.dll'
Say "¡Listo! Ya puedes jugar (PlanetHarriers-Recomp.exe)." "Done! You can play now (PlanetHarriers-Recomp.exe)."
