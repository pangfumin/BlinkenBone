
rem http://stackoverflow.com/questions/328017/path-to-msbuild
rem reg.exe query "HKLM\SOFTWARE\Microsoft\MSBuild\ToolsVersions\4.0" /v MSBuildToolsPath
rem set MSBUILD=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\msbuild.exe
for /f "skip=2 tokens=2,*" %%A in ('reg.exe query "HKLM\SOFTWARE\Microsoft\MSBuild\ToolsVersions\4.0" /v MSBuildToolsPath') do SET MSBUILDDIR=%%B
set MSBUILD=%MSBUILDDIR%\msbuild.exe
%MSBUILD% /version

pushd 07.1_blinkenlight_server\msvc
%MSBUILD%  blinkenlightd.vcxproj
popd

pushd 07.2_blinkenlight_test\msvc
%MSBUILD%  blinkenlight_test.vcxproj
popd


pushd "02.3_simh.4.x.jh\src\Visual Studio Projects"
%MSBUILD% pdp8_realcons.vcxproj
%MSBUILD% pdp10_realcons.vcxproj
%MSBUILD% pdp11_realcons.vcxproj
popd
