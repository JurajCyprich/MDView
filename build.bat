@echo off
rem Zostavenie MDView.exe (MSVC Build Tools). Vysledok je jedno standalone exe bez zavislosti.
rem NAVOD.md sa vklada do exe ako resource (mdview.rc).
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
rc /nologo /fo mdview.res mdview.rc
cl /nologo /O2 /MT /W3 /utf-8 mdview.c mdview.res /Fe:MDView.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest ^
  user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib
if exist mdview.obj del mdview.obj
if exist mdview.res del mdview.res
