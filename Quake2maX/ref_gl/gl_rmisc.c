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
// r_misc.c

#include "gl_local.h"
#include "jpeglib.h" //Heffo - JPEG Screenshots

/*
==================
R_InitParticleTexture
==================
*/

void SetParticlePicture (int num, char *name)
{
	r_particletextures[num] = GL_FindImage(name, it_part);
	if (!r_particletextures[num])
		r_particletextures[num] = r_notexture;
}

byte	notexture[8][8] =
{
	{	075,	075,	075,	075,	255,	255,	255,	255	},
	{	075,	075,	075,	075,	255,	255,	255,	255	},
	{	075,	075,	075,	075,	255,	255,	255,	255	},
	{	075,	075,	075,	075,	255,	255,	255,	255	},
	{	255,	255,	255,	255,	175,	175,	175,	175	},
	{	255,	255,	255,	255,	175,	175,	175,	175	},
	{	255,	255,	255,	255,	175,	175,	175,	175	},
	{	255,	255,	255,	255,	175,	175,	175,	175	},
};

#define CEL_SIZE 32

const byte celcolors [CEL_SIZE][2] =
{
	//3 (3)
	0,		255,
	0,		255,
	0,		255,

	//5 (8)
	0,		170,
	0,		170,
	0,		170,
	0,		170,
	0,		170,

	//8 (16)
	0,		85,
	0,		85,
	0,		85,
	0,		85,
	0,		85,
	0,		85,
	0,		85,
	0,		85,

	//16 (32)
	0,		0,
	0,		0,
	0,		0,
	0,		0,
	0,		0,
	0,		0,
	0,		0,
	0,		0,
	255,	0,
	255,	0,
	255,	0,
	255,	0,
	255,	0,
	255,	0,
	255,	0,
	255,	0,
};

#define ENVMAPHACKSIZE 256
void R_InitParticleTexture (void)
{
	int		x,y;
	byte	no_data[8][8][4];
	byte	cel_data[CEL_SIZE][CEL_SIZE][4];
	byte	env_data[ENVMAPHACKSIZE*ENVMAPHACKSIZE*4];

	//
	// also use this for bad textures, but without alpha
	//
	for (x=0 ; x<8 ; x++)
		for (y=0 ; y<8 ; y++)
		{
			no_data[y][x][0] = notexture[y][x];
			no_data[y][x][1] = notexture[y][x];
			no_data[y][x][2] = notexture[y][x];
			no_data[y][x][3] = 255;
		}

	r_notexture = GL_LoadPic ("***r_notexture***", (byte *)no_data, 8, 8, it_wall, 32);

	r_particlebeam = GL_FindImage("particles/beam.png", it_part);
	if (!r_particlebeam)
		r_particlebeam = r_notexture;

	for (x=0 ; x<PARTICLE_TYPES ; x++)
		r_particletextures[x] = NULL;

	memset((void *)env_data, 0, ENVMAPHACKSIZE*ENVMAPHACKSIZE*4);
	r_dynamicimage = GL_LoadPic ("***r_dynamicimage***", (byte *)env_data, ENVMAPHACKSIZE, ENVMAPHACKSIZE, it_wall, 32);

	for (x=0 ; x<CEL_SIZE ; x++)
		for (y=0 ; y<CEL_SIZE ; y++)
		{
			cel_data[y][x][0] = (byte)celcolors[x][0];
			cel_data[y][x][1] = (byte)celcolors[x][0];
			cel_data[y][x][2] = (byte)celcolors[x][0];
			cel_data[y][x][3] = (byte)celcolors[x][1];
		}

	//set texture mode too
	r_celtexture = GL_LoadPic ("***r_celtexture***", (byte *)cel_data, CEL_SIZE, CEL_SIZE, it_pic, 32);
	GL_Bind (r_celtexture->texnum);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	ri.SetParticlePics();
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/ 

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

void GL_ScreenShot_PNG (void);

/* 
================== 
GL_ScreenShot_JPG
By Robert 'Heffo' Heffernan
================== 
*/
void GL_ScreenShot_JPG (void)
{
	struct jpeg_compress_struct		cinfo;
	struct jpeg_error_mgr			jerr;
	byte							*rgbdata;
	JSAMPROW						s[1];
	FILE							*file;
	char							picname[80], checkname[MAX_OSPATH];
	int								i, offset;

	// Create the scrnshots directory if it doesn't exist
	Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot", ri.FS_Gamedir());
	Sys_Mkdir (checkname);

	for (i=0 ; i<=999 ; i++) 
	{ 
		Com_sprintf (picname, sizeof(picname), "quake2max%i%i%i.jpg", (int)(i/100)%10, (int)(i/10)%10, i%10);
		Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot/%s", ri.FS_Gamedir(), picname);
		file = fopen (checkname, "rb");
		if (!file)
			break;	// file doesn't exist
		fclose (file);
	} 
	if (i==1000) 
	{
		ri.Con_Printf (PRINT_ALL, "SCR_JPGScreenShot_f: Couldn't create a file\n"); 
		return;
 	}

	// Open the file for Binary Output
	file = fopen(checkname, "wb");
	if(!file)
	{
		ri.Con_Printf (PRINT_ALL, "SCR_JPGScreenShot_f: Couldn't create a file\n"); 
		return;
 	}

	// Allocate room for a copy of the framebuffer
	rgbdata = malloc(vid.width * vid.height * 3);
	if(!rgbdata)
	{
		fclose(file);
		return;
	}

	// Read the framebuffer into our storage
	qglReadPixels(0, 0, vid.width, vid.height, GL_RGB, GL_UNSIGNED_BYTE, rgbdata);

	// Initialise the JPEG compression object
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, file);

	// Setup JPEG Parameters
	cinfo.image_width = vid.width;
	cinfo.image_height = vid.height;
	cinfo.in_color_space = JCS_RGB;
	cinfo.input_components = 3;
	jpeg_set_defaults(&cinfo);

	if((gl_screenshot_quality->value > 100) || (gl_screenshot_quality->value <= 0))
		ri.Cvar_Set("gl_screenshot_quality", "85");

	jpeg_set_quality(&cinfo, gl_screenshot_quality->value, TRUE);

	// Start Compression
	jpeg_start_compress(&cinfo, true);

	// Feed Scanline data
	offset = (cinfo.image_width * cinfo.image_height * 3) - (cinfo.image_width * 3);
	while(cinfo.next_scanline < cinfo.image_height)
	{
		s[0] = &rgbdata[offset - (cinfo.next_scanline * (cinfo.image_width * 3))];
		jpeg_write_scanlines(&cinfo, s, 1);
	}

	// Finish Compression
	jpeg_finish_compress(&cinfo);

	// Destroy JPEG object
	jpeg_destroy_compress(&cinfo);

	// Close File
	fclose(file);

	// Free Temp Framebuffer
	free(rgbdata);

	// Done!
	ri.Con_Printf (PRINT_ALL, "Wrote %s\n", picname);
}

/* 
================== 
GL_ScreenShot_f
================== 
*/  
void GL_ScreenShot_f (void) 
{
	byte		*buffer;
	char		picname[80]; 
	char		checkname[MAX_OSPATH];
	int			i, c, temp;
	FILE		*f;

	if (ri.Cmd_Argc()>1)
	{
		if (!strcmp ("tga", ri.Cmd_Argv(1)))
		{
			//now we continue
		}		
		else if (!strcmp ("png", ri.Cmd_Argv(1)))
		{
			GL_ScreenShot_PNG();
			return;
		}
		else if (!strcmp ("jpg", ri.Cmd_Argv(1)))
		{
			GL_ScreenShot_JPG();
			return;
		}
		else //bad params
		{
			return;
		}
	}
	else //default is jpg
	{
		GL_ScreenShot_JPG();
		return;
	}

	// 
	// find a file name to save it to 
	// 

	// create the scrnshots directory if it doesn't exist
	Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot", ri.FS_Gamedir());
	Sys_Mkdir (checkname);

	for (i=0 ; i<=999 ; i++) 
	{
		Com_sprintf (picname, sizeof(picname), "quake2max%i%i%i.tga", (int)(i/100)%10, (int)(i/10)%10, i%10);
		Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot/%s", ri.FS_Gamedir(), picname);
		f = fopen (checkname, "rb");
		if (!f)
			break;	// file doesn't exist
		fclose (f);
	}
	if (i==1000) 
	{
		ri.Con_Printf (PRINT_ALL, "SCR_ScreenShot_f: Couldn't create a file\n"); 
		return;
 	}

	buffer = malloc(vid.width*vid.height*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = vid.width&255;
	buffer[13] = vid.width>>8;
	buffer[14] = vid.height&255;
	buffer[15] = vid.height>>8;
	buffer[16] = 24;	// pixel size

	qglReadPixels (0, 0, vid.width, vid.height, GL_RGB, GL_UNSIGNED_BYTE, buffer+18 ); 

	// swap rgb to bgr
	c = 18+vid.width*vid.height*3;
	for (i=18 ; i<c ; i+=3)
	{
		temp = buffer[i];
		buffer[i] = buffer[i+2];
		buffer[i+2] = temp;
	}

	f = fopen (checkname, "wb");
	fwrite (buffer, 1, c, f);
	fclose (f);

	free (buffer);
	ri.Con_Printf (PRINT_ALL, "Wrote %s\n", picname);
} 

/*
** GL_Strings_f
*/
void GL_Strings_f( void )
{
	ri.Con_Printf (PRINT_ALL, "GL_VENDOR: %s\n", gl_config.vendor_string );
	ri.Con_Printf (PRINT_ALL, "GL_RENDERER: %s\n", gl_config.renderer_string );
	ri.Con_Printf (PRINT_ALL, "GL_VERSION: %s\n", gl_config.version_string );
	ri.Con_Printf (PRINT_ALL, "GL_EXTENSIONS: %s\n", gl_config.extensions_string );
}

/*
** GL_SetDefaultState
*/
void GL_SetDefaultState( void )
{
	qglClearColor (1,0, 0.5 , 0.5);
	qglCullFace(GL_FRONT);
	qglEnable(GL_TEXTURE_2D);

	qglEnable( GL_ALPHA_TEST);
	qglAlphaFunc(GL_GREATER, 0.666);
	gl_state.alpha_test=true;

	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_CULL_FACE);
	qglDisable (GL_BLEND);
	gl_state.blend=false;

	qglColor4f (1,1,1,1);

	qglPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_ShadeModel (GL_FLAT);

	GL_TextureMode( gl_texturemode->string );
	GL_TextureAlphaMode( gl_texturealphamode->string );
	GL_TextureSolidMode( gl_texturesolidmode->string );

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL_TexEnv( GL_REPLACE );

	GL_UpdateSwapInterval();
}

void GL_UpdateSwapInterval( void )
{
	if ( gl_swapinterval->modified )
	{
		gl_swapinterval->modified = false;

		if ( !gl_state.stereo_enabled ) 
		{
#ifdef _WIN32
			if ( qwglSwapIntervalEXT )
				qwglSwapIntervalEXT( gl_swapinterval->value );
#endif
		}
	}
}