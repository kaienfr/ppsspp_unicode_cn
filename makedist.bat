@echo off
mkdir dist
copy /Y PPSSPPWindows.exe dist\
copy /Y PPSSPPWindows64.exe dist\
xcopy /E /Y flash0 dist\flash0\
xcopy /E /Y lang dist\lang\
xcopy /E /Y assets dist\assets\
echo �����氲װ�ɹ�����distĿ¼
pause