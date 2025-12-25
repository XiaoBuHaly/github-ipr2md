@echo off
setlocal enabledelayedexpansion

REM Root of repo (directory containing this script)
set "ROOT_DIR=%~dp0"
REM Strip trailing backslash
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

set "BUILD_DIR=%ROOT_DIR%\build"
if not defined RUN_BUILD_TYPE set "RUN_BUILD_TYPE=Release"

REM Script-only flags (not forwarded to the executable)
set "RUN_CLEAN=0"
set "RUN_RECONFIGURE=0"
set "RUN_FORCE_BUILD=0"
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
if /i "%~1"=="--run-reconfigure" goto flag_run_reconfigure
if /i "%~1"=="--run-force-build" goto flag_run_force_build
if /i "%~1"=="--run-no-build" goto flag_run_no_build
if /i "%~1"=="--run-werror" goto flag_run_werror
if /i "%~1"=="--run-build-dir" goto flag_run_build_dir
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

REM Clean build directory if requested
if "%RUN_CLEAN%"=="1" (
  if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
  )
)

REM Run and forward remaining args
set "EXE=%BUILD_DIR%\github-ipr2md.exe"
if not exist "%EXE%" (
  REM Some generators may not add .exe in the same place; try without extension
  if exist "%BUILD_DIR%\github-ipr2md" (
    set "EXE=%BUILD_DIR%\github-ipr2md"
  )
)

REM Decide whether we need to (re)build.
set "NEED_BUILD=0"
if "%RUN_NO_BUILD%"=="1" (
  set "NEED_BUILD=0"
) else if "%RUN_RECONFIGURE%"=="1" (
  set "NEED_BUILD=1"
) else if "%RUN_FORCE_BUILD%"=="1" (
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

if "%RUN_RECONFIGURE%"=="1" (
  if exist "%BUILD_DIR%\CMakeCache.txt" (
    del /f /q "%BUILD_DIR%\CMakeCache.txt"
  )
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

if not exist "%EXE%" (
  echo Error: executable not found under "%BUILD_DIR%".
  exit /b 1
)

"%EXE%" %FORWARD_ARGS%
exit /b %ERRORLEVEL%


