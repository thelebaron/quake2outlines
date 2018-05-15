/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl_inv.c -- client inventory screen

#include "client.h"

extern cvar_t *inven_pos;

/*
================
CL_ParseInventory
================
*/
void CL_ParseInventory (void)
{
	int		i;

	for (i=0 ; i<MAX_ITEMS ; i++)
		cl.inventory[i] = MSG_ReadShort (&net_message);
}


/*
================
Inv_DrawString
================
*/
void DrawHudString( int x, int y, const char *string, int alpha, float scale);
void Inv_DrawString (int x, int y, char *string)
{
	DrawHudString(x,y,string, 255, 1);
}

void SetStringHighBit (char *s)
{
	while (*s)
		*s++ |= 128;
}

/*
================
CL_DrawInventory
================
*/
#define	DISPLAY_ITEMS	15

void CL_DrawInventory (void)
{
	int		i, j;
	int		num, selected_num, item;
	int		index[MAX_ITEMS];
	char	string[1024];
	int		x, y, w, h;
	char	binding[1024];
	char	*bind;
	int		selected;
	int		top;

	selected = cl.frame.playerstate.stats[STAT_SELECTED_ITEM];

	num = 0;
	selected_num = 0;
	for (i=0 ; i<MAX_ITEMS ; i++)
	{
		if (i==selected)
			selected_num = num;
		if (cl.inventory[i])
		{
			index[num] = i;
			num++;
		}
	}

	// determine scroll point
	top = selected_num - DISPLAY_ITEMS/2;
	if (num - top < DISPLAY_ITEMS)
		top = num - DISPLAY_ITEMS;
	if (top < 0)
		top = 0;

	if (inven_pos->value==0) //center
	{
		x = (viddef.width-256)/2;
		y = (viddef.height-240)/2;
	}
	else if (inven_pos->value==1) //topleft
	{
		x = 0;
		y = 0;
	}
	else if (inven_pos->value==2) //topright
	{
		x = viddef.width-256;
		y = 0;
	}
	else if (inven_pos->value==3) //botleft
	{
		x = 0;
		y = viddef.height-240;
	}
	else //botright
	{
		x = viddef.width-256;
		y = viddef.height-240;
	}

	// repaint everything next frame
	SCR_DirtyScreen ();

	re.DrawPic (x, y+8, "inventory");

	y += 32;
	x += 8;
	Inv_DrawString (x+8, y, "^2^b^skey ### item");
	Inv_DrawString (x+8, y+8, "^2^b^s--- --- -----");
	y += 16;
	for (i=top ; i<num && i < top+(DISPLAY_ITEMS) ; i++)
	{
		item = index[i];
		// search for a binding
		Com_sprintf (binding, sizeof(binding), "use %s", cl.configstrings[CS_ITEMS+item]);
		bind = "";
		for (j=0 ; j<256 ; j++)
			if (keybindings[j] && !Q_stricmp (keybindings[j], binding))
			{
				bind = Key_KeynumToString(j);
				break;
			}

			
		if (item != selected)
		{
			Com_sprintf (string, sizeof(string), " ^0%3s %3i %7s", bind, cl.inventory[item],
				cl.configstrings[CS_ITEMS+item] );
		}
		else	// draw a blinky cursor by the selected item
		{
			Com_sprintf (string, sizeof(string), "^b>^i%3s %3i %7s", bind, cl.inventory[item],
				cl.configstrings[CS_ITEMS+item] );
		}
		Inv_DrawString (x, y, string);
		y += 8;
	}


}


