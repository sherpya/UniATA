	@echo off 
	copy .\Debug\IdeDma.sys C:\WINNT\System32\Drivers\IdeDma.sys 
	copy .\Debug\IdeDma.sys C:\WINNT\System32\Drivers\IdeDmb.sys 
	copy .\Debug\IdeDma.sys .\Debug_Dist\uniata.sys
	copy Dist\* .\Debug_Dist
	echo .\Debug_Dist
	echo Driver copied > copy.msg 
