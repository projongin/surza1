# Microsoft Developer Studio Project File - Name="Surza" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=Surza - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "Surza.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "Surza.mak" CFG="Surza - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Surza - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "Surza - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 1
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Surza - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT  /W3 /GX /YX /FD /c  /O2 /Gy /D "NDEBUG"  /D "WIN32" /I "$(RTTarget)\Include"
# ADD      CPP /nologo /MT  /W3 /GX /YX /FD /c  /O2 /Gy /D "NDEBUG"  /D "WIN32" /I "$(RTTarget)\Include"
# ADD BASE LINK32 rtk32s.lib drvrt32.lib rtt32.lib /incremental:no /opt:ref /opt:icf  /nodefaultlib:"kernel32.lib" /fixed:no /nologo /map /include:"_malloc" /include:"_EnterCriticalSection@4" /include:"_RTFileSystemList" /libpath:"$(RTTarget)\Libmsvc"
# ADD      LINK32 rtk32s.lib drvrt32.lib rtt32.lib /incremental:no /opt:ref /opt:icf  /nodefaultlib:"kernel32.lib" /fixed:no /nologo /map /include:"_malloc" /include:"_EnterCriticalSection@4" /include:"_RTFileSystemList" /libpath:"$(RTTarget)\Libmsvc"

!ELSEIF  "$(CFG)" == "Surza - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /GX /YX /FD /c  /Od /Gm /ZI          /D "WIN32" /I "$(RTTarget)\Include"
# ADD      CPP /nologo /MTd /W3 /GX /YX /FD /c  /Od /Gm /ZI          /D "WIN32" /I "$(RTTarget)\Include"
# ADD BASE LINK32 rtk32.lib drvrt32.lib rtt32.lib /incremental:yes /debug            /nodefaultlib:"kernel32.lib" /fixed:no /nologo /map /include:"_malloc" /include:"_EnterCriticalSection@4" /include:"_RTFileSystemList" /libpath:"$(RTTarget)\Libmsvc"
# ADD      LINK32 rtk32.lib drvrt32.lib rtt32.lib /incremental:yes /debug            /nodefaultlib:"kernel32.lib" /fixed:no /nologo /map /include:"_malloc" /include:"_EnterCriticalSection@4" /include:"_RTFileSystemList" /libpath:"$(RTTarget)\Libmsvc"

!ENDIF 

# Begin Target

# Name "Surza - Win32 Release"
# Name "Surza - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\Surza.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# Begin Group "MAP Files Debug"

# PROP Default_Filter "map"
# Begin Source File

SOURCE=.\Debug\Surza.map
# End Source File
# End Group
# Begin Group "MAP Files Release"

# PROP Default_Filter "map"
# Begin Source File

SOURCE=.\Release\Surza.map
# End Source File
# End Group
# End Target
# End Project
