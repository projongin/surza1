# Microsoft Developer Studio Project File - Name="Target" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Generic Project" 0x010a

CFG=Target - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "Target.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "Target.mak" CFG="Target - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Target - Win32 Release" (based on "Win32 (x86) Generic Project")
!MESSAGE "Target - Win32 Debug" (based on "Win32 (x86) Generic Project")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
MTL=midl.exe

!IF  "$(CFG)" == "Target - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""

!ELSEIF  "$(CFG)" == "Target - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""

!ENDIF 

# Begin Target

# Name "Target - Win32 Release"
# Name "Target - Win32 Debug"
# Begin Group "Config Files"

# PROP Default_Filter "cfg"
# Begin Source File

SOURCE=.\Demopc.cfg
# End Source File
# Begin Source File

SOURCE=.\Monitor.cfg

!IF  "$(CFG)" == "Target - Win32 Release"

!ELSEIF  "$(CFG)" == "Target - Win32 Debug"

USERDEP__MONIT="Demopc.cfg"	
# Begin Custom Build - Locate Monitor
OutDir=.\Debug
InputPath=.\Monitor.cfg

"$(OutDir)\Monitor.rtb" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	"$(RTTarget)\Bin\RTLoc" -DBOOT "$(OutDir)\Monitor" Demopc.cfg Monitor.cfg

# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Surza.cfg

!IF  "$(CFG)" == "Target - Win32 Release"

USERDEP__THREA="Demopc.cfg"	"$(OutDir)\Surza.exe"	
# Begin Custom Build - Locate Surza
OutDir=.\Release
InputPath=.\Surza.cfg

"$(OutDir)\Surza.rtb" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	"$(RTTarget)\Bin\RTLoc" -DBOOT "$(OutDir)\Surza" Demopc.cfg Surza.cfg

# End Custom Build

!ELSEIF  "$(CFG)" == "Target - Win32 Debug"

USERDEP__THREA="Demopc.cfg"	"$(OutDir)\Monitor.rtb"	"$(OutDir)\Surza.exe"	
# Begin Custom Build - Locate Surza
OutDir=.\Debug
InputPath=.\Surza.cfg

"$(OutDir)\Surza.rtb" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	"$(RTTarget)\Bin\RTLoc" "$(OutDir)\Surza" Demopc.cfg Surza.cfg

# End Custom Build

!ENDIF 

# End Source File
# End Group
# Begin Group "Boot Diskettes"

# PROP Default_Filter "rtb"
# Begin Source File

SOURCE=.\Debug\Monitor.rtb

!IF  "$(CFG)" == "Target - Win32 Release"

!ELSEIF  "$(CFG)" == "Target - Win32 Debug"

# Begin Custom Build - Creating Monitor Boot Diskette
OutDir=.\Debug
InputPath=.\Debug\Monitor.rtb

"$(OutDir)\BootDisk.txt" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	"$(RTTarget)\Bin\BootDisk" "$(InputPath)" A: 
	if not errorlevel 1 echo New boot disk > "$(OutDir)\BootDisk.txt" 
	
# End Custom Build

!ENDIF 

# End Source File
# Begin Source File

SOURCE=.\Release\Surza.rtb

!IF  "$(CFG)" == "Target - Win32 Release"

# Begin Custom Build - Creating Surza Boot Diskette
OutDir=.\Release
InputPath=.\Release\Surza.rtb

"$(OutDir)\BootDisk.txt" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)"
	"$(RTTarget)\Bin\BootDisk" "$(InputPath)" A: 
	if not errorlevel 1 echo New boot disk > "$(OutDir)\BootDisk.txt" 
	
# End Custom Build

!ELSEIF  "$(CFG)" == "Target - Win32 Debug"

!ENDIF 

# End Source File
# End Group
# Begin Group "LOC Files Debug"

# PROP Default_Filter "loc"
# Begin Source File

SOURCE=.\Debug\Monitor.loc
# End Source File
# Begin Source File

SOURCE=.\Debug\Surza.loc
# End Source File
# End Group
# Begin Group "LOC Files Release"

# PROP Default_Filter "loc"
# Begin Source File

SOURCE=.\Release\Surza.loc
# End Source File
# End Group
# End Target
# End Project
