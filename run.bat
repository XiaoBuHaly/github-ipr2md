@echo off
setlocal enabledelayedexpansion

REM Root of repo (directory containing this script)
set "ROOT_DIR=%~dp0"
REM Strip trailing backslash
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_DIR=%ROOT_DIR%\build"
REM If repo is accessed via UNC path (e.g. \\wsl.localhost\...), some tools (MSBuild/CMD)
REM may fail when the build directory is also UNC. Prefer a local TEMP build dir by default.
if "%ROOT_DIR:~0,2%"=="\\\\" (
  if defined TEMP (
    set "BUILD_DIR=%TEMP%\github-ipr2md-build"
  )
)
if not defined RUN_BUILD_TYPE set "RUN_BUILD_TYPE=Release"

REM Script-only flags (not forwarded to the executable)
set "RUN_CLEAN=0"
set "RUN_BUILD=0"
set "RUN_NO_BUILD=0"
set "RUN_WERROR=0"
set "RUN_BUILD_DIR="
set "FORWARD_ARGS="
set "AFTER_DD=0"

:parse_args
if "%~1"=="" goto args_done

if "%AFTER_DD%"=="1" goto forward_arg

if "%~1"=="--" goto parse_double_dash
if /i "%~1"=="--run-clean" goto flag_run_clean
if /i "%~1"=="--run-build" goto flag_run_build
if /i "%~1"=="--run-build-type" goto flag_run_build_type
if /i "%~1"=="--run-no-build" goto flag_run_no_build
if /i "%~1"=="--run-werror" goto flag_run_werror
if /i "%~1"=="--run-build-dir" goto flag_run_build_dir
if /i "%~1"=="--run-help" goto flag_run_help
if /i "%~1"=="-h" goto flag_run_help
if /i "%~1"=="--run-force-build" goto flag_removed_force_build
if /i "%~1"=="--run-reconfigure" goto flag_removed_reconfigure
goto forward_arg

:parse_double_dash
set "AFTER_DD=1"
shift
goto parse_args

:flag_run_clean
set "RUN_CLEAN=1"
shift
goto parse_args

:flag_run_reconfigure
set "RUN_RECONFIGURE=1"
shift
goto parse_args

:flag_run_force_build
set "RUN_FORCE_BUILD=1"
shift
goto parse_args

:flag_run_no_build
set "RUN_NO_BUILD=1"
shift
goto parse_args

:flag_run_werror
set "RUN_WERROR=1"
shift
goto parse_args

:flag_run_build_dir
if "%~2"=="" (
  echo Error: --run-build-dir requires a directory argument.
  exit /b 1
)
set "RUN_BUILD_DIR=%~2"
shift
shift
goto parse_args

:forward_arg
REM Unknown arg -> forward to executable
if defined FORWARD_ARGS goto forward_arg_append
set FORWARD_ARGS="%~1"
shift
goto parse_args

:forward_arg_append
set FORWARD_ARGS=%FORWARD_ARGS% "%~1"
shift
goto parse_args

:args_done

REM Apply overrides
if defined RUN_BUILD_DIR set "BUILD_DIR=%RUN_BUILD_DIR%"

REM Flag conflict checks
if "%RUN_NO_BUILD%"=="1" (
  if "%RUN_CLEAN%"=="1" (
    echo Error: --run-no-build cannot be used with --run-clean. 1>&2
    exit /b 2
  )
  if "%RUN_BUILD%"=="1" (
    echo Error: --run-no-build cannot be used with --run-build. 1>&2
    exit /b 2
  )
)

REM Clean build directory if requested
if "%RUN_CLEAN%"=="1" (
  if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
  )
)

REM Resolve executable path (supports multi-config generators like Visual Studio)
set "EXE="
call :resolve_exe "%BUILD_DIR%" "%RUN_BUILD_TYPE%"

REM Decide whether we need to (re)build.
set "NEED_BUILD=0"
if "%RUN_NO_BUILD%"=="1" (
  set "NEED_BUILD=0"
) else if "%RUN_CLEAN%"=="1" (
  set "NEED_BUILD=1"
) else if "%RUN_BUILD%"=="1" (
  set "NEED_BUILD=1"
) else if exist "%EXE%" (
  set "NEED_BUILD=0"
) else (
  set "NEED_BUILD=1"
)

REM If already built, run directly (unless reconfigure/force-build/no-build logic says otherwise).
if "%NEED_BUILD%"=="0" (
  if exist "%EXE%" (
    "%EXE%" %FORWARD_ARGS%
    exit /b %ERRORLEVEL%
  )
)

REM Configure + build (first run or forced)
if "%RUN_NO_BUILD%"=="1" (
  echo Error: --run-no-build was specified but executable not found under "%BUILD_DIR%".
  exit /b 1
)

set "CMAKE_WERROR_ARG="
if "%RUN_WERROR%"=="1" set "CMAKE_WERROR_ARG=-DENABLE_WERROR=ON"
if defined CI set "CMAKE_WERROR_ARG=-DENABLE_WERROR=ON"
if defined GITHUB_ACTIONS set "CMAKE_WERROR_ARG=-DENABLE_WERROR=ON"

cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%RUN_BUILD_TYPE% %CMAKE_WERROR_ARG%
if errorlevel 1 exit /b 1
if not "%RUN_NO_BUILD%"=="1" (
  cmake --build "%BUILD_DIR%" --config %RUN_BUILD_TYPE%
  if errorlevel 1 exit /b 1
)

REM Re-resolve executable after build (VS outputs into %BUILD_DIR%\%RUN_BUILD_TYPE%\)
set "EXE="
call :resolve_exe "%BUILD_DIR%" "%RUN_BUILD_TYPE%"

if not exist "%EXE%" (
  echo Error: executable not found under "%BUILD_DIR%".
  exit /b 1
)

"%EXE%" %FORWARD_ARGS%
exit /b %ERRORLEVEL%

:resolve_exe
REM Args: 1) build dir 2) build type (Release/Debug/...)
set "EXE=%~1\github-ipr2md.exe"
if exist "%EXE%" exit /b 0
set "EXE=%~1\%~2\github-ipr2md.exe"
if exist "%EXE%" exit /b 0
REM Some generators or toolchains may omit .exe.
set "EXE=%~1\github-ipr2md"
if exist "%EXE%" exit /b 0
set "EXE=%~1\%~2\github-ipr2md"
if exist "%EXE%" exit /b 0
REM Some projects place binaries under a bin/ folder.
set "EXE=%~1\bin\github-ipr2md.exe"
if exist "%EXE%" exit /b 0
set "EXE=%~1\bin\%~2\github-ipr2md.exe"
if exist "%EXE%" exit /b 0
REM Fall back to default location (caller will check existence)
set "EXE=%~1\github-ipr2md.exe"
exit /b 0

::flag_run_build
set "RUN_BUILD=1"
shift
goto parse_args

::flag_run_build_type
if "%~2"=="" (
  echo Error: --run-build-type requires a build type. ^(Debug/Release/RelWithDebInfo/MinSizeRel^) 1>&2
  exit /b 1
)
set "RUN_BUILD_TYPE=%~2"
shift
shift
goto parse_args

::flag_run_help
echo Usage:
echo   run.bat [--run-* wrapper flags] [--] [github-ipr2md args...]
echo.
echo Wrapper flags ^(not forwarded to the executable^):
echo   --run-build-dir ^<dir^>      Build directory ^(default: .\build^)
echo   --run-build-type ^<type^>    Debug/Release/RelWithDebInfo/MinSizeRel ^(default: Release^)
echo   --run-build                Force configure+build ^(no directory deletion^)
echo   --run-clean                Clean rebuild: delete build dir then configure+build
echo   --run-no-build             Run only, do not build ^(exe must exist^)
echo   --run-werror               Configure with -DENABLE_WERROR=ON ^(also enabled in CI^)
echo   --run-help, -h             Show this help
echo   --                         Stop parsing wrapper flags; forward remaining args
exit /b 0

::flag_removed_force_build
echo Error: --run-force-build has been removed. Use --run-build instead. 1>&2
exit /b 2

::flag_removed_reconfigure
echo Error: --run-reconfigure has been removed. Use --run-build (configure+build) or --run-clean (clean rebuild). 1>&2
exit /b 2


