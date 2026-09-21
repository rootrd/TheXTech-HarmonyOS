@echo off
set "JAVA_HOME=C:\Program Files\Java\jdk-25.0.4.1+1"
set "PATH=%JAVA_HOME%\bin;%PATH%"
cd /d "E:\TheXTechOH\hap"
call "C:\Program Files\HuaWei\DevEco Studio\tools\hvigor\bin\hvigorw.bat" --mode module -p product=default assembleHap --no-daemon