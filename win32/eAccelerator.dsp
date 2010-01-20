# Microsoft Developer Studio Project File - Name="eAccelerator" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=eAccelerator - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "eAccelerator.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "eAccelerator.mak" CFG="eAccelerator - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "eAccelerator - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "eAccelerator - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "eAccelerator - Win32 Release NTS" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "eAccelerator - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D ZTS=1 /D ZEND_DEBUG=1 /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_ENCODER" /D "WITH_EACCELERATOR_LOADER" /D "WITH_EACCELERATOR_SESSIONS" /D "WITH_EACCELERATOR_CONTENT_CACHING" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /I "../../" /D "_DEBUG" /D ZEND_DEBUG=1 /D "DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D ZTS=1 /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_SESSIONS" /D "WITH_EACCELERATOR_CONTENT_CACHING" /D "WITH_EACCELERATOR_SHM" /D "WITH_EACCELERATOR_INFO" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 php5ts_debug.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /libpath:"..\..\.."
# ADD LINK32 php5ts_debug.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /libpath:"..\..\.."

!ELSEIF  "$(CFG)" == "eAccelerator - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /D "NDEBUG" /D ZEND_DEBUG=0 /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D ZTS=1 /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_ENCODER" /D "WITH_EACCELERATOR_LOADER" /D "WITH_EACCELERATOR_SESSIONS" /D "WITH_EACCELERATOR_CONTENT_CACHING" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /I "../../" /D "NDEBUG" /D ZEND_DEBUG=0 /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D ZTS=1 /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_INFO" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 php5ts.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /libpath:"..\..\.."
# ADD LINK32 php5ts.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"Release/eAccelerator_ts.dll" /libpath:"..\..\.."

!ELSEIF  "$(CFG)" == "eAccelerator - Win32 Release NTS"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "eAccelerator___Win32_Release_NTS"
# PROP BASE Intermediate_Dir "eAccelerator___Win32_Release_NTS"
# PROP BASE Ignore_Export_Lib 0
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "eAccelerator___Win32_Release_NTS"
# PROP Intermediate_Dir "eAccelerator___Win32_Release_NTS"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MD /W3 /GX /O2 /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /I "../../" /D "NDEBUG" /D ZEND_DEBUG=0 /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D ZTS=1 /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_INFO" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "../../.." /I "../../../zend" /I "../../../TSRM" /I "../../../main" /I "../../" /D "NDEBUG" /D ZEND_DEBUG=0 /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "HAVE_EACCELERATOR" /D "COMPILE_DL_EACCELERATOR" /D "ZEND_WIN32" /D "PHP_WIN32" /D HAVE_EXT_SESSION_PHP_SESSION_H=1 /D "WITH_EACCELERATOR_CRASH_DETECTION" /D "WITH_EACCELERATOR_OPTIMIZER" /D "WITH_EACCELERATOR_INFO" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 php5ts.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"Release/eAccelerator_ts.dll" /libpath:"..\..\.."
# ADD LINK32 php5.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386 /out:"Release/eAccelerator.dll" /libpath:"..\..\.."

!ENDIF 

# Begin Target

# Name "eAccelerator - Win32 Debug"
# Name "eAccelerator - Win32 Release"
# Name "eAccelerator - Win32 Release NTS"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\debug.c
# End Source File
# Begin Source File

SOURCE=..\ea_dasm.c
# End Source File
# Begin Source File

SOURCE=..\ea_info.c
# End Source File
# Begin Source File

SOURCE=..\ea_restore.c
# End Source File
# Begin Source File

SOURCE=..\ea_store.c
# End Source File
# Begin Source File

SOURCE=..\eaccelerator.c
# End Source File
# Begin Source File

SOURCE=..\fnmatch.c
# End Source File
# Begin Source File

SOURCE=..\mm.c
# End Source File
# Begin Source File

SOURCE=..\opcodes.c
# End Source File
# Begin Source File

SOURCE=..\optimize.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\debug.h
# End Source File
# Begin Source File

SOURCE=..\ea_dasm.h
# End Source File
# Begin Source File

SOURCE=..\ea_info.h
# End Source File
# Begin Source File

SOURCE=..\ea_restore.h
# End Source File
# Begin Source File

SOURCE=..\ea_store.h
# End Source File
# Begin Source File

SOURCE=..\eaccelerator.h
# End Source File
# Begin Source File

SOURCE=..\eaccelerator_version.h
# End Source File
# Begin Source File

SOURCE=..\fnmatch.h
# End Source File
# Begin Source File

SOURCE=..\mm.h
# End Source File
# Begin Source File

SOURCE=..\opcodes.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
