
.386 

EXTRN  _EnumSystemLocalesW@8:near 
PUBLIC __imp__EnumSystemLocalesW@8 
PUBLIC _EnumSystemLocalesW 

_TEXT SEGMENT DWORD USE32 PUBLIC 'CODE' 

ASSUME CS:_TEXT, DS:_TEXT 

__imp__EnumSystemLocalesW@8 DD offset _EnumSystemLocalesW@8 
_EnumSystemLocalesW: JMP _EnumSystemLocalesW@8 

_TEXT ENDS 

END

