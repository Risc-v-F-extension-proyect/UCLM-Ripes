@echo off
setlocal EnableExtensions
cd /d "C:\dev\UCLM-Ripes-backport"

set "REG_PATH=HKCU\Software\Ripes\Ripes"
set "actual="

for /f "tokens=3" %%A in ('reg query "%REG_PATH%" /v processor_id 2^>nul') do (
    set "actual=%%A"
)

call :nombre_actual "%actual%"

echo ==========================================
echo       Seleccionar procesador Ripes
echo ==========================================
echo.
echo Configuracion actual: %actual_nombre% [%actual%]
echo.
echo  0 - RV32 uniciclo
echo  1 - RV32 1 slot, salto no tomado
echo  2 - RV32 1 slot, delayed branch
echo  3 - RV32 2 slots, salto no tomado ^(5 etapas normal^)
echo  4 - RV32 2 slots, delayed branch
echo  5 - RV32 3 slots, salto no tomado
echo  6 - RV32 3 slots, delayed branch
echo  7 - RV64 uniciclo
echo  8 - RV64 1 slot, salto no tomado
echo  9 - RV64 1 slot, delayed branch
echo 10 - RV64 2 slots, salto no tomado ^(5 etapas normal^)
echo 11 - RV64 2 slots, delayed branch
echo 12 - RV64 3 slots, salto no tomado
echo 13 - RV64 3 slots, delayed branch
echo.

tasklist /FI "IMAGENAME eq Ripes.exe" 2^>nul | %SystemRoot%\System32\find.exe /I "Ripes.exe" ^>nul
if not errorlevel 1 (
    echo AVISO: Ripes.exe esta abierto.
    echo Cierralo antes de aplicar el cambio.
    echo.
)

set /p "opcion=Selecciona una opcion [0]: "
if "%opcion%"=="" set "opcion=0"

echo(%opcion%| findstr /R "^[0-9][0-9]*$" >nul
if errorlevel 1 goto :opcion_invalida
set /a opcion_num=%opcion% 2>nul
if %opcion_num% LSS 0 goto :opcion_invalida
if %opcion_num% GTR 13 goto :opcion_invalida

if "%opcion%"=="0"  (set "processor_id=0"  & set "nombre=RV32 uniciclo")
if "%opcion%"=="1"  (set "processor_id=7"  & set "nombre=RV32 1 slot, salto no tomado")
if "%opcion%"=="2"  (set "processor_id=8"  & set "nombre=RV32 1 slot, delayed branch")
if "%opcion%"=="3"  (set "processor_id=6"  & set "nombre=RV32 2 slots, salto no tomado")
if "%opcion%"=="4"  (set "processor_id=9"  & set "nombre=RV32 2 slots, delayed branch")
if "%opcion%"=="5"  (set "processor_id=10" & set "nombre=RV32 3 slots, salto no tomado")
if "%opcion%"=="6"  (set "processor_id=11" & set "nombre=RV32 3 slots, delayed branch")
if "%opcion%"=="7"  (set "processor_id=13" & set "nombre=RV64 uniciclo")
if "%opcion%"=="8"  (set "processor_id=20" & set "nombre=RV64 1 slot, salto no tomado")
if "%opcion%"=="9"  (set "processor_id=21" & set "nombre=RV64 1 slot, delayed branch")
if "%opcion%"=="10" (set "processor_id=19" & set "nombre=RV64 2 slots, salto no tomado")
if "%opcion%"=="11" (set "processor_id=22" & set "nombre=RV64 2 slots, delayed branch")
if "%opcion%"=="12" (set "processor_id=23" & set "nombre=RV64 3 slots, salto no tomado")
if "%opcion%"=="13" (set "processor_id=24" & set "nombre=RV64 3 slots, delayed branch")

reg add "%REG_PATH%" /v processor_id /t REG_DWORD /d %processor_id% /f >nul
if errorlevel 1 goto :error

reg add "%REG_PATH%" /v processor_layout_id /t REG_DWORD /d 0 /f >nul
if errorlevel 1 goto :error

echo.
echo Nueva configuracion: %nombre% [ID %processor_id%]
echo Reinicia Ripes para aplicar el cambio.
echo.
pause
exit /b 0

:nombre_actual
set "actual_nombre=No configurado"
if /I "%~1"=="0x0"  set "actual_nombre=RV32 uniciclo"
if /I "%~1"=="0x6"  set "actual_nombre=RV32 2 slots, salto no tomado"
if /I "%~1"=="0x7"  set "actual_nombre=RV32 1 slot, salto no tomado"
if /I "%~1"=="0x8"  set "actual_nombre=RV32 1 slot, delayed branch"
if /I "%~1"=="0x9"  set "actual_nombre=RV32 2 slots, delayed branch"
if /I "%~1"=="0xa"  set "actual_nombre=RV32 3 slots, salto no tomado"
if /I "%~1"=="0xb"  set "actual_nombre=RV32 3 slots, delayed branch"
if /I "%~1"=="0xd"  set "actual_nombre=RV64 uniciclo"
if /I "%~1"=="0x13" set "actual_nombre=RV64 2 slots, salto no tomado"
if /I "%~1"=="0x14" set "actual_nombre=RV64 1 slot, salto no tomado"
if /I "%~1"=="0x15" set "actual_nombre=RV64 1 slot, delayed branch"
if /I "%~1"=="0x16" set "actual_nombre=RV64 2 slots, delayed branch"
if /I "%~1"=="0x17" set "actual_nombre=RV64 3 slots, salto no tomado"
if /I "%~1"=="0x18" set "actual_nombre=RV64 3 slots, delayed branch"
exit /b 0

:opcion_invalida
echo.
echo ERROR: Opcion no valida. Debe estar entre 0 y 13.
echo.
pause
exit /b 1

:error
echo.
echo ERROR: No se pudo actualizar la configuracion de Ripes.
echo.
pause
exit /b 1
