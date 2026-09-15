cd .
RD /S /Q ..\IPCast.bak
xcopy *.mdb "..\IPCast.bak" /S /Y /I
xcopy *.ini "..\IPCast.bak" /S /Y /I 
xcopy *.bat "..\IPCast.bak" /S /Y /I 

SET RARCLINE="C:\Program Files\WinRAR\Winrar.exe"
SET RARSOURCE="..\IPCast.bak\*.*"
SET RARDEST="IPπ„≤•≈‰÷√±∏∑›.rar"
%RARCLINE% a -m5 -ep1 -r -k -ag.yyyymmdd.hh.mm %RARDEST% %RARSOURCE%
