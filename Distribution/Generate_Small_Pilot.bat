@echo off
setlocal

set "GENERATOR_DIR=%~dp0"
set "GENERATOR_EXE=%GENERATOR_DIR%he_grenade_game.exe"
set "GENERATOR_CONFIG=%GENERATOR_DIR%generator-config.json"

if not exist "%GENERATOR_EXE%" (
  echo Generator executable not found:
  echo %GENERATOR_EXE%
  pause
  exit /b 1
)

"%GENERATOR_EXE%" -GenerateDataset -GeneratorConfig="%GENERATOR_CONFIG%" -BuildRevision=37cb24d-local-generator-preflight -RenderOffscreen -unattended -nosound -NoSplash -NoVSync -log
set "GENERATOR_EXIT=%ERRORLEVEL%"

if not "%GENERATOR_EXIT%"=="0" (
  echo.
  echo Generation failed with exit code %GENERATOR_EXIT%.
  pause
  exit /b %GENERATOR_EXIT%
)

echo.
echo Generation completed. See GeneratedData\small-pilot.
pause
