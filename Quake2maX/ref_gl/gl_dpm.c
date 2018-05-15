/*
Copyright (C) 2002-2003 Victor Luchits

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
// r_dpm.c: darkplaces skeletal animation model format

#include "r_local.h"

static  mesh_t			dpm_mesh;
static	meshbuffer_t	dpm_mbuffer;

static	dpmbonepose_t	dpmbonepose[DPM_MAX_BONES];

/*
=================
R_InitDarkPlacesModels
=================
*/
void R_InitDarkPlacesModels (void)
{
	dpm_mesh.xyz_array = tempVertexArray;
	dpm_mesh.normals_array = tempNormalsArray;
	dpm_mesh.trnormals = NULL;

	dpm_mbuffer.mesh = &dpm_mesh;
}

/*
==============================================================================

DPM MODELS

==============================================================================
*/

/*
=================
Mod_LoadDarkPlacesModel
=================
*/
void Mod_LoadDarkPlacesModel ( model_t *mod, void *buffer )
{
	int				i, j, k, l;
	ddpmheader_t	*pinmodel;
	dpmmodel_t		*poutmodel;
	ddpmbone_t		*pinbone;
	dpmbone_t		*poutbone;
	ddpmmesh_t		*pinmesh;
	dpmmesh_t		*poutmesh;
	ddpmvertex_t	*pindpmvert;
	dpmvertex_t		*poutdpmvert;
	ddpmbonevert_t	*pinbonevert;
	dpmbonevert_t	*poutbonevert;
	ddpmcoord_t		*pinstcoord;
	dpmcoord_t		*poutstcoord;
	index_t			*pintris, *pouttris;
	ddpmframe_t		*pinframe;
	dpmframe_t		*poutframe;
	ddpmbonepose_t	*pinbonepose;
	dpmbonepose_t	*poutbonepose, temppose;

	pinmodel = ( ddpmheader_t * )buffer;

	if ( BigLong (pinmodel->type) != DPM_MODELTYPE ) {
		Com_Error ( ERR_DROP, "%s has wrong type number (%i should be %i)",
				 mod->name, BigLong (pinmodel->type), DPM_MODELTYPE );
	}

	if ( BigLong (pinmodel->filesize) > DPM_MAX_FILESIZE ) {
		Com_Error ( ERR_DROP, "%s has has wrong filesize (%i should be less than %i)",
				 mod->name, BigLong (pinmodel->filesize), DPM_MAX_FILESIZE );
	}

	poutmodel = mod->dpmmodel = Hunk_AllocName ( sizeof(dpmmodel_t), mod->name );
	poutmodel->numbones = BigLong ( pinmodel->num_bones );
	poutmodel->nummeshes = BigLong ( pinmodel->num_meshes );
	poutmodel->numframes = BigLong ( pinmodel->num_frames );

	if ( poutmodel->nummeshes <= 0 ) {
		Com_Error ( ERR_DROP, "model %s has no meshes", mod->name );
	} else if ( poutmodel->nummeshes > DPM_MAX_MESHES ) {
		Com_Error ( ERR_DROP, "model %s has to many meshes", mod->name );
	}

	if ( poutmodel->numbones <= 0 ) {
		Com_Error ( ERR_DROP, "model %s has no bones", mod->name );
	} else if ( poutmodel->numbones > DPM_MAX_BONES ) {
		Com_Error ( ERR_DROP, "model %s has to many bones", mod->name );
	}

	if ( poutmodel->numframes <= 0 ) {
		Com_Error ( ERR_DROP, "model %s has no frames", mod->name );
	} else if ( poutmodel->numframes > DPM_MAX_FRAMES ) {
		Com_Error ( ERR_DROP, "model %s has to many frames", mod->name );
	}

	for ( i = 0; i < 3; i++ ) 
	{
		poutmodel->mins[i] = BigFloat ( pinmodel->mins[i] );
		poutmodel->maxs[i] = BigFloat ( pinmodel->maxs[i] );
	}

	poutmodel->yawradius = BigFloat ( pinmodel->yawradius );
	poutmodel->allradius = BigFloat ( pinmodel->allradius );

	pinbone = ( ddpmbone_t * )( ( byte * )pinmodel + BigLong ( pinmodel->ofs_bones ) );
	poutbone = poutmodel->bones = Hunk_AllocName ( sizeof(dpmbone_t) * poutmodel->numbones, mod->name );

	for ( i = 0; i < poutmodel->numbones; i++, pinbone++, poutbone++ ) 
	{
		Com_sprintf ( poutbone->name, DPM_MAX_NAME, pinbone->name );
		poutbone->flags = BigLong ( pinbone->flags );
		poutbone->parent = BigLong ( pinbone->parent );
	}

	pinmesh = ( ddpmmesh_t * )( ( byte * )pinmodel + BigLong ( pinmodel->ofs_meshes ) );
	poutmesh = poutmodel->meshes = Hunk_AllocName ( sizeof(dpmmesh_t) * poutmodel->nummeshes, mod->name );

	for ( i = 0; i < poutmodel->nummeshes; i++, pinmesh++, poutmesh++ ) 
	{
		poutmesh->numverts = BigLong ( pinmesh->num_verts );
		poutmesh->numtris = BigLong ( pinmesh->num_tris );

		if ( poutmesh->numverts <= 0 ) {
			Com_Error ( ERR_DROP, "mesh %i in model %s has no vertexes", i, mod->name );
		} else if ( poutmesh->numverts > DPM_MAX_VERTS ) {
			Com_Error ( ERR_DROP, "mesh %i in model %s has to many vertexes", i, mod->name );
		}
		if ( poutmesh->numtris <= 0 ) {
			Com_Error ( ERR_DROP, "mesh %i in model %s has no indices", i, mod->name );
		} else if ( poutmesh->numtris > DPM_MAX_TRIS ) {
			Com_Error ( ERR_DROP, "mesh %i in model %s has to many indices", i, mod->name );
		}

		Q_strncpyz ( poutmesh->skin.shadername, pinmesh->shadername, sizeof(poutmesh->skin.shadername) );

		pindpmvert = ( ddpmvertex_t * )( ( byte * )pinmodel + BigLong ( pinmesh->ofs_verts ) );
		poutdpmvert = poutmesh->vertexes = Hunk_AllocName ( sizeof(dpmvertex_t) * poutmesh->numverts, mod->name );

		for ( j = 0; j < poutmesh->numverts; j++, poutdpmvert++ )
		{
			poutdpmvert->numbones = BigLong ( pindpmvert->numbones );

			pinbonevert = ( ddpmbonevert_t * )( ( byte * )pindpmvert + sizeof(poutdpmvert->numbones) );
			poutbonevert = poutdpmvert->verts = Hunk_AllocName ( sizeof(dpmbonevert_t) * poutdpmvert->numbones, mod->name );

			for ( l = 0; l < poutdpmvert->numbones; l++, pinbonevert++, poutbonevert++ )
			{
				for ( k = 0; k < 3; k++ )
				{
					poutbonevert->origin[k] = BigFloat ( pinbonevert->origin[k] );
					poutbonevert->normal[k] = BigFloat ( pinbonevert->normal[k] );
				}

				poutbonevert->influence = BigFloat ( pinbonevert->influence );
				poutbonevert->bonenum = BigLong ( pinbonevert->bonenum );
			}

			pindpmvert = ( ddpmvertex_t * )( ( byte * )pinbonevert );
		}

		pinstcoord = ( ddpmcoord_t * )( ( byte * )pinmodel + BigLong (pinmesh->ofs_texcoords) );
		poutstcoord = poutmesh->stcoords = Hunk_AllocName ( sizeof(dpmcoord_t) * poutmesh->numverts, mod->name );

		for ( j = 0; j < poutmesh->numverts; j++, pinstcoord++, poutstcoord++ )
		{
			poutstcoord->st[0] = BigFloat ( pinstcoord->st[0] );
			poutstcoord->st[1] = BigFloat ( pinstcoord->st[1] );
		}

		pintris = ( index_t * )( ( byte * )pinmodel + BigLong(pinmesh->ofs_indices) );
		pouttris = poutmesh->indexes = Hunk_AllocName ( sizeof(index_t) * poutmesh->numtris * 3, mod->name );

		for ( j = 0; j < poutmesh->numtris; j++, pintris += 3, pouttris += 3 ) {
			pouttris[0] = BigLong ( pintris[0] );
			pouttris[1] = BigLong ( pintris[1] );
			pouttris[2] = BigLong ( pintris[2] );
		}

	//
	// build triangle neighbors
	//
		poutmesh->trneighbors = Hunk_AllocName ( sizeof(int) * poutmesh->numtris * 3, mod->name );
		R_BuildTriangleNeighbors ( poutmesh->trneighbors, poutmesh->indexes, poutmesh->numtris );
	}

	pinframe = ( ddpmframe_t * )( ( byte * )pinmodel + BigLong ( pinmodel->ofs_frames ) );
	poutframe = poutmodel->frames = Hunk_AllocName ( sizeof(dpmframe_t) * poutmodel->numframes, mod->name );

	for ( i = 0; i < poutmodel->numframes; i++, pinframe++, poutframe++ )
	{
		for ( j = 0; j < 3; j++ )
		{
			poutframe->mins[j] = BigFloat ( pinframe->mins[j] );
			poutframe->maxs[j] = BigFloat ( pinframe->maxs[j] );
		}

		poutframe->yawradius = BigFloat ( pinframe->yawradius );
		poutframe->allradius = BigFloat ( pinframe->allradius );

		poutbone = poutmodel->bones;

		pinbonepose = ( ddpmbonepose_t * )( ( byte * )pinmodel + BigLong (pinframe->ofs_bonepositions) );
		poutbonepose = poutframe->boneposes = Hunk_AllocName ( sizeof(dpmbonepose_t) * poutmodel->numbones, mod->name );

		for ( j = 0; j < poutmodel->numbones; j++, poutbone++, pinbonepose++, poutbonepose++ )
		{
			for ( k = 0; k < 3; k++ )
			{
				temppose.matrix[k][0] = BigFloat ( pinbonepose->matrix[k][0] );
				temppose.matrix[k][1] = BigFloat ( pinbonepose->matrix[k][1] );
				temppose.matrix[k][2] = BigFloat ( pinbonepose->matrix[k][2] );
				temppose.matrix[k][3] = BigFloat ( pinbonepose->matrix[k][3] );
			}

			if ( poutbone->parent >= 0 ) {
				R_ConcatTransforms ( &poutframe->boneposes[poutbone->parent].matrix[0][0], &temppose.matrix[0][0], &poutbonepose->matrix[0][0] );
			} else {
				memcpy ( &poutbonepose->matrix[0][0], &temppose.matrix[0][0], sizeof(dpmbonepose_t) );
			}
		}
	}

	mod->radius = poutmodel->allradius;
	VectorCopy ( poutmodel->mins, mod->mins );
	VectorCopy ( poutmodel->maxs, mod->maxs );

	mod->type = mod_dpm;
}

/*
================
Mod_RegisterDarkPlacesModel
================
*/
void Mod_RegisterDarkPlacesModel ( model_t *mod )
{
	int				i;
	dpmmodel_t		*dpmmodel;
	dpmmesh_t		*mesh;
	dpmskin_t		*skin;

	if ( !(dpmmodel = mod->dpmmodel) ) {
		return;
	}

	for ( i = 0, mesh = dpmmodel->meshes; i < dpmmodel->nummeshes; i++, mesh++ ) {
		skin = &mesh->skin;
		skin->shader = R_RegisterSkin ( skin->shadername );
	}
}

/*
** R_DarkPlacesModelLODForDistance
*/
model_t *R_DarkPlacesModelLODForDistance ( entity_t *e )
{
	vec3_t v;
	int lod;
	float dist;

	if ( !e->model->numlods ) {
		return e->model;
	}

	VectorSubtract ( e->origin, r_origin, v );
	dist = VectorLength ( v );
	dist *= tan (r_newrefdef.fov_x * (M_PI/180) * 0.5f);

	lod = (int)(dist / e->model->radius);
	lod /= 8;
	lod += (int)max(r_lodbias->value, 0);

	if ( lod < 1 ) {
		return e->model;
	} else {
		return e->model->lods[min(lod, e->model->numlods)-1];
	}
}

/*
** R_DarkPlacesModelBBox
*/
void R_DarkPlacesModelBBox ( entity_t *e, model_t *mod )
{
	int			i;
	dpmframe_t	*pframe, *poldframe;
	float		*thismins, *oldmins, *thismaxs, *oldmaxs;
	dpmmodel_t	*dpmmodel = mod->dpmmodel;

	if ( ( e->frame >= dpmmodel->numframes ) || ( e->frame < 0 ) )
	{
		Com_DPrintf ("R_DrawDarkPlacesModel %s: no such frame %d\n", mod->name, e->frame);
		e->frame = 0;
	}
	if ( ( e->oldframe >= dpmmodel->numframes ) || ( e->oldframe < 0 ) )
	{
		Com_DPrintf ("R_DrawDarkPlacesModel %s: no such oldframe %d\n", mod->name, e->oldframe);
		e->oldframe = 0;
	}

	pframe = dpmmodel->frames + e->frame;
	poldframe = dpmmodel->frames + e->oldframe;

	/*
	** compute axially aligned mins and maxs
	*/
	if ( pframe == poldframe )
	{
		VectorCopy ( pframe->mins, dpm_mesh.mins );
		VectorCopy ( pframe->maxs, dpm_mesh.maxs );
		dpm_mesh.radius = pframe->allradius;
	}
	else
	{
		thismins = pframe->mins;
		thismaxs = pframe->maxs;

		oldmins  = poldframe->mins;
		oldmaxs  = poldframe->maxs;

		for ( i = 0; i < 3; i++ )
		{
			if ( thismins[i] < oldmins[i] )
				dpm_mesh.mins[i] = thismins[i];
			else
				dpm_mesh.mins[i] = oldmins[i];

			if ( thismaxs[i] > oldmaxs[i] )
				dpm_mesh.maxs[i] = thismaxs[i];
			else
				dpm_mesh.maxs[i] = oldmaxs[i];
		}

		dpm_mesh.radius = max ( poldframe->allradius, pframe->allradius );
	}

	if ( e->scale != 1.0f ) {
		VectorScale ( dpm_mesh.mins, e->scale, dpm_mesh.mins );
		VectorScale ( dpm_mesh.maxs, e->scale, dpm_mesh.maxs );
		dpm_mesh.radius *= e->scale;
	}
}

/*
** R_CullDarkPlacesModel
*/
qboolean R_CullDarkPlacesModel( entity_t *e, model_t *mod )
{
	if ( e->flags & RF_WEAPONMODEL ) {
		return false;
	}
	if ( e->flags & RF_VIEWERMODEL ) {
		return !(r_mirrorview || r_portalview);
	}

	if ( R_CullSphere (e->origin, dpm_mesh.radius, 15) ) {
		return true;
	}

	if ( (r_mirrorview || r_portalview) && !r_nocull->value ) {
		if ( PlaneDiff (e->origin, &r_clipplane) < -dpm_mesh.radius )
			return true;
	}

	return false;
}

/*
================
R_DarkPlacesModelLerpAttachment
================
*/
void R_DarkPlacesModelLerpAttachment ( orientation_t *orient, dpmmodel_t *dpmmodel, int framenum, int oldframenum, 
									  float backlerp, char *name )
{
	int i;
	dpmbone_t		*bone;
	dpmframe_t		*frame, *oldframe;
	dpmbonepose_t	*bonepose, *oldbonepose;

	// find the appropriate attachment bone
	bone = dpmmodel->bones;
	for ( i = 0; i < dpmmodel->numbones; i++, bone++ )
	{
		if ( !(bone->flags & DPM_BONEFLAG_ATTACH) ) {
			continue;
		}
		if ( !Q_stricmp (bone->name, name) ) {
			break;
		}
	}

	if ( i == dpmmodel->numbones ) {
		Com_DPrintf ("R_DPMLerpAttachment: no such bone %s\n", name );
		return;
	}

	// ignore invalid frames
	if ( ( framenum >= dpmmodel->numframes ) || ( framenum < 0 ) )
	{
		Com_DPrintf ("R_DPMLerpAttachment %s: no such oldframe %i\n", name, framenum);
		framenum = 0;
	}
	if ( ( oldframenum >= dpmmodel->numframes ) || ( oldframenum < 0 ) )
	{
		Com_DPrintf ("R_DPMLerpAttachment %s: no such oldframe %i\n", name, oldframenum);
		oldframenum = 0;
	}

	frame = dpmmodel->frames + framenum;
	oldframe = dpmmodel->frames + oldframenum;

	bonepose = frame->boneposes + i;
	oldbonepose = oldframe->boneposes + i;

	// interpolate matrices
	orient->axis[0][0] = bonepose->matrix[0][0] + (oldbonepose->matrix[0][0] - bonepose->matrix[0][0]) * backlerp;
	orient->axis[0][1] = bonepose->matrix[0][1] + (oldbonepose->matrix[0][1] - bonepose->matrix[0][1]) * backlerp;
	orient->axis[0][2] = bonepose->matrix[0][2] + (oldbonepose->matrix[0][2] - bonepose->matrix[0][2]) * backlerp;
	orient->origin[0] = bonepose->matrix[0][3] + (oldbonepose->matrix[0][3] - bonepose->matrix[0][3]) * backlerp;
	orient->axis[1][0] = bonepose->matrix[1][0] + (oldbonepose->matrix[1][0] - bonepose->matrix[1][0]) * backlerp;
	orient->axis[1][1] = bonepose->matrix[1][1] + (oldbonepose->matrix[1][1] - bonepose->matrix[1][1]) * backlerp;
	orient->axis[1][2] = bonepose->matrix[1][2] + (oldbonepose->matrix[1][2] - bonepose->matrix[1][2]) * backlerp;
	orient->origin[1] = bonepose->matrix[1][3] + (oldbonepose->matrix[1][3] - bonepose->matrix[1][3]) * backlerp;
	orient->axis[2][0] = bonepose->matrix[2][0] + (oldbonepose->matrix[2][0] - bonepose->matrix[2][0]) * backlerp;
	orient->axis[2][1] = bonepose->matrix[2][1] + (oldbonepose->matrix[2][1] - bonepose->matrix[2][1]) * backlerp;
	orient->axis[2][2] = bonepose->matrix[2][2] + (oldbonepose->matrix[2][2] - bonepose->matrix[2][2]) * backlerp;
	orient->origin[2] = bonepose->matrix[2][3] + (oldbonepose->matrix[2][3] - bonepose->matrix[2][3]) * backlerp;
}

/*
================
R_DrawBonesFrameLerp
================
*/
void R_DrawBonesFrameLerp ( meshbuffer_t *mb, model_t *mod, float backlerp, qboolean shadow )
{
	int				i, meshnum;
	int				j, k, l;
	int				features;
	vec3_t			move, delta;
	dpmmesh_t		*mesh;
	dpmbone_t		*bone;
	dpmframe_t		*frame, *oldframe;
	dpmbonepose_t	*bonepose, *oldbonepose;
	dpmbonepose_t	*out = dpmbonepose;
	dpmvertex_t		*dpmverts;
	dpmbonevert_t	*boneverts;
	entity_t		*e = mb->entity;
	dpmmodel_t		*dpmmodel = mod->dpmmodel;

	if ( !shadow && (e->flags & RF_VIEWERMODEL) && !r_mirrorview && !r_portalview ) {
		return;
	}

	meshnum = -(mb->infokey + 1);
	if ( meshnum < 0 || meshnum >= dpmmodel->nummeshes ) {
		return;
	}

	mesh = dpmmodel->meshes + meshnum;

	if ( shadow && !mesh->trneighbors ) {
		return;
	}

	bone = dpmmodel->bones;

	frame = dpmmodel->frames + e->frame;
	oldframe = dpmmodel->frames + e->oldframe;

	bonepose = frame->boneposes;
	oldbonepose = oldframe->boneposes;

	// move should be the delta back to the previous frame * backlerp
	VectorSubtract ( e->oldorigin, e->origin, delta );
	Matrix3_Multiply_Vec3 ( e->axis, delta, move );
	VectorScale ( move, backlerp, move );

	out = dpmbonepose;
	for ( i = 0; i < dpmmodel->numbones; i++, bonepose++, oldbonepose++, out++ )
	{
		// interpolate matrices
		out->matrix[0][0] = bonepose->matrix[0][0] + (oldbonepose->matrix[0][0] - bonepose->matrix[0][0]) * backlerp;
		out->matrix[0][1] = bonepose->matrix[0][1] + (oldbonepose->matrix[0][1] - bonepose->matrix[0][1]) * backlerp;
		out->matrix[0][2] = bonepose->matrix[0][2] + (oldbonepose->matrix[0][2] - bonepose->matrix[0][2]) * backlerp;
		out->matrix[0][3] = bonepose->matrix[0][3] + (oldbonepose->matrix[0][3] - bonepose->matrix[0][3]) * backlerp;
		out->matrix[1][0] = bonepose->matrix[1][0] + (oldbonepose->matrix[1][0] - bonepose->matrix[1][0]) * backlerp;
		out->matrix[1][1] = bonepose->matrix[1][1] + (oldbonepose->matrix[1][1] - bonepose->matrix[1][1]) * backlerp;
		out->matrix[1][2] = bonepose->matrix[1][2] + (oldbonepose->matrix[1][2] - bonepose->matrix[1][2]) * backlerp;
		out->matrix[1][3] = bonepose->matrix[1][3] + (oldbonepose->matrix[1][3] - bonepose->matrix[1][3]) * backlerp;
		out->matrix[2][0] = bonepose->matrix[2][0] + (oldbonepose->matrix[2][0] - bonepose->matrix[2][0]) * backlerp;
		out->matrix[2][1] = bonepose->matrix[2][1] + (oldbonepose->matrix[2][1] - bonepose->matrix[2][1]) * backlerp;
		out->matrix[2][2] = bonepose->matrix[2][2] + (oldbonepose->matrix[2][2] - bonepose->matrix[2][2]) * backlerp;
		out->matrix[2][3] = bonepose->matrix[2][3] + (oldbonepose->matrix[2][3] - bonepose->matrix[2][3]) * backlerp;
	}

	dpmverts = mesh->vertexes;
	for ( j = 0; j < mesh->numverts; j++, dpmverts++ ) 
	{
		VectorCopy ( move, tempVertexArray[j] );
		VectorClear ( tempNormalsArray[j] );

		for ( l = 0, boneverts = dpmverts->verts; l < dpmverts->numbones; l++, boneverts++ )
		{
			bonepose = dpmbonepose + boneverts->bonenum;

			for ( k = 0; k < 3; k++ )
			{
				tempVertexArray[j][k] += boneverts->origin[0] * bonepose->matrix[k][0] +
					boneverts->origin[1] * bonepose->matrix[k][1] +
					boneverts->origin[2] * bonepose->matrix[k][2] +
					boneverts->influence * bonepose->matrix[k][3];

				tempNormalsArray[j][k] += boneverts->normal[0] * bonepose->matrix[k][0] +
					boneverts->normal[1] * bonepose->matrix[k][1] +
					boneverts->normal[2] * bonepose->matrix[k][2];
			}
		}
	}

	if ( shadow ) {
		dpm_mesh.st_array = NULL;
		dpm_mesh.trneighbors = mesh->trneighbors;
	} else {
		dpm_mesh.st_array = ( vec2_t * )mesh->stcoords;
		dpm_mesh.trneighbors = NULL;
	}

	dpm_mesh.indexes = mesh->indexes;
	dpm_mesh.numindexes = mesh->numtris * 3;
	dpm_mesh.numvertexes = mesh->numverts;

	dpm_mbuffer.shader = mb->shader;
	dpm_mbuffer.entity = e;
	dpm_mbuffer.fog = mb->fog;

	features = MF_NONBATCHED | dpm_mbuffer.shader->features;
	if ( r_shownormals->value && !shadow ) {
		features |= MF_NORMALS;
	}
	if ( dpm_mbuffer.shader->features & SHADER_AUTOSPRITE ) {
		features |= MF_NOCULL;
	}

	R_RotateForEntity ( e );

	R_PushMesh ( &dpm_mesh, features );
	R_RenderMeshBuffer ( &dpm_mbuffer, shadow );

	if ( shadow ) {
		if ( r_shadows->value == 1) {
			R_Draw_SimpleShadow ( e );
		} else {
			R_DarkPlacesModelBBox ( e, mod );
			R_DrawShadowVolumes ( &dpm_mesh );
		}
	}
}

/*
=================
R_DrawDarkPlacesModel
=================
*/
void R_DrawDarkPlacesModel ( meshbuffer_t *mb, qboolean shadow )
{
	entity_t *e = mb->entity;

	//
	// draw all the triangles
	//
	if (e->flags & RF_DEPTHHACK) // hack the depth range to prevent view model from poking into walls
		qglDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));

	if ( ( e->flags & RF_WEAPONMODEL ) && ( r_lefthand->value == 1.0F ) )
	{
		mat4_t m;

		Matrix4_Identity ( m );
		Matrix4_Scale ( m, -1, 1, 1 );

		qglMatrixMode( GL_PROJECTION );
		qglPushMatrix();
		qglLoadMatrixf ( m );

		MYgluPerspective( r_newrefdef.fov_y, ( float ) r_newrefdef.width / r_newrefdef.height, 4, 12288);
		qglMatrixMode( GL_MODELVIEW );

		qglFrontFace( GL_CW );
	}

	if ( !r_lerpmodels->value ) {
		e->backlerp = 0;
	}

	R_DrawBonesFrameLerp ( mb, R_DarkPlacesModelLODForDistance ( e ), e->backlerp, shadow );

	if ( ( e->flags & RF_WEAPONMODEL ) && ( r_lefthand->value == 1.0F ) ) {
		qglMatrixMode( GL_PROJECTION );
		qglPopMatrix();
		qglMatrixMode( GL_MODELVIEW );
		qglFrontFace( GL_CCW );
	}

	if ( e->flags & RF_DEPTHHACK ) {
		qglDepthRange (gldepthmin, gldepthmax);
	}
}

/*
=================
R_AddDarkPlacesModelToList
=================
*/
void R_AddDarkPlacesModelToList ( entity_t *e )
{
	int				i;
	mfog_t			*fog;
	model_t			*mod;
	dpmmodel_t		*dpmmodel;
	dpmmesh_t		*dpmmesh;

	if ( (e->flags & RF_WEAPONMODEL) && (r_lefthand->value == 2) ) {
		return;
	}

	mod = R_DarkPlacesModelLODForDistance ( e );
	if ( !(dpmmodel = mod->dpmmodel) ) {
		return;
	}

	R_DarkPlacesModelBBox ( e, mod );
	if ( !r_shadows->value && R_CullDarkPlacesModel( e, mod ) ) {
		return;
	}
	
	fog = R_FogForSphere ( e->origin, dpm_mesh.radius );
	for ( i = 0, dpmmesh = dpmmodel->meshes; i < dpmmodel->nummeshes; i++, dpmmesh++ )
	{
		if ( e->customShader )
			R_AddMeshToBuffer ( NULL, fog, NULL, e->customShader, -(i+1) );
		else
			R_AddMeshToBuffer ( NULL, fog, NULL, dpmmesh->skin.shader, -(i+1) );
	}
}
