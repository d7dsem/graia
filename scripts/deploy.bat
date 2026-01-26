@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."

:: === Defaults ===
set "BUILD_TYPE=Debug"
set "TARGET=test_file_reader"
set "WD_DIR=%PROJECT_ROOT%\wd"

:: Manual compiler setup
:: Require presens in PATH

set "CC=clang"
set "CXX=clang++"

set "CC=gcc"
set "CXX=g++"

:: === Parse CLI arguments ===
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-t" (
    set "TARGET=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-m" (
    set "BUILD_TYPE=%~2"
    shift
    shift
    goto parse_args
)
echo ERROR: Unknown argument: %~1
exit /b 1

:args_done

:: === Validate BUILD_TYPE ===
if /i not "%BUILD_TYPE%"=="Debug" if /i not "%BUILD_TYPE%"=="Release" (
    echo ERROR: BUILD_TYPE must be Debug or Release, got: %BUILD_TYPE%
    exit /b 1
)

:: If you use a specific generator, set it here
:: Generator for mingw/msys2 environment
::set "GEN=-G "Unix Makefiles""
:: Require env coretly seted up
set "GEN=-G Ninja"

set "BUILD_DIR=%PROJECT_ROOT%\build_%CC%_%BUILD_TYPE%"

:: === Ensure build directory ===
if not exist "%BUILD_DIR%\" mkdir "%BUILD_DIR%"
if errorlevel 1 exit /b 1

:: === Configure ===
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" %GEN% ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_C_COMPILER="%CC%" ^
    -DCMAKE_CXX_COMPILER="%CXX%"
if errorlevel 1 exit /b 1

:: === Build target ===
cmake --build "%BUILD_DIR%" --target "%TARGET%"
if errorlevel 1 exit /b 1

:: === Ensure wd directory ===
if not exist "%WD_DIR%\" mkdir "%WD_DIR%"
if errorlevel 1 exit /b 1

:: === Copy built artifact ===
set "SRC_EXE=%BUILD_DIR%\bin\%TARGET%.exe"
if not exist "%SRC_EXE%" (
    echo ERROR: Built file not found: "%SRC_EXE%"
    exit /b 2
)

echo Copying "%SRC_EXE%" to "%WD_DIR%\"
copy /Y "%SRC_EXE%" "%WD_DIR%\"
if errorlevel 1 exit /b 1

echo OK: "%TARGET%.exe" deployed to "%WD_DIR%\"
exit /b 0