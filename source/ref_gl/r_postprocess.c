/*
Copyright (C) 2009-2014 COR Entertainment

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "r_local.h"

#define EXPLOSION 1
#define PAIN 2

extern float v_blend[4];
extern void R_TransformVectorToScreen( refdef_t *rd, vec3_t in, vec2_t out );
void R_DrawBloodEffect (void);

image_t *r_framebuffer;
image_t *r_distortwave;
image_t *r_droplets;
image_t	*r_blooddroplets;
image_t	*r_blooddroplets_nm;

vec3_t r_explosionOrigin;
int r_drawing_fbeffect;
int	r_fbFxType;
float r_fbeffectTime;

extern  cvar_t	*cl_raindist;

static GLuint distort_FBO;

/*
=================
R_Postprocess_AllocFBOTexture 

Create a 24-bit texture with specified size and attach it to an FBO
=================
*/
image_t *R_Postprocess_AllocFBOTexture (char *name, int width, int height, GLuint *FBO)
{
	byte	*data;
	int		size;
	image_t	*image;
	
	size = width * height * 3;
	data = malloc (size);
	memset (data, 0, size);
	
	// create the texture
	image = GL_FindFreeImage (name, width, height, it_pic);
	GL_Bind (image->texnum);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	qglTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	qglTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	
	// create up the FBO
	qglGenFramebuffersEXT (1, FBO);
	qglBindFramebufferEXT (GL_FRAMEBUFFER_EXT, *FBO);

	// bind the texture to it
	qglFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, image->texnum, 0);
	
	// clean up 
	free (data);
	qglBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
	
	return image;
}

// Be sure to set up your GLSL program and uniforms before calling this! Make
// sure r_framebuffer is already bound to a TMU, and tell this function which
// TMU it is.
static void Distort_RenderQuad (int x, int y, int w, int h)
{
	int xl = x, yt = y, xr = x + w, yb = y + h;
	
	//set up full screen workspace
	qglMatrixMode (GL_PROJECTION );
	qglLoadIdentity ();
	qglOrtho (0, 1, 1, 0, -10, 100);
	qglMatrixMode (GL_MODELVIEW);
	qglLoadIdentity ();
	
	GLSTATE_DISABLE_BLEND
	qglDisable (GL_DEPTH_TEST);
	
	GL_MBind (0, r_framebuffer->texnum);
	
	// Ie need to grab the frame buffer. If we're not working with the whole
	// framebuffer, copy a margin to avoid artifacts
	qglBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT, distort_FBO);
	if (xl > 0) xl--;
	if (xr < viddef.width) xr++;
	if (yt > 0) yt--;
	if (yb < viddef.height) yb++;
	qglBlitFramebufferEXT (xl, yt, xr, yb, xl, yt, xr, yb, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	qglBindFramebufferEXT (GL_DRAW_FRAMEBUFFER_EXT, 0);
	
	qglViewport (x, y, w, h);
	
	// Set the texture matrix so the GLSL shader knows only to sample part
	// of the FB texture
	GL_SelectTexture (0);
	qglMatrixMode (GL_TEXTURE);
	qglPushMatrix ();
	qglTranslatef ((float)x/(float)viddef.width, (float)y/(float)viddef.height, 0);
	qglScalef ((float)w/(float)viddef.width, (float)h/(float)viddef.height, 1);
	
	// FIXME: textures captured from the framebuffer need to be rendered
	// upside down for some reason
	GL_SetupWholeScreen2DVBO (wholescreen_fliptextured);
	R_DrawVarrays (GL_QUADS, 0, 4);
	
	qglPopMatrix ();
	qglMatrixMode (GL_MODELVIEW);
	R_KillVArrays ();
	R_SetupViewport ();
	GLSTATE_ENABLE_BLEND
	qglEnable (GL_DEPTH_TEST);
}

void R_GLSLDistortion(void)
{
	vec3_t	vec;
	float	dot, r_fbeffectLen;
	vec3_t	forward, mins, maxs;
	trace_t r_trace;
	
	if(r_fbFxType == EXPLOSION) 
	{
		//is it in our view?
		AngleVectors (r_newrefdef.viewangles, forward, NULL, NULL);
		VectorSubtract (r_explosionOrigin, r_newrefdef.vieworg, vec);
		VectorNormalize (vec);
		dot = DotProduct (vec, forward);
		if (dot <= 0.3)
			r_drawing_fbeffect = false;

		//is anything blocking it from view?
		VectorSet(mins, 0, 0, 0);
		VectorSet(maxs,	0, 0, 0);

		r_trace = CM_BoxTrace(r_origin, r_explosionOrigin, maxs, mins, r_worldmodel->firstnode, MASK_VISIBILILITY);
		if (r_trace.fraction != 1.0)
			r_drawing_fbeffect = false;
	}

	//if not doing stuff, return
	if(!r_drawing_fbeffect)
		return;

	if(r_fbFxType == EXPLOSION)
	{
		vec3_t ul_3d, lr_3d;
		vec2_t ul_2d, lr_2d;
		float scale, intensity;
		
		r_fbeffectLen = 0.2f;
		
		//create a distortion wave effect at point of explosion
		glUseProgramObjectARB( g_fbprogramObj );

		glUniform1iARB (distort_uniforms.framebuffTex, 0);

		if(r_distortwave)
			GL_MBind (1, r_distortwave->texnum);
		glUniform1iARB (distort_uniforms.distortTex, 1);

		// get positions of bounds of warp area
		scale = 8.0f + (rs_realtime - r_fbeffectTime) * 78.0f / r_fbeffectLen;
		VectorMA (r_explosionOrigin, -scale, vright, ul_3d);
		VectorMA (ul_3d, scale, vup, ul_3d);
		R_TransformVectorToScreen(&r_newrefdef, ul_3d, ul_2d);
		VectorMA (r_explosionOrigin, scale, vright, lr_3d);
		VectorMA (lr_3d, -scale, vup, lr_3d);
		R_TransformVectorToScreen(&r_newrefdef, lr_3d, lr_2d);
		
		intensity = sinf (M_PI * (rs_realtime - r_fbeffectTime) / r_fbeffectLen);
		glUniform1fARB (distort_uniforms.intensity, intensity);
		
		// Note that r_framebuffer is on TMU 1 this time
		Distort_RenderQuad (ul_2d[0], lr_2d[1], lr_2d[0] - ul_2d[0], ul_2d[1] - lr_2d[1]);
		
		glUseProgramObjectARB (0);
		GL_MBind (1, 0);
	}
	else
	{
		r_fbeffectLen = 0.1f;
		R_DrawBloodEffect();
		
		//do a radial blur
		glUseProgramObjectARB( g_rblurprogramObj );

		glUniform1iARB( g_location_rsource, 0);

		glUniform3fARB( g_location_rparams, viddef.width/2.0, viddef.height/2.0, 0.25);

		Distort_RenderQuad (0, 0, viddef.width, viddef.height);

		glUseProgramObjectARB( 0 );
	}

	if(rs_realtime > r_fbeffectTime+r_fbeffectLen) 
		r_drawing_fbeffect = false; //done effect

	return;
}

void R_GLSLWaterDroplets(void)
{
	trace_t tr;
	vec3_t end;
	static float r_drTime;

	if(!(r_weather == 1) || !cl_raindist->integer)
		return;

	VectorCopy(r_newrefdef.vieworg, end);
	end[2] += 8192;

	// trace up looking for sky
	tr = CM_BoxTrace(r_newrefdef.vieworg, end, vec3_origin, vec3_origin, 0, MASK_SHOT);

	if((tr.surface->flags & SURF_SKY))
	{
		r_drTime = rs_realtime;
	}

	if(rs_realtime - r_drTime > 0.5)
		return; //been out of the rain long enough for effect to dry up
	
	//draw water droplets - set up GLSL program and uniforms
	glUseProgramObjectARB( g_dropletsprogramObj ); //this program will have two or three of the normalmap scrolling over the buffer

	glUniform1iARB( g_location_drSource, 0);

	GL_MBind (1, r_droplets->texnum);
	glUniform1iARB( g_location_drTex, 1);

	glUniform1fARB( g_location_drTime, rs_realtime);
	
	Distort_RenderQuad (0, 0, viddef.width, viddef.height);

	glUseProgramObjectARB( 0 );

	return;
}


/*
==============
R_ShadowBlend
Draws projection shadow(s)
from stenciled volume
==============
*/
image_t *r_colorbuffer;

void R_ShadowBlend(float alpha)
{
	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
		return;

	qglMatrixMode(GL_PROJECTION);
	qglPushMatrix();
	qglLoadIdentity();
	qglOrtho(0, 1, 1, 0, -99999, 99999);

	qglMatrixMode(GL_MODELVIEW);
	qglPushMatrix();
	qglLoadIdentity();
	
	GL_SetupWholeScreen2DVBO (wholescreen_blank);

	if(gl_state.fbo && gl_state.hasFBOblit && atoi(&gl_config.version_string[0]) >= 3.0) 
	{
		alpha/=1.5; //necessary because we are blending two quads

		//blit the stencil mask from main buffer
		qglBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, 0);
		qglBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, fboId[2]);

		qglBlitFramebufferEXT(0, 0, viddef.width, viddef.height, 0, 0, viddef.width, viddef.height,
			GL_STENCIL_BUFFER_BIT, GL_NEAREST);

		qglBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, 0);
		qglBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);

		//render offscreen
		qglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fboId[2]);

		qglDisable(GL_STENCIL_TEST);
		GLSTATE_DISABLE_ALPHATEST
		GLSTATE_ENABLE_BLEND
		qglDisable (GL_DEPTH_TEST);
		qglDisable (GL_TEXTURE_2D);

		qglColor4f (1,1,1, 1);

		R_DrawVarrays (GL_QUADS, 0, 4);
	}

	qglColor4f (0,0,0, alpha);

	GLSTATE_DISABLE_ALPHATEST
	GLSTATE_ENABLE_BLEND
	qglDisable (GL_DEPTH_TEST);
	qglDisable (GL_TEXTURE_2D);

	qglEnable(GL_STENCIL_TEST);
	qglStencilFunc( GL_NOTEQUAL, 0, 0xFF);
	qglStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

	R_DrawVarrays (GL_QUADS, 0, 4);

	if(gl_state.fbo && gl_state.hasFBOblit && atoi(&gl_config.version_string[0]) >= 3.0) 
	{
		qglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

		GLSTATE_ENABLE_BLEND
		qglEnable( GL_TEXTURE_2D );

		GL_BlendFunction (GL_ZERO, GL_SRC_COLOR);
		qglDisable (GL_DEPTH_TEST);
		qglDisable(GL_STENCIL_TEST);

		qglColor4f (1,1,1,1);
		
		// FIXME: textures captured from the framebuffer need to be rendered
		// upside down for some reason
		GL_SetupWholeScreen2DVBO (wholescreen_fliptextured);
		glUseProgramObjectARB( g_blurprogramObj );
		GL_MBind (0, r_colorbuffer->texnum);
		glUniform1iARB( g_location_source, 0);

		// Render two blurred versions of the original hard shadows onto the
		// framebuffer. (The second blur does NOT start with the first blur as
		// input!) This is not a proper Gaussian blur, but is designed to
		// create penumbra-like effects.
		glUniform2fARB( g_location_scale, 4.0/vid.width, 2.0/vid.height);
		R_DrawVarrays (GL_QUADS, 0, 4);
		glUniform2fARB( g_location_scale, 2.0/vid.width, 4.0/vid.height);
		R_DrawVarrays (GL_QUADS, 0, 4);

		glUseProgramObjectARB (0);
	}

	//revert settings
	qglMatrixMode(GL_PROJECTION);
	qglPopMatrix();
	qglMatrixMode(GL_MODELVIEW);
	qglPopMatrix();

	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GLSTATE_DISABLE_BLEND
	qglEnable (GL_TEXTURE_2D);
	qglEnable (GL_DEPTH_TEST);
	qglDisable(GL_STENCIL_TEST);

	qglColor4f(1,1,1,1);
	
	R_KillVArrays ();
}

/*
=================
R_FB_InitTextures
=================
*/

void R_FB_InitTextures( void )
{
	byte	*data;
	int		size;

	//init the various FBO textures
	size = viddef.width * viddef.height * 4;
	if (size < 16 * 16 * 4) // nullpic min size
		size = 16 * 16 * 4;
	data = malloc (size);

	r_framebuffer = R_Postprocess_AllocFBOTexture ("***r_framebuffer***", viddef.width, viddef.height, &distort_FBO);
	
	memset (data, 255, size);
	r_colorbuffer = GL_LoadPic ("***r_colorbuffer***", data, viddef.width, viddef.height, it_pic, 3);
	
	//init the distortion textures
	r_distortwave = GL_FindImage("gfx/distortwave.jpg", it_pic);
	if (!r_distortwave) 
		r_distortwave = GL_LoadPic ("***r_distortwave***", data, 16, 16, it_pic, 32);
	r_droplets = GL_FindImage("gfx/droplets.jpg", it_pic);
	if (!r_droplets) 
		r_droplets = GL_LoadPic ("***r_droplets***", data, 16, 16, it_pic, 32);

	//init gore/blood textures
	r_blooddroplets = GL_FindImage("gfx/blooddrops.jpg", it_pic);
	if (!r_blooddroplets) 
		r_blooddroplets = GL_LoadPic ("***r_blooddroplets***", data, 16, 16, it_pic, 32);
	r_blooddroplets_nm = GL_FindImage("gfx/blooddrops_nm.jpg", it_pic);
	if (!r_blooddroplets_nm) 
		r_blooddroplets_nm = GL_LoadPic ("***r_blooddroplets_nm***", data, 16, 16, it_pic, 32);
	
	free (data);
}

static void VehicleHud_DrawQuad_Callback (void)
{
	R_DrawVarrays(GL_QUADS, 0, 4);
}

extern int vehicle_hud;
extern cvar_t *cl_vehicle_huds;
void R_DrawVehicleHUD (void)
{	
	image_t *gl = NULL;
	rscript_t *rs = NULL;
	
	//draw image over screen
	if(!cl_vehicle_huds->integer)
		return;

	switch(vehicle_hud)
	{
		case 1:
			gl = R_RegisterPic ("hud_bomber");
			break;
		case 2:
			gl = R_RegisterPic ("hud_strafer");
			break;
		case 3:
			gl = R_RegisterPic ("hud_hover");
			break;
		case 0:
		default:
			break;
	}

	
	if (!gl)
		return;

	GL_TexEnv(GL_MODULATE);
	GLSTATE_ENABLE_BLEND
	GL_BlendFunction (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	GL_MBind (0, gl->texnum);
		
	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity ();
	qglOrtho(0, 1, 1, 0, -10, 100);
	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity ();
	
	GL_SetupWholeScreen2DVBO (wholescreen_textured);
	R_DrawVarrays (GL_QUADS, 0, 4);

	rs = gl->script;
	
	if(r_shaders->integer && rs)
	{
		RS_ReadyScript(rs);
		RS_Draw (rs, 0, vec3_origin, vec3_origin, false, rs_lightmap_off, false, VehicleHud_DrawQuad_Callback);
	}
	
	R_KillVArrays ();
	
	qglColor4f(1,1,1,1);
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GLSTATE_DISABLE_BLEND
	GL_TexEnv(GL_REPLACE);

	R_KillVArrays();
}

void R_DrawBloodEffect (void)
{	
	image_t *gl = NULL;
	
	gl = R_RegisterPic ("blood_ring");
	
	if (!gl)
		return;

	GLSTATE_ENABLE_BLEND

	GL_MBind (0, gl->texnum);
		
	qglMatrixMode( GL_PROJECTION );
    qglLoadIdentity ();
	qglOrtho(0, 1, 1, 0, -10, 100);
	qglMatrixMode( GL_MODELVIEW );
    qglLoadIdentity ();
	
	GL_SetupWholeScreen2DVBO (wholescreen_textured);
	R_DrawVarrays (GL_QUADS, 0, 4);
	R_KillVArrays ();

	GLSTATE_DISABLE_BLEND
}

extern void PART_RenderSunFlare(image_t * tex, float offset, float size, float r,
                      float g, float b, float alpha);
extern void R_DrawShadowMapWorld (qboolean forEnt, vec3_t origin);
extern void R_DrawVegetationCasters( qboolean forShadows );
extern float sun_alpha;
extern void MYgluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void R_GLSLGodRays(void)
{
	float size, screenaspect;
	vec2_t fxScreenPos;
	vec3_t origin = {0, 0, 0};

	if(!r_godrays->integer || !r_drawsun->integer)
		return;

	 if (!draw_sun || sun_alpha <= 0)
		return;

	//switch to fbo
	qglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fboId[2]); //need color buffer

	qglDisable( GL_DEPTH_TEST );
	qglDepthMask (1);

	qglClear ( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	//render sun object center
	qglMatrixMode(GL_PROJECTION);
    qglPushMatrix();
    qglLoadIdentity();
    qglOrtho(0, r_newrefdef.width, r_newrefdef.height, 0, -99999, 99999);
    qglMatrixMode(GL_MODELVIEW);
    qglPushMatrix();
    qglLoadIdentity();

	size = r_newrefdef.width * sun_size/4.0;
    PART_RenderSunFlare(sun2_object, 0, size, 1.0, 1.0, 1.0, 1.0);
    
	qglPopMatrix();
    qglMatrixMode(GL_PROJECTION);
    qglPopMatrix();
	qglLoadIdentity();

	//render occuders simple, textureless
	//need to set up proper matrix for this view!
	screenaspect = (float)r_newrefdef.width/(float)r_newrefdef.height;    

	if(r_newrefdef.fov_y < 90)
		MYgluPerspective (r_newrefdef.fov_y,  screenaspect,  4,  128000);
	else
		MYgluPerspective(r_newrefdef.fov_y, screenaspect, 4 * 74 / r_newrefdef.fov_y, 15000); 

	qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity ();

	qglRotatef (-90, 1, 0, 0);	    // put Z going up
    qglRotatef (90,  0, 0, 1);	    // put Z going up

    qglRotatef (-r_newrefdef.viewangles[2],  1, 0, 0);
	qglRotatef (-r_newrefdef.viewangles[0],  0, 1, 0);
	qglRotatef (-r_newrefdef.viewangles[1],  0, 0, 1);
	qglTranslatef (-r_newrefdef.vieworg[0],  -r_newrefdef.vieworg[1],  -r_newrefdef.vieworg[2]);

	qglCullFace(GL_FRONT);
	if (gl_cull->integer)
		qglEnable(GL_CULL_FACE);

	R_DrawShadowMapWorld(false, origin); //could tweak this to only draw surfaces that are in the sun?
	R_DrawVegetationCasters(false);
	
	qglMatrixMode(GL_PROJECTION);
    qglPushMatrix();
    qglLoadIdentity();
    qglOrtho(0, 1, 1, 0, -99999, 99999);
    qglMatrixMode(GL_MODELVIEW);
    qglPushMatrix();
    qglLoadIdentity();	

	//glsl the fbo with effect

	qglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); 

	glUseProgramObjectARB( g_godraysprogramObj );

	GL_MBind (0, r_colorbuffer->texnum);

	glUniform1iARB( g_location_sunTex, 0);

	R_TransformVectorToScreen(&r_newrefdef, sun_origin, fxScreenPos);

	fxScreenPos[0] /= viddef.width; 
	fxScreenPos[1] /= viddef.height;

	glUniform2fARB( g_location_lightPositionOnScreen, fxScreenPos[0], fxScreenPos[1]);

	glUniform1fARB( g_location_godrayScreenAspect, screenaspect);
    glUniform1fARB( g_location_sunRadius, sun_size*r_godray_intensity->value);
    
	//render quad 
	GLSTATE_ENABLE_BLEND
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE);
	
	GL_SetupWholeScreen2DVBO (wholescreen_fliptextured);
	R_DrawVarrays (GL_QUADS, 0, 4);
	R_KillVArrays ();

	GLSTATE_DISABLE_BLEND
	GL_BlendFunction (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgramObjectARB( 0 );
	
	qglPopMatrix();
    qglMatrixMode(GL_PROJECTION);
    qglPopMatrix();
    qglMatrixMode(GL_MODELVIEW);	
}

void R_GLSLPostProcess(void)
{
	R_GLSLGodRays();

	R_GLSLWaterDroplets();

	R_GLSLDistortion();
}
