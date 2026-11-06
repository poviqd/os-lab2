@echo off
setlocal
cd /d "%~dp0"
 
git pull --ff-only || exit /b 1
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug || exit /b 1
cmake --build build || exit /b 1
build\proctest.exe
