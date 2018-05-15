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
#include <string.h>
#include <ctype.h>

#include "client.h"
#include "qmenu.h"

float scaledVideo (float param);
float videoScale (void);

static void	 Action_DoEnter( menuaction_s *a );
static void	 Action_Draw( menuaction_s *a );
static void  Menu_DrawStatusBar( const char *string );
static void  Menu_DrawCursorStatusBar( const char *string );
static void	 Menulist_DoEnter( menulist_s *l );
static void	 MenuList_Draw( menulist_s *l );
static void	 Separator_Draw( menuseparator_s *s );
static void	 Slider_DoSlide( menuslider_s *s, int dir );
static void	 Slider_Draw( menuslider_s *s );
static void	 SpinControl_DoEnter( menulist_s *s );
static void	 SpinControl_Draw( menulist_s *s );
static void	 SpinControl_DoSlide( menulist_s *s, int dir );

extern refexport_t re;
extern viddef_t viddef;

#define VID_WIDTH viddef.width
#define VID_HEIGHT viddef.height

#define Draw_Char re.DrawScaledChar
#define Draw_Fill re.DrawFill

float fontScale (void);

int stringLen (char *string);
int stringLengthExtra ( const char *string)
{
	unsigned i, j;
	char modifier;
	float len = strlen( string );

	for ( i = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier>128)
			modifier-=128;

		if ((string[i] == '^') || (i>0 && string[i-1] == '^'))
			j++;
	}

	return j;
}

int stringLength( const char *string )
{
	unsigned i, j, temp, extra, len = strlen( string );
	char modifier;
	qboolean modified;

	for ( i = 0, extra = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier&128) modifier &= ~128;

		if (modifier == '^' && i < len)
		{
			i++;

			modifier = string[i];
			if (modifier&128) modifier &= ~128;

			if (modifier!='^')
			{
				modified = setParams(modifier, &temp, &temp, &temp, &temp, &temp, &temp, &temp);

				if (modified)
				{
					extra++;
					continue;
				}
				else
					i--;
			}
		}
		j++;
	}

	return len - extra;
}

char *unformattedString ( const char *string)
{
	unsigned i;
	int len;
	char character;
	char *newstring = "";

	len = strlen( string );

	for ( i = 0; i < len; i++ )
	{
		character = string[i];

		if (character&128) character &= ~128;
		if (character == '^' && i < len)
		{
			i++;
			continue;
		}
		character = string[i];

		va("%s%c", newstring, character);
	}

	return newstring;
}

int mouseOverAlpha( menucommon_s *m )
{
	if (cursor.menuitem == m)
	{
		int alpha;

		alpha = 125 + 25 * cos(anglemod(cl.time*0.005));

		if (alpha>255) alpha = 255;
		if (alpha<0) alpha = 0;

		return alpha;
	}
	else 
		return 255;
}

void Action_DoEnter( menuaction_s *a )
{
	if ( a->generic.callback )
		a->generic.callback( a );
}

void Action_Draw( menuaction_s *a )
{
	int alpha = mouseOverAlpha(a);

	if ( a->generic.flags & QMF_LEFT_JUSTIFY )
	{
		if ( a->generic.flags & QMF_GRAYED )
			Menu_DrawStringDark( 
			scaledVideo(a->generic.x) + a->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
			scaledVideo(a->generic.y) + a->generic.parent->y,
			a->generic.name, alpha );
		else
			Menu_DrawString( 
			scaledVideo(a->generic.x) + a->generic.parent->x + scaledVideo(LCOLUMN_OFFSET), 
			scaledVideo(a->generic.y) + a->generic.parent->y,
			a->generic.name, alpha );
	}
	else
	{
		if ( a->generic.flags & QMF_GRAYED )
			Menu_DrawStringR2LDark( 
			scaledVideo(a->generic.x) + a->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
			scaledVideo(a->generic.y) + a->generic.parent->y,
			a->generic.name, alpha );
		else
			Menu_DrawStringR2L( 
			scaledVideo(a->generic.x) + a->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
			scaledVideo(a->generic.y) + a->generic.parent->y,
			a->generic.name, alpha );
	}
	if ( a->generic.ownerdraw )
		a->generic.ownerdraw( a );
}

qboolean Field_DoEnter( menufield_s *f )
{
	if ( f->generic.callback )
	{
		f->generic.callback( f );
		return true;
	}
	return false;
}

void Field_Draw( menufield_s *f )
{
	int i, alpha = mouseOverAlpha(f), xtra;
	char tempbuffer[128]="";
	int offset;

	if ( f->generic.name )
		Menu_DrawStringR2LDark( scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
								scaledVideo(f->generic.y) + f->generic.parent->y,
								f->generic.name, 255 );


	if (xtra = stringLengthExtra(f->buffer))
	{
		strncpy( tempbuffer, f->buffer + f->visible_offset, f->visible_length );
		offset = strlen(tempbuffer) - xtra;

		if (offset>f->visible_length)
		{
			f->visible_offset = offset - f->visible_length;
			strncpy( tempbuffer, f->buffer + f->visible_offset - xtra, f->visible_length + xtra );
			offset = f->visible_offset;
		}
	}
	else
	{
		strncpy( tempbuffer, f->buffer + f->visible_offset, f->visible_length );
		offset = strlen(tempbuffer);
	}

	Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*2),
				scaledVideo(f->generic.y) + f->generic.parent->y - scaledVideo(MENU_FONT_SIZE/2),
				18 ,fontScale(), 255,255,255,255,false);

	Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*2),
				scaledVideo(f->generic.y) + f->generic.parent->y + scaledVideo(MENU_FONT_SIZE/2) - 1,
				24 ,fontScale(), 255,255,255,255,false);

	Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*3 + f->visible_length * MENU_FONT_SIZE),
				scaledVideo(f->generic.y) + f->generic.parent->y - scaledVideo(MENU_FONT_SIZE/2),
				20 ,fontScale(), 255,255,255,255,false);

	Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*3 + f->visible_length * MENU_FONT_SIZE),
				scaledVideo(f->generic.y) + f->generic.parent->y + scaledVideo(MENU_FONT_SIZE/2) - 1,
				26 ,fontScale(), 255,255,255,255,false);

	for ( i = 0; i < f->visible_length; i++ )
	{
		Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*3 + i * MENU_FONT_SIZE),
					scaledVideo(f->generic.y) + f->generic.parent->y - scaledVideo(MENU_FONT_SIZE/2),
					19 ,fontScale(), 255,255,255,255,false);
		Draw_Char(	scaledVideo(f->generic.x) + f->generic.parent->x + scaledVideo(MENU_FONT_SIZE*3 + i * MENU_FONT_SIZE),
					scaledVideo(f->generic.y) + f->generic.parent->y + scaledVideo(MENU_FONT_SIZE/2) - 1,
					25 ,fontScale(), 255,255,255,255,false);
	}

	//add cursor thingie
	if ( Menu_ItemAtCursor(f->generic.parent)==f  && ((int)(Sys_Milliseconds()/250))&1 )
			Com_sprintf(tempbuffer, sizeof(tempbuffer),	"%s%c", tempbuffer, 11);

	Menu_DrawString(	scaledVideo(f->generic.x + MENU_FONT_SIZE*2) + f->generic.parent->x,
						scaledVideo(f->generic.y) + f->generic.parent->y,
						tempbuffer, alpha );
}

qboolean Field_Key( menufield_s *f, int key )
{
	extern int keydown[];

	switch ( key )
	{
	case K_KP_SLASH:
		key = '/';
		break;
	case K_KP_MINUS:
		key = '-';
		break;
	case K_KP_PLUS:
		key = '+';
		break;
	case K_KP_HOME:
		key = '7';
		break;
	case K_KP_UPARROW:
		key = '8';
		break;
	case K_KP_PGUP:
		key = '9';
		break;
	case K_KP_LEFTARROW:
		key = '4';
		break;
	case K_KP_5:
		key = '5';
		break;
	case K_KP_RIGHTARROW:
		key = '6';
		break;
	case K_KP_END:
		key = '1';
		break;
	case K_KP_DOWNARROW:
		key = '2';
		break;
	case K_KP_PGDN:
		key = '3';
		break;
	case K_KP_INS:
		key = '0';
		break;
	case K_KP_DEL:
		key = '.';
		break;
	}

	if ( key > 127 )
	{
		switch ( key )
		{
		case K_DEL:
		default:
			return false;
		}
	}

	/*
	** support pasting from the clipboard
	*/
	if ( ( toupper( key ) == 'V' && keydown[K_CTRL] ) ||
		 ( ( ( key == K_INS ) || ( key == K_KP_INS ) ) && keydown[K_SHIFT] ) )
	{
		char *cbd;
		
		if ( ( cbd = Sys_GetClipboardData() ) != 0 )
		{
			strtok( cbd, "\n\r\b" );

			strncpy( f->buffer, cbd, f->length - 1 );
			f->cursor = strlen( f->buffer );
			f->visible_offset = f->cursor - f->visible_length;
			if ( f->visible_offset < 0 )
				f->visible_offset = 0;

			free( cbd );
		}
		return true;
	}

	switch ( key )
	{
	case K_KP_LEFTARROW:
	case K_LEFTARROW:
	case K_BACKSPACE:
		if ( f->cursor > 0 )
		{
			memmove( &f->buffer[f->cursor-1], &f->buffer[f->cursor], strlen( &f->buffer[f->cursor] ) + 1 );
			f->cursor--;

			if ( f->visible_offset )
			{
				f->visible_offset--;
			}
		}
		break;

	case K_KP_DEL:
	case K_DEL:
		memmove( &f->buffer[f->cursor], &f->buffer[f->cursor+1], strlen( &f->buffer[f->cursor+1] ) + 1 );
		break;

	case K_KP_ENTER:
	case K_ENTER:
	case K_ESCAPE:
	case K_TAB:
		return false;

	case K_SPACE:
	default:
		if ( !isdigit( key ) && ( f->generic.flags & QMF_NUMBERSONLY ) )
			return false;

		if ( f->cursor - stringLengthExtra(f->buffer) < f->length  )
		{
			f->buffer[f->cursor++] = key;
			f->buffer[f->cursor] = 0;

			if ( f->cursor > f->visible_length )
			{
				f->visible_offset++;
			}
		}
	}



	return true;
}

void Menu_AddItem( menuframework_s *menu, void *item )
{
	if ( menu->nitems == 0 )
		menu->nslots = 0;

	if ( menu->nitems < MAXMENUITEMS )
	{
		menu->items[menu->nitems] = item;
		( ( menucommon_s * ) menu->items[menu->nitems] )->parent = menu;
		menu->nitems++;
	}

	menu->nslots = Menu_TallySlots( menu );
}

/*
** Menu_AdjustCursor
**
** This function takes the given menu, the direction, and attempts
** to adjust the menu's cursor so that it's at the next available
** slot.
*/
void Menu_AdjustCursor( menuframework_s *m, int dir )
{
	menucommon_s *citem;

	/*
	** see if it's in a valid spot
	*/
	if ( m->cursor >= 0 && m->cursor < m->nitems )
	{
		if ( ( citem = Menu_ItemAtCursor( m ) ) != 0 )
		{
			if ( citem->type != MTYPE_SEPARATOR )
				return;
		}
	}

	/*
	** it's not in a valid spot, so crawl in the direction indicated until we
	** find a valid spot
	*/
	if ( dir == 1 )
	{
		while ( 1 )
		{
			citem = Menu_ItemAtCursor( m );
			if ( citem )
				if ( citem->type != MTYPE_SEPARATOR )
					break;
			m->cursor += dir;
			if ( m->cursor >= m->nitems )
				m->cursor = 0;
		}
	}
	else
	{
		while ( 1 )
		{
			citem = Menu_ItemAtCursor( m );
			if ( citem )
				if ( citem->type != MTYPE_SEPARATOR )
					break;
			m->cursor += dir;
			if ( m->cursor < 0 )
				m->cursor = m->nitems - 1;
		}
	}
}

#define MENUMINTEXT 15
void Menu_Center( menuframework_s *menu )
{
	int height, i, highest, min;
	menucommon_s **items = menu->items;

	for (highest=i=0; i<menu->nitems-1; i++)
		if (items[i]->y > items[highest]->y)
			highest = i;

	height = items[highest]->y;
	height += 10;

	min = viddef.height - scaledVideo(MENU_FONT_SIZE*MENUMINTEXT) - height;

	menu->y = scaledVideo(MENU_FONT_SIZE*MENUMINTEXT) + min*0.25;


}

void Menu_Draw( menuframework_s *menu )
{
	int i;
	menucommon_s *item;

	/*
	** draw contents
	*/
	for ( i = 0; i < menu->nitems; i++ )
	{
		switch ( ( ( menucommon_s * ) menu->items[i] )->type )
		{
		case MTYPE_FIELD:
			Field_Draw( ( menufield_s * ) menu->items[i] );
			break;
		case MTYPE_SLIDER:
			Slider_Draw( ( menuslider_s * ) menu->items[i] );
			break;
		case MTYPE_LIST:
			MenuList_Draw( ( menulist_s * ) menu->items[i] );
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_Draw( ( menulist_s * ) menu->items[i] );
			break;
		case MTYPE_ACTION:
			Action_Draw( ( menuaction_s * ) menu->items[i] );
			break;
		case MTYPE_SEPARATOR:
			Separator_Draw( ( menuseparator_s * ) menu->items[i] );
			break;
		}
	}

	/*
	** now check mouseovers - psychospaz
	*/
	cursor.menu = menu;

	if (cursor.mouseaction)
	{
		menucommon_s *lastitem = cursor.menuitem;
		refreshCursorLink();

		for ( i = menu->nitems; i >= 0 ; i-- )
		{
			int type;
			int len;
			int min[2], max[2];

			item = ((menucommon_s * )menu->items[i]);

			if (!item || item->type == MTYPE_SEPARATOR)
				continue;

			max[0] = min[0] = menu->x + scaledVideo(item->x + RCOLUMN_OFFSET + MENU_FONT_SIZE); //+ 2 chars for space + cursor
			max[1] = min[1] = menu->y + scaledVideo(item->y);
			max[1] += scaledVideo(MENU_FONT_SIZE);

			switch ( item->type )
			{
				case MTYPE_ACTION:
					{
						len = strlen(item->name);
						
						if (item->flags & QMF_LEFT_JUSTIFY)
						{
							min[0] += scaledVideo(LCOLUMN_OFFSET*2);
							max[0] = min[0] + scaledVideo(len*MENU_FONT_SIZE);
						}
						else
						{
							min[0] -= scaledVideo(len*MENU_FONT_SIZE + MENU_FONT_SIZE*3);
						}

						type = MENUITEM_ACTION;
					}
					break;
				case MTYPE_SLIDER:
					{
						min[0] -= scaledVideo(16);
						max[0] += scaledVideo((SLIDER_RANGE + 4)*MENU_FONT_SIZE);
						type = MENUITEM_SLIDER;
					}
					break;
				case MTYPE_LIST:
				case MTYPE_SPINCONTROL:
					{
						int len;
						menulist_s *spin = menu->items[i];


						if (item->name)
						{
							len = strlen(item->name);
							min[0] -= scaledVideo(len*MENU_FONT_SIZE - LCOLUMN_OFFSET*2);
						}

						len = strlen(spin->itemnames[spin->curvalue]);
						max[0] += scaledVideo(len*MENU_FONT_SIZE);

						type = MENUITEM_ROTATE;
					}
					break;
				case MTYPE_FIELD:
					{
						menufield_s *text = menu->items[i];

						len = text->visible_length + 1;

						min[0] -= scaledVideo(MENU_FONT_SIZE);
						max[0] += scaledVideo(len*MENU_FONT_SIZE);
						type = MENUITEM_TEXT;
					}
					break;
				default:
					continue;
			}

			if (cursor.x>=min[0] && 
				cursor.x<=max[0] &&
				cursor.y>=min[1] && 
				cursor.y<=max[1])
			{
				//new item
				if (lastitem!=item)
				{
					int j;

					for (j=0;j<MENU_CURSOR_BUTTON_MAX;j++)
					{
						cursor.buttonclicks[j] = 0;
						cursor.buttontime[j] = 0;
					}
				}
				cursor.menuitem = item;
				cursor.menuitemtype = type;
				
				menu->cursor = i;

				break;
			}
		}
	}

	if (cursor.x != cursor.oldx || cursor.y != cursor.oldy)
		cursor.menutime = cl.time*0.01;

	cursor.mouseaction = false;

	item = Menu_ItemAtCursor( menu );

	if ( item && item->cursordraw )
	{
		item->cursordraw( item );
	}
	else if ( menu->cursordraw )
	{
		menu->cursordraw( menu );
	}
	else if ( item && item->type != MTYPE_FIELD )
	{
		if ( item->flags & QMF_LEFT_JUSTIFY )
		{
			Draw_Char(	menu->x + scaledVideo(item->x - 24 + item->cursor_offset),
						menu->y + scaledVideo(item->y),
						12 + ( ( int ) ( Sys_Milliseconds()/250 ) & 1 ) ,fontScale(), 255,255,255,255,false);
		}
		else
		{
			Draw_Char(	menu->x + scaledVideo(item->cursor_offset),
						menu->y + scaledVideo(item->y),
						12 + ( ( int ) ( Sys_Milliseconds()/250 ) & 1 ) ,fontScale(), 255,255,255,255,false);
		}
	}

	if ( item )
	{
		if ( item->statusbarfunc )
			item->statusbarfunc( ( void * ) item );
		else if ( item->statusbar )
		{
			if (cursor.menuitem == item)
				Menu_DrawCursorStatusBar( item->statusbar );
		//	else
		//		Menu_DrawStatusBar( item->statusbar );
		}
		else
			Menu_DrawStatusBar( menu->statusbar );

	}
	else
	{
		Menu_DrawStatusBar( menu->statusbar );
	}
}

#define STATUSINVISIBLETIME 6
void Menu_DrawCursorStatusBar( const char *string )
{
	float alpha, time = cl.time*0.01 - cursor.menutime;

	if (time < STATUSINVISIBLETIME)
		return;

	if ( string )
	{
		int l = strlen( string ), x, y, w ,h;

		w = l * scaledVideo(MENU_FONT_SIZE + 2);
		h = scaledVideo(MENU_FONT_SIZE)*2;

		x = cursor.x - w*0.5;
		y = cursor.y - h;

		alpha = (time - STATUSINVISIBLETIME) * 0.25;
		if (alpha > 1) alpha = 1;

		re.DrawFadeBox(x-1, y-1, w+2, h+2, 0, 0, 0, alpha);
		re.DrawFadeBox(x, y, w, h, 0.25, 0.25, 0.25, alpha);
		Menu_DrawString( 
			x, 
			y + scaledVideo(MENU_FONT_SIZE)*0.5, 
			string, alpha * 255 );

	}
}

void Menu_DrawStatusBar( const char *string )
{
	if ( string )
	{
		int l = strlen( string );
		int maxrow = VID_HEIGHT / scaledVideo(MENU_FONT_SIZE);
		int maxcol = VID_WIDTH / scaledVideo(MENU_FONT_SIZE);
		int col = maxcol / 2 - l / 2;

		Draw_Fill( 0, VID_HEIGHT-scaledVideo(MENU_FONT_SIZE+2), VID_WIDTH, scaledVideo(MENU_FONT_SIZE+2), 4 );
		Draw_Fill( 0, VID_HEIGHT-scaledVideo(MENU_FONT_SIZE+2), VID_WIDTH, 1, 0 );

		Menu_DrawString( scaledVideo(col*8), VID_HEIGHT - scaledVideo(MENU_FONT_SIZE+1), string, 255 );
		
	}
/*
	else
	{
		Draw_Fill( 0, VID_HEIGHT-scaledVideo(MENU_FONT_SIZE+2), VID_WIDTH, scaledVideo(MENU_FONT_SIZE+2), 4 );
		Draw_Fill( 0, VID_HEIGHT-scaledVideo(MENU_FONT_SIZE+2), VID_WIDTH, 1, 0 );
	}
*/
}

float fontScale (void)
{
	return scaledVideo(MENU_FONT_SIZE)/con_font_size->value;
}

qboolean setParams(char modifier, int *red, int *green, int *blue, int *bold, int *shadow, int *italic, int *reset);
void Menu_DrawString( int x, int y, const char *string, int alpha )
{
	unsigned i, j;
	char modifier, character;
	int len, red, green, blue, italic, shadow, bold, reset;
	qboolean modified;

	//default
	red = 255;
	green = 255;
	blue = 255;
	italic = false;
	shadow = true;
	bold = false;

	len = strlen( string );
	for ( i = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier&128) modifier &= ~128;

		if (modifier == '^' && i < len)
		{
			i++;

			reset = 0;
			modifier = string[i];
			if (modifier&128) modifier &= ~128;

			if (modifier!='^')
			{
				modified = setParams(modifier, &red, &green, &blue, &bold, &shadow, &italic, &reset);

				if (reset)
				{
					red = 255;
					green = 255;
					blue = 255;
					italic = false;
					shadow = true;
					bold = false;
				}
				if (modified)
					continue;
				else
					i--;
			}
		}
		j++;

		character = string[i];
		if (bold && character<128)
			character += 128;
		else if (bold && character>128)
			character -= 128;

		if (shadow)
			Draw_Char( ( x + scaledVideo(j*MENU_FONT_SIZE+2) ), y+scaledVideo(1),  character,fontScale(), 0, 0, 0, alpha, italic);
		Draw_Char( ( x + scaledVideo(j*MENU_FONT_SIZE) ), y, character ,fontScale(), red, green, blue, alpha, italic);
	}
}

void Menu_DrawStringDark( int x, int y, const char *string, int alpha )
{
	unsigned i, j;
	char modifier, character;
	int len, red, green, blue, italic, shadow, bold, reset;
	qboolean modified;

	//default
	red = 0;
	green = 255;
	blue = 0;
	italic = false;
	shadow = true;
	bold = true;

	len = strlen( string );
	for ( i = 0, j = 0; i < len; i++ )
	{
		modifier = string[i];
		if (modifier&128) modifier &= ~128;

		if (modifier == '^' && i < len)
		{
			i++;

			reset = 0;
			modifier = string[i];
			if (modifier&128) modifier &= ~128;

			if (modifier!='^')
			{
				modified = setParams(modifier, &red, &green, &blue, &bold, &shadow, &italic, &reset);

				if (reset)
				{
					red = 0;
					green = 255;
					blue = 0;
					italic = false;
					shadow = true;
					bold = false;
				}
				if (modified)
					continue;
				else
					i--;
			}
		}
		j++;

		character = string[i];
		if (bold && character<128)
			character += 128;
		else if (bold && character>128)
			character -= 128;

		if (shadow)
			Draw_Char( ( x + scaledVideo(j*MENU_FONT_SIZE+2) ), y+scaledVideo(1),  character,fontScale(), 0, 0, 0, alpha, italic);
		Draw_Char( ( x + scaledVideo(j*MENU_FONT_SIZE) ), y, character ,fontScale(), red, green, blue, alpha, italic);
	}
}

void Menu_DrawStringR2L( int x, int y, const char *string, int alpha )
{
	x -= stringLen(string)*scaledVideo(MENU_FONT_SIZE);
	Menu_DrawString(x, y, string, alpha);
}

void Menu_DrawStringR2LDark( int x, int y, const char *string, int alpha )
{
	x -= stringLen(string)*scaledVideo(MENU_FONT_SIZE);
	Menu_DrawStringDark(x, y, string, alpha);
}

void *Menu_ItemAtCursor( menuframework_s *m )
{
	if ( m->cursor < 0 || m->cursor >= m->nitems )
		return 0;

	return m->items[m->cursor];
}

qboolean Menu_SelectItem( menuframework_s *s )
{
	menucommon_s *item = ( menucommon_s * ) Menu_ItemAtCursor( s );

	if ( item )
	{
		switch ( item->type )
		{
		case MTYPE_FIELD:
			return Field_DoEnter( ( menufield_s * ) item ) ;
		case MTYPE_ACTION:
			Action_DoEnter( ( menuaction_s * ) item );
			return true;
		case MTYPE_LIST:
//			Menulist_DoEnter( ( menulist_s * ) item );
			return false;
		case MTYPE_SPINCONTROL:
//			SpinControl_DoEnter( ( menulist_s * ) item );
			return false;
		}
	}
	return false;
}


qboolean Menu_MouseSelectItem( menucommon_s *item )
{
	if ( item )
	{
		switch ( item->type )
		{
		case MTYPE_FIELD:
			return Field_DoEnter( ( menufield_s * ) item ) ;
		case MTYPE_ACTION:
			Action_DoEnter( ( menuaction_s * ) item );
			return true;
		case MTYPE_LIST:
		case MTYPE_SPINCONTROL:
			return false;
		}
	}
	return false;
}

void Menu_SetStatusBar( menuframework_s *m, const char *string )
{
	m->statusbar = string;
}

void Menu_SlideItem( menuframework_s *s, int dir )
{
	menucommon_s *item = ( menucommon_s * ) Menu_ItemAtCursor( s );

	if ( item )
	{
		switch ( item->type )
		{
		case MTYPE_SLIDER:
			Slider_DoSlide( ( menuslider_s * ) item, dir );
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_DoSlide( ( menulist_s * ) item, dir );
			break;
		}
	}
}

int Menu_TallySlots( menuframework_s *menu )
{
	int i;
	int total = 0;

	for ( i = 0; i < menu->nitems; i++ )
	{
		if ( ( ( menucommon_s * ) menu->items[i] )->type == MTYPE_LIST )
		{
			int nitems = 0;
			const char **n = ( ( menulist_s * ) menu->items[i] )->itemnames;

			while (*n)
				nitems++, n++;

			total += nitems;
		}
		else
		{
			total++;
		}
	}

	return total;
}

void Menulist_DoEnter( menulist_s *l )
{
	int start;

	start = l->generic.y / 10 + 1;

	l->curvalue = l->generic.parent->cursor - start;

	if ( l->generic.callback )
		l->generic.callback( l );
}

void MenuList_Draw( menulist_s *l )
{
	const char **n;
	int y = 0, alpha = mouseOverAlpha(l);

	Menu_DrawStringR2LDark( 
		scaledVideo(l->generic.x) + l->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
		scaledVideo(l->generic.y) + l->generic.parent->y, l->generic.name, alpha);

	n = l->itemnames;

  	Draw_Fill( scaledVideo(l->generic.x - 112) + l->generic.parent->x,
		l->generic.parent->y + scaledVideo(l->generic.y + l->curvalue*10 + 10),
		128, 10, 16 );
	while ( *n )
	{
		Menu_DrawStringR2LDark( 
			scaledVideo(l->generic.x) + l->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
			scaledVideo(l->generic.y) + l->generic.parent->y + scaledVideo(y + 10), *n, alpha );

		n++;
		y += 10;
	}
}

void Separator_Draw( menuseparator_s *s )
{
	int alpha = mouseOverAlpha(s);

	if ( s->generic.name )
		Menu_DrawStringR2LDark( 
			scaledVideo(s->generic.x) + s->generic.parent->x,
			scaledVideo(s->generic.y) + s->generic.parent->y,
			s->generic.name, alpha );
}

void Slider_DoSlide( menuslider_s *s, int dir )
{
	s->curvalue += dir;

	if ( s->curvalue > s->maxvalue )
		s->curvalue = s->maxvalue;
	else if ( s->curvalue < s->minvalue )
		s->curvalue = s->minvalue;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void Slider_Draw( menuslider_s *s )
{
	int i, alpha = mouseOverAlpha(s);

	Menu_DrawStringR2LDark( scaledVideo(s->generic.x) + s->generic.parent->x + scaledVideo(LCOLUMN_OFFSET),
		                scaledVideo(s->generic.y) + s->generic.parent->y, 
						s->generic.name, alpha );

	s->range = ( s->curvalue - s->minvalue ) / ( float ) ( s->maxvalue - s->minvalue );

	if ( s->range < 0)
		s->range = 0;
	if ( s->range > 1)
		s->range = 1;

	Draw_Char( scaledVideo(s->generic.x) + s->generic.parent->x + scaledVideo(RCOLUMN_OFFSET), 
		scaledVideo(s->generic.y) + s->generic.parent->y, 128,fontScale(), 255,255,255,255,false);

	for ( i = 0; i < SLIDER_RANGE; i++ )
		Draw_Char( scaledVideo(RCOLUMN_OFFSET + s->generic.x + (i+1)*MENU_FONT_SIZE) + s->generic.parent->x,
		scaledVideo(s->generic.y) + s->generic.parent->y, 129,fontScale(), 255,255,255,255,false);

	Draw_Char( scaledVideo(RCOLUMN_OFFSET + s->generic.x + (i+1)*MENU_FONT_SIZE) + s->generic.parent->x,
		scaledVideo(s->generic.y) + s->generic.parent->y, 130,fontScale(), 255,255,255,255,false);

	Draw_Char( 
		s->generic.parent->x + scaledVideo(RCOLUMN_OFFSET + s->generic.x + (SLIDER_RANGE)*MENU_FONT_SIZE * s->range ),
		scaledVideo(s->generic.y) + s->generic.parent->y, 131,fontScale(), 255,255,255,255,false);
}

void SpinControl_DoEnter( menulist_s *s )
{
	s->curvalue++;
	if ( s->itemnames[s->curvalue] == 0 )
		s->curvalue = 0;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void SpinControl_DoSlide( menulist_s *s, int dir )
{
	s->curvalue += dir;

	if ( s->curvalue < 0 )
		s->curvalue = 0;
	else if ( s->itemnames[s->curvalue] == 0 )
		s->curvalue--;

	if ( s->generic.callback )
		s->generic.callback( s );
}

void SpinControl_Draw( menulist_s *s )
{
	int alpha = mouseOverAlpha(s);
	char buffer[100];

	if ( s->generic.name )
	{
		Menu_DrawStringR2LDark( scaledVideo(s->generic.x) + s->generic.parent->x + scaledVideo(LCOLUMN_OFFSET), 
							scaledVideo(s->generic.y) + s->generic.parent->y, 
							s->generic.name, alpha );
	}
	if ( !strchr( s->itemnames[s->curvalue], '\n' ) )
	{
		Menu_DrawString( scaledVideo(RCOLUMN_OFFSET + s->generic.x) + s->generic.parent->x, 
			scaledVideo(s->generic.y) + s->generic.parent->y, s->itemnames[s->curvalue], alpha );
	}
	else
	{
		strcpy( buffer, s->itemnames[s->curvalue] );
		*strchr( buffer, '\n' ) = 0;
		Menu_DrawString( scaledVideo(RCOLUMN_OFFSET + s->generic.x) + s->generic.parent->x,
			scaledVideo(s->generic.y) + s->generic.parent->y, buffer, alpha );
		strcpy( buffer, strchr( s->itemnames[s->curvalue], '\n' ) + 1 );
		Menu_DrawString( scaledVideo(RCOLUMN_OFFSET + s->generic.x) + s->generic.parent->x, 
			scaledVideo(s->generic.y) + s->generic.parent->y + scaledVideo(MENU_FONT_SIZE), buffer, alpha );
	}
}

