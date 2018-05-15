# quake2outlines
quake 2 engine mod that adds outlines to geometry

XTREME Q2 (Max?)

Its been eons since I made this(holy shit) and I dont remember anything about what I did I dont even remember the full name of this, only that i called it xtreme something or other. 

Outline code was taken from QMB(engine mod for q1) and added into quake2max. 

TO PLAY:
Extract files into q2 directory. I already included a copy of quake2max with the updated exe and dlls from own mod. No idea how current it(quake2max version I included) is.

my added console commands:
r_bloom 0 or 1
r_outline 0 or 1

also from q2max the command for outlines on playermodels and items is
r_cellshading 0 or 1

enjoy,
chris lebaron



CODE NOTES:

commented out 	"assert( err == GL_NO_ERROR );" in /win32/glw_imp.c fixed crashing error(dunno why)

added outline code from qmb

added bloom from kmq2

as of 2013 i tried recompiling, you will have to do:
change
#include "afxres.h"
to
#include "windows.h"
in q2.rc
otherwise wouldnt compile
