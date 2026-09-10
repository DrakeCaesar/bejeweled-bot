@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --build "C:\Users\domin\dev\bejeweled-bot\build\dynamic-ninja-msvc-release" --target bejeweled-tests
if errorlevel 1 exit /b 1
"C:\Users\domin\dev\bejeweled-bot\build\dynamic-ninja-msvc-release\bejeweled-tests.exe"
