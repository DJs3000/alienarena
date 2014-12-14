/*
Copyright (C) 2009-2014 COR Entertainment, LLC

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
// r_program.c

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "r_local.h"

//GLSL
PFNGLCREATEPROGRAMOBJECTARBPROC		glCreateProgramObjectARB	= NULL;
PFNGLDELETEOBJECTARBPROC			glDeleteObjectARB			= NULL;
PFNGLUSEPROGRAMOBJECTARBPROC		glUseProgramObjectARB		= NULL;
PFNGLCREATESHADEROBJECTARBPROC		glCreateShaderObjectARB		= NULL;
PFNGLSHADERSOURCEARBPROC			glShaderSourceARB			= NULL;
PFNGLCOMPILESHADERARBPROC			glCompileShaderARB			= NULL;
PFNGLGETOBJECTPARAMETERIVARBPROC	glGetObjectParameterivARB	= NULL;
PFNGLATTACHOBJECTARBPROC			glAttachObjectARB			= NULL;
PFNGLGETINFOLOGARBPROC				glGetInfoLogARB				= NULL;
PFNGLLINKPROGRAMARBPROC				glLinkProgramARB			= NULL;
PFNGLGETUNIFORMLOCATIONARBPROC		glGetUniformLocationARB		= NULL;
PFNGLUNIFORM4IARBPROC				glUniform4iARB				= NULL;
PFNGLUNIFORM3FARBPROC				glUniform3fARB				= NULL;
PFNGLUNIFORM2FARBPROC				glUniform2fARB				= NULL;
PFNGLUNIFORM1IARBPROC				glUniform1iARB				= NULL;
PFNGLUNIFORM1FARBPROC				glUniform1fARB				= NULL;
PFNGLUNIFORMMATRIX3FVARBPROC		glUniformMatrix3fvARB		= NULL;
PFNGLUNIFORMMATRIX3X4FVARBPROC		glUniformMatrix3x4fvARB		= NULL;
PFNGLVERTEXATTRIBPOINTERARBPROC	 glVertexAttribPointerARB	= NULL;
PFNGLENABLEVERTEXATTRIBARRAYARBPROC glEnableVertexAttribArrayARB = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYARBPROC glDisableVertexAttribArrayARB = NULL;
PFNGLBINDATTRIBLOCATIONARBPROC		glBindAttribLocationARB		= NULL;
PFNGLGETSHADERINFOLOGPROC			glGetShaderInfoLog			= NULL;

#define STRINGIFY(...) #__VA_ARGS__

//GLSL Programs

//BSP Surfaces
static char bsp_vertex_program[] = STRINGIFY (
	uniform mat3 tangentSpaceTransform;
	uniform vec3 Eye;
	uniform vec3 lightPosition;
	uniform vec3 staticLightPosition;
	uniform float rsTime;
	uniform int LIQUID;
	uniform int FOG;
	uniform int PARALLAX;
	uniform int DYNAMIC;
	uniform int SHINY;

	const float Eta = 0.66;
	const float FresnelPower = 5.0;
	const float F = ((1.0-Eta) * (1.0-Eta))/((1.0+Eta) * (1.0+Eta));

	varying float FresRatio;
	varying vec3 EyeDir;
	varying vec3 LightDir;
	varying vec3 StaticLightDir;
	varying vec4 sPos;
	varying float fog;

	void main( void )
	{
		sPos = gl_Vertex;

		gl_Position = ftransform();

		if(SHINY > 0)
		{
			vec3 norm = vec3(0, 0, 1); 

			vec4 neyeDir = gl_ModelViewMatrix * gl_Vertex;
			vec3 refeyeDir = neyeDir.xyz / neyeDir.w;
			refeyeDir = normalize(refeyeDir);

			FresRatio = F + (1.0-F) * pow((1.0-dot(refeyeDir,norm)),FresnelPower);
		}

		gl_FrontColor = gl_Color;

		EyeDir = tangentSpaceTransform * ( Eye - gl_Vertex.xyz );

		if (DYNAMIC > 0)
		{
			LightDir = tangentSpaceTransform * (lightPosition - gl_Vertex.xyz);
		}
		if (PARALLAX > 0)
		{
			StaticLightDir = tangentSpaceTransform * (staticLightPosition - gl_Vertex.xyz);
		}

		// pass any active texunits through
		gl_TexCoord[0] = gl_MultiTexCoord0;
		gl_TexCoord[1] = gl_MultiTexCoord1;

		//fog
		if(FOG > 0){
			fog = (gl_Position.z - gl_Fog.start) / (gl_Fog.end - gl_Fog.start);
			fog = clamp(fog, 0.0, 1.0);
		}
	}
);

// You must shadow_fudge in your shader
#define USE_SHADOWMAP_LIBRARY "/*USE_SHADOWMAP_LIBRARY*/"

static char shadowmap_header[] = STRINGIFY (
	float lookupShadow (shadowsampler_t Map, vec4 ShadowCoord);
);

static char shadowmap_library[] = 
"\n#ifndef AMD_GPU\n"
STRINGIFY (
	float lookup (vec2 offSet, sampler2DShadow Map, vec4 ShadowCoord)
	{	
		return shadow2DProj(Map, ShadowCoord + vec4(offSet.x * xPixelOffset * ShadowCoord.w, offSet.y * yPixelOffset * ShadowCoord.w, 0.05, 0.0) ).w;
	}
	
	const float internal_shadow_fudge = shadow_fudge;
)
"\n#else\n"
STRINGIFY (
	float lookup (vec2 offSet, sampler2D Map, vec4 ShadowCoord)
	{
		vec4 shadowCoordinateWdivide = (ShadowCoord + vec4(offSet.x * xPixelOffset * ShadowCoord.w, offSet.y * yPixelOffset * ShadowCoord.w, 0.0, 0.0)) / ShadowCoord.w ;
		// Used to lower moir pattern and self-shadowing
		shadowCoordinateWdivide.z += 0.0005;

		float distanceFromLight = texture2D(Map, shadowCoordinateWdivide.xy).z;

		if (ShadowCoord.w > 0.0)
			return distanceFromLight < shadowCoordinateWdivide.z ? 0.25 : 1.0 ;
		return 1.0;
	}
	
	const float internal_shadow_fudge = 0.0;
)
"\n#endif\n"
STRINGIFY (
	float lookupShadow (shadowsampler_t Map, vec4 ShadowCoord)
	{
		float shadow = 1.0;

		if(SHADOWMAP > 0) 
		{			
			if (ShadowCoord.w > 1.0)
			{
				vec2 o = mod(floor(gl_FragCoord.xy), 2.0);
				
				shadow += lookup (vec2(-1.5, 1.5) + o, Map, ShadowCoord);
				shadow += lookup (vec2( 0.5, 1.5) + o, Map, ShadowCoord);
				shadow += lookup (vec2(-1.5, -0.5) + o, Map, ShadowCoord);
				shadow += lookup (vec2( 0.5, -0.5) + o, Map, ShadowCoord);
				shadow *= 0.25 ;
			}
	
			shadow += internal_shadow_fudge; 
			if(shadow > 1.0)
				shadow = 1.0;
		}
		
		return shadow;
	}
);

static char bsp_fragment_program[] = USE_SHADOWMAP_LIBRARY STRINGIFY (
	uniform sampler2D surfTexture;
	uniform sampler2D HeightTexture;
	uniform sampler2D NormalTexture;
	uniform sampler2D lmTexture;
	uniform sampler2D liquidTexture;
	uniform sampler2D liquidNormTex;
	uniform sampler2D chromeTex;
	uniform shadowsampler_t ShadowMap;
	uniform shadowsampler_t StatShadowMap;
	uniform vec3 lightColour;
	uniform float lightCutoffSquared;
	uniform int FOG;
	uniform int PARALLAX;
	uniform int DYNAMIC;
	uniform int STATSHADOW;
	uniform int SHADOWMAP;
	uniform int LIQUID;
	uniform int SHINY;
	uniform float rsTime;
	uniform float xPixelOffset;
	uniform float yPixelOffset;

	varying float FresRatio;
	varying vec4 sPos;
	varying vec3 EyeDir;
	varying vec3 LightDir;
	varying vec3 StaticLightDir;
	varying float fog;
	
	const float shadow_fudge = 0.2; // Used by shadowmap library

	void main( void )
	{
		vec4 diffuse;
		vec4 lightmap;
		vec4 alphamask;
		vec4 bloodColor;
		float distanceSquared;
		vec3 relativeLightDirection;
		float diffuseTerm;
		vec3 halfAngleVector;
		float specularTerm;
		float swamp;
		float attenuation;
		vec4 litColour;
		vec3 varyingLightColour;
		float varyingLightCutoffSquared;
		float dynshadowval;
		float statshadowval;
		vec2 displacement;
		vec2 displacement2;
		vec2 displacement3;
		vec2 displacement4;

		varyingLightColour = lightColour;
		varyingLightCutoffSquared = lightCutoffSquared;

		vec3 relativeEyeDirection = normalize( EyeDir );

		vec3 normal = 2.0 * ( texture2D( NormalTexture, gl_TexCoord[0].xy).xyz - vec3( 0.5, 0.5, 0.5 ) );
		vec3 textureColour = texture2D( surfTexture, gl_TexCoord[0].xy ).rgb;

		lightmap = texture2D( lmTexture, gl_TexCoord[1].st );
		alphamask = texture2D( surfTexture, gl_TexCoord[0].xy );

	   	//shadows
		if(DYNAMIC > 0)
			dynshadowval = lookupShadow (ShadowMap, gl_TextureMatrix[7] * sPos);
		else
			dynshadowval = 0.0;

		if(STATSHADOW > 0)
			statshadowval = lookupShadow (StatShadowMap, gl_TextureMatrix[6] * sPos);
		else
			statshadowval = 1.0;

		bloodColor = vec4(0.0, 0.0, 0.0, 0.0);
		displacement4 = vec2(0.0, 0.0);
		if(LIQUID > 0)
		{
			vec3 noiseVec;
			vec3 noiseVec2;
			vec3 noiseVec3;

			//for liquid fx scrolling
			vec4 texco = gl_TexCoord[0];
			texco.t = texco.t - rsTime*1.0/LIQUID;

			vec4 texco2 = gl_TexCoord[0];
			texco2.t = texco2.t - rsTime*0.9/LIQUID;
			//shift the horizontal here a bit
			texco2.s = texco2.s/1.5;

			vec4 texco3 = gl_TexCoord[0];
			texco3.t = texco3.t - rsTime*0.6/LIQUID;

			vec4 Offset = texture2D( HeightTexture,gl_TexCoord[0].xy );
			Offset = Offset * 0.04 - 0.02;
			vec2 TexCoords = Offset.xy * relativeEyeDirection.xy + gl_TexCoord[0].xy;

			displacement = texco.st;

			noiseVec = normalize(texture2D(liquidNormTex, texco.st)).xyz;
			noiseVec = (noiseVec * 2.0 - 0.635) * 0.035;

			displacement2 = texco2.st;

			noiseVec2 = normalize(texture2D(liquidNormTex, displacement2.xy)).xyz;
			noiseVec2 = (noiseVec2 * 2.0 - 0.635) * 0.035;

			if(LIQUID > 2)
			{
				displacement3 = texco3.st;

				noiseVec3 = normalize(texture2D(liquidNormTex, displacement3.xy)).xyz;
				noiseVec3 = (noiseVec3 * 2.0 - 0.635) * 0.035;
			}
			else
			{
				//used for water effect only
				displacement4.x = noiseVec.x + noiseVec2.x;
				displacement4.y = noiseVec.y + noiseVec2.y;
			}

			displacement.x = texco.s + noiseVec.x + TexCoords.x;
			displacement.y = texco.t + noiseVec.y + TexCoords.y;
			displacement2.x = texco2.s + noiseVec2.x + TexCoords.x;
			displacement2.y = texco2.t + noiseVec2.y + TexCoords.y;
			displacement3.x = texco3.s + noiseVec3.x + TexCoords.x;
			displacement3.y = texco3.t + noiseVec3.y + TexCoords.y;

			if(LIQUID > 2)
			{
				vec4 diffuse1 = texture2D(liquidTexture, texco.st + displacement.xy);
				vec4 diffuse2 = texture2D(liquidTexture, texco2.st + displacement2.xy);
				vec4 diffuse3 = texture2D(liquidTexture, texco3.st + displacement3.xy);
				vec4 diffuse4 = texture2D(liquidTexture, gl_TexCoord[0].st + displacement4.xy);
				bloodColor = max(diffuse1, diffuse2);
				bloodColor = max(bloodColor, diffuse3);
			}
		}

	   if(PARALLAX > 0) 
	   {
			//do the parallax mapping
			vec4 Offset = texture2D( HeightTexture,gl_TexCoord[0].xy );
			Offset = Offset * 0.04 - 0.02;
			vec2 TexCoords = Offset.xy * relativeEyeDirection.xy + gl_TexCoord[0].xy + displacement4.xy;

			diffuse = texture2D( surfTexture, TexCoords );

			relativeLightDirection = normalize (StaticLightDir);

			diffuseTerm = dot( normal, relativeLightDirection  );

			if( diffuseTerm > 0.0 )
			{
				halfAngleVector = normalize( relativeLightDirection + relativeEyeDirection );

				specularTerm = clamp( dot( normal, halfAngleVector ), 0.0, 1.0 );
				specularTerm = pow( specularTerm, 32.0 );

				litColour = vec4 (specularTerm + ( 3.0 * diffuseTerm ) * textureColour, 6.0);
			}
			else
			{
				litColour = vec4( 0.0, 0.0, 0.0, 6.0 );
			}

			gl_FragColor = max(litColour, diffuse * 2.0);
			gl_FragColor = (gl_FragColor * lightmap) + bloodColor;
			gl_FragColor = (gl_FragColor * statshadowval);
	   }
	   else
	   {
			diffuse = texture2D(surfTexture, gl_TexCoord[0].xy);
			gl_FragColor = (diffuse * lightmap * 2.0);
			gl_FragColor = (gl_FragColor * statshadowval);
	   }

	   if(DYNAMIC > 0) 
	   {
			lightmap = texture2D(lmTexture, gl_TexCoord[1].st);

			//now do the dynamic lighting
			distanceSquared = dot( LightDir, LightDir );
			relativeLightDirection = LightDir / sqrt( distanceSquared );

			diffuseTerm = clamp( dot( normal, relativeLightDirection ), 0.0, 1.0 );
			vec3 colour = vec3( 0.0, 0.0, 0.0 );

			if( diffuseTerm > 0.0 )
			{
				halfAngleVector = normalize( relativeLightDirection + relativeEyeDirection );

				float specularTerm = clamp( dot( normal, halfAngleVector ), 0.0, 1.0 );
				specularTerm = pow( specularTerm, 32.0 );

				colour = specularTerm * vec3( 1.0, 1.0, 1.0 ) / 2.0;
			}

			attenuation = clamp( 1.0 - ( distanceSquared / varyingLightCutoffSquared ), 0.0, 1.0 );

			swamp = attenuation;
			swamp *= swamp;
			swamp *= swamp;
			swamp *= swamp;

			colour += ( ( ( 0.5 - swamp ) * diffuseTerm ) + swamp ) * textureColour * 3.0;

			vec4 dynamicColour = vec4( attenuation * colour * dynshadowval * varyingLightColour, 1.0 );
			if(PARALLAX > 0) 
			{
				dynamicColour = max(dynamicColour, gl_FragColor);
			}
			else 
			{
				dynamicColour = max(dynamicColour, vec4(textureColour, 1.0) * lightmap * 2.0);
			}
			gl_FragColor = dynamicColour;
	   }

	   gl_FragColor = mix(vec4(0.0, 0.0, 0.0, alphamask.a), gl_FragColor, alphamask.a);

	   if(SHINY > 0)
	   {
		   vec3 reflection = reflect(relativeEyeDirection, normal);
		   vec3 refraction = refract(relativeEyeDirection, normal, 0.66);

		   vec4 Tl = texture2DProj(chromeTex, vec4(reflection.xy, 1.0, 1.0) );
		   vec4 Tr = texture2DProj(chromeTex, vec4(refraction.xy, 1.0, 1.0) );

		   vec4 cubemap = mix(Tl,Tr,FresRatio);  

		   gl_FragColor = max(gl_FragColor, (cubemap * 0.05 * alphamask.a));
	   }

	   if(FOG > 0)
			gl_FragColor = mix(gl_FragColor, gl_Fog.color, fog);
	}
);


//SHADOWS
static char shadow_vertex_program[] = STRINGIFY (		
	varying vec4 ShadowCoord;

	void main( void )
	{
		ShadowCoord = gl_TextureMatrix[6] * gl_Vertex;

		gl_Position = ftransform();

		gl_Position.z -= 0.05; //eliminate z-fighting on some drivers
	}
);

static char shadow_fragment_program[] = USE_SHADOWMAP_LIBRARY STRINGIFY (
	uniform shadowsampler_t StatShadowMap;
	uniform float fadeShadow;
	uniform float xPixelOffset;
	uniform float yPixelOffset;
	
	varying vec4 ShadowCoord;
	
	const int SHADOWMAP = 1;
	const float shadow_fudge = 0.3;

	void main( void )
	{
		gl_FragColor = vec4 (1.0/fadeShadow * lookupShadow (StatShadowMap, ShadowCoord));
	}
);

// Minimap
static char minimap_vertex_program[] = STRINGIFY (
	attribute vec2 colordata;
	
	void main (void)
	{
		vec4 pos = gl_ModelViewProjectionMatrix * gl_Vertex;
		
		gl_Position.xywz = vec4 (pos.xyw, 0.0);
		
		gl_FrontColor.a = pos.z / -2.0;
		
		if (gl_FrontColor.a > 0.0)
		{
			gl_FrontColor.rgb = vec3 (0.5, 0.5 + colordata[0], 0.5);
			gl_FrontColor.a = 1.0 - gl_FrontColor.a;
		}
		else
		{
			gl_FrontColor.rgb = vec3 (0.5, colordata[0], 0);
			gl_FrontColor.a += 1.0;
		}
		
		gl_FrontColor.a *= colordata[1];
	}
);


//RSCRIPTS
static char rscript_vertex_program[] = STRINGIFY (
	uniform int envmap;
	uniform int	numblendtextures;
	uniform int FOG;
	uniform int DYNAMIC;
	uniform vec3 lightPosition;
	uniform vec3 lightAmount;
	// 0 means no lightmap, 1 means lightmap using the main texcoords, and 2
	// means lightmap using its own set of texcoords.
	uniform int lightmap; 
	
	varying float fog;
	varying vec3 orig_normal;
	varying vec3 orig_coord;
	varying vec3 LightDir, LightVec;
	varying float LightDistSquared;
	
	// This is just the maximum axis from lightAmount. It's used as a
	// mathematical shortcut to avoid some unnecessary calculations.
	varying float lightCutoffSquared; //TODO: make this uniform
	
	attribute vec4 tangent;
	
	void main ()
	{
		gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
		gl_FrontColor = gl_BackColor = gl_Color;
		
		// XXX: tri-planar projection requires the vertex normal, so don't use
		// the blendmap RScript command on BSP surfaces yet!
		if (numblendtextures != 0)
		{
			orig_normal = gl_Normal.xyz;
			orig_coord = gl_Vertex.xyz;
		}
		
		vec4 maincoord;
		
		if (envmap == 1)
		{
			maincoord.st = normalize (gl_Position.xyz).xy;
			maincoord.pq = vec2 (0.0, 1.0);
		}
		else
		{
			maincoord = gl_MultiTexCoord0;
		}
		
		gl_TexCoord[0] = gl_TextureMatrix[0] * maincoord;
		
		if (lightmap == 1)
			gl_TexCoord[1] = gl_TextureMatrix[1] * gl_MultiTexCoord0;
		else if (lightmap == 2)
			gl_TexCoord[1] = gl_TextureMatrix[1] * gl_MultiTexCoord1;
		
		//fog
		if(FOG > 0)
		{
			fog = (gl_Position.z - gl_Fog.start) / (gl_Fog.end - gl_Fog.start);
			fog = clamp(fog, 0.0, 1.0);
		}
		
		if (DYNAMIC > 0)
		{
			vec3 n = normalize (gl_NormalMatrix * gl_Normal);
			vec3 t = normalize (gl_NormalMatrix * tangent.xyz);
			vec3 b = tangent.w * normalize (gl_NormalMatrix * cross (n, t));
			
			LightVec = lightPosition - vec3 (gl_ModelViewMatrix * gl_Vertex);
			LightDistSquared = dot (LightVec, LightVec);
			
			LightDir.x = dot (LightVec, t);
			LightDir.y = dot (LightVec, b);
			LightDir.z = dot (LightVec, n);
			
			lightCutoffSquared = max (max (lightAmount[0], lightAmount[1]), lightAmount[2]);
		}
	}
);

static char rscript_fragment_program[] = STRINGIFY (
	uniform sampler2D mainTexture;
	uniform sampler2D mainTexture2;
	uniform sampler2D lightmapTexture;
	uniform sampler2D blendTexture0;
	uniform sampler2D blendTexture1;
	uniform sampler2D blendTexture2;
	uniform sampler2D blendTexture3;
	uniform sampler2D blendTexture4;
	uniform sampler2D blendTexture5;
	uniform int	numblendtextures;
	uniform int FOG;
	uniform int DYNAMIC;
	uniform vec3 lightAmount;
	uniform mat3x4 blendscales;
	// 0 means no lightmap, 1 means lightmap using the main texcoords, and 2
	// means lightmap using its own set of texcoords.
	uniform int lightmap;
	
	varying float fog;
	varying vec3 orig_normal;
	varying vec3 orig_coord;
	varying vec3 LightDir, LightVec;
	varying float LightDistSquared;
	
	// This is just the maximum axis from lightAmount. It's used as a
	// mathematical shortcut to avoid some unnecessary calculations.
	varying float lightCutoffSquared; //TODO: make this uniform
	
	// This is tri-planar projection, based on code from NVIDIA's GPU Gems
	// website. Potentially could be replaced with bi-planar projection, for
	// roughly 1/3 less sampling overhead but also less accuracy, or 
	// alternately, for even less overhead and *greater* accuracy, this fancy
	// thing: http://graphics.cs.williams.edu/papers/IndirectionI3D08/
	
	vec4 triplanar_sample (sampler2D tex, vec3 blend_weights, vec2 scale)
	{
		return vec4 (
			blend_weights[0] * texture2D (tex, orig_coord.yz * scale).rgb +
			blend_weights[1] * texture2D (tex, orig_coord.zx * scale).rgb +
			blend_weights[2] * texture2D (tex, orig_coord.xy * scale).rgb,
			1.0
		);
	}
	
	void main ()
	{
		
		vec4 mainColor = texture2D (mainTexture, gl_TexCoord[0].st);
		
		if (numblendtextures == 0)
		{
			gl_FragColor = mainColor;
		}
		else
		{
			vec4 mainColor2 = vec4 (0.0);
			if (numblendtextures > 3)
				mainColor2 = texture2D (mainTexture2, gl_TexCoord[0].st);
			
			float tmp =	mainColor.r + mainColor.g + mainColor.b +
						mainColor2.r + mainColor2.g + mainColor2.b;
			mainColor.rgb /= tmp;
			mainColor2.rgb /= tmp;
			
			vec3 blend_weights = abs (normalize (orig_normal));
			blend_weights = (blend_weights - vec3 (0.2)) * 7;
			blend_weights = max (blend_weights, 0);
			blend_weights /= (blend_weights.x + blend_weights.y + blend_weights.z);
			
			// Sigh, GLSL doesn't allow you to index arrays of samplers with
			// variables.
			gl_FragColor = vec4 (0.0);
			switch (numblendtextures)
			{
				case 6:
					if (mainColor2.b > 0.0)
						gl_FragColor += triplanar_sample (blendTexture5, blend_weights, blendscales[2].zw) * mainColor2.b;
				case 5:
					if (mainColor2.g > 0.0)
						gl_FragColor += triplanar_sample (blendTexture4, blend_weights, blendscales[2].xy) * mainColor2.g;
				case 4:
					if (mainColor2.r > 0.0)
						gl_FragColor += triplanar_sample (blendTexture3, blend_weights, blendscales[1].zw) * mainColor2.r;
				case 3:
					if (mainColor.b > 0.0)
						gl_FragColor += triplanar_sample (blendTexture2, blend_weights, blendscales[1].xy) * mainColor.b;
				case 2:
					if (mainColor.g > 0.0)
						gl_FragColor += triplanar_sample (blendTexture1, blend_weights, blendscales[0].zw) * mainColor.g;
				default:
					if (mainColor.r > 0.0)
						gl_FragColor += triplanar_sample (blendTexture0, blend_weights, blendscales[0].xy) * mainColor.r;
			}
		}
		
		gl_FragColor *= gl_Color;
		vec3 textureColor = gl_FragColor.rgb;
		
		if (lightmap != 0)
			gl_FragColor *= 2.0 * texture2D (lightmapTexture, gl_TexCoord[1].st);
		
		if (DYNAMIC > 0)
		{
			float dist2 = dot (LightVec, LightVec);
			if (dist2 < lightCutoffSquared)
			{
				// If we get this far, the fragment is within range of the 
				// dynamic light
				vec3 attenuation = clamp (vec3 (1.0) - (vec3 (dist2) / lightAmount), vec3 (0.0), vec3 (1.0));
				if (attenuation != vec3 (0.0))
				{
					// If we get this far, the fragment is facing the dynamic
					// light.
					float diffuseTerm = max (0.0, -LightDir[2] / dist2);
					vec3 swamp = attenuation * attenuation;
					swamp *= swamp;
					swamp *= swamp;
					vec3 dynamicColor = ((vec3 (0.5) - swamp) * diffuseTerm + swamp) * textureColor * 3.0 * attenuation;
					gl_FragColor.rgb = max (gl_FragColor.rgb, dynamicColor);
				}
			}
		}
		
		if (FOG > 0)
			gl_FragColor = mix(gl_FragColor, gl_Fog.color, fog);
	}
);

// Old-style per-vertex water effects
// These days commonly combined with transparency to form a mist volume effect
// Be sure to check the warptest map when you change this
// TODO: both passes into a single run of this shader, using a fragment
// shader. If each pass has an alpha of n, then the the output (incl. alpha)
// should be n * pass2 + (n - n*n) * pass1.
static char warp_vertex_program[] = STRINGIFY (
	uniform float time;
	uniform int warpvert;
	uniform int envmap; // select which pass
	
	// = 1/2 wave amplitude on each axis
	// = 1/4 wave amplitude on both axes combined
	const float wavescale = 2.0;
	
	void main ()
	{
		gl_FrontColor = gl_Color;
		
		// warping effect
		vec4 vert = gl_Vertex;
		
		if (warpvert != 0)
		{
			vert[2] += wavescale *
			(
				sin (vert[0] * 0.025 + time) * sin (vert[2] * 0.05 + time) + 
				sin (vert[1] * 0.025 + time * 2) * sin (vert[2] * 0.05 + time) - 
				2 // top of brush = top of waves
			);
		}
		
		gl_Position = gl_ModelViewProjectionMatrix * vert;
		
		if (envmap == 0) // main texture
		{
			gl_TexCoord[0] = gl_TextureMatrix[0] * vec4
			(
				gl_MultiTexCoord0.s + 4.0 * sin (gl_MultiTexCoord0.t / 8.0 + time),
				gl_MultiTexCoord0.t + 4.0 * sin (gl_MultiTexCoord0.s / 8.0 + time),
				0, 1
			);
		}
		else // env map texture (if enabled)
		{
			gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;
		}
	}
);


#define USE_MESH_ANIM_LIBRARY "/*USE_MESH_ANIM_LIBRARY*/"
static char mesh_anim_library[] = STRINGIFY (
	uniform mat3x4 bonemats[70]; // Keep this equal to SKELETAL_MAX_BONEMATS
	uniform int GPUANIM; // 0 for none, 1 for IQM skeletal, 2 for MD2 lerp
	
	// MD2 only
	uniform float lerp; // 1.0 = all new vertex, 0.0 = all old vertex

	// IQM and MD2
	attribute vec4 tangent;
	
	// IQM only

	attribute vec4 weights;
	attribute vec4 bones;
	
	// MD2 only
	attribute vec3 oldvertex;
	attribute vec3 oldnormal;
	attribute vec4 oldtangent;
	
	// anim_compute () output
	vec4 anim_vertex;
	vec3 anim_normal;
	vec3 anim_tangent;
	vec3 anim_tangent_w;
	
	// if dotangent is true, compute anim_tangent and anim_tangent_w
	// if donormal is true, compute anim_normal
	// hopefully the if statements for these booleans will get optimized out
	void anim_compute (bool dotangent, bool donormal)
	{
		if (GPUANIM == 1)
		{
			mat3x4 m = bonemats[int(bones.x)] * weights.x;
			m += bonemats[int(bones.y)] * weights.y;
			m += bonemats[int(bones.z)] * weights.z;
			m += bonemats[int(bones.w)] * weights.w;
			
			anim_vertex = vec4 (gl_Vertex * m, gl_Vertex.w);
			if (donormal)
				anim_normal = vec4 (gl_Normal, 0.0) * m;
			if (dotangent)
			{
				anim_tangent = vec4 (tangent.xyz, 0.0) * m;
				anim_tangent_w = vec4 (tangent.w, 0.0, 0.0, 0.0) * m;
			}
		}
		else if (GPUANIM == 2)
		{
			anim_vertex = mix (vec4 (oldvertex, 1), gl_Vertex, lerp);
			if (donormal)
				anim_normal = normalize (mix (oldnormal, gl_Normal, lerp));
			if (dotangent)
			{
				vec4 tmptan = mix (oldtangent, tangent, lerp);
				anim_tangent = tmptan.xyz;
				anim_tangent_w = vec3 (tmptan.w);
			}
		}
		else
		{
			anim_vertex = gl_Vertex;
			if (donormal)
				anim_normal = gl_Normal;
			if (dotangent)
			{
				anim_tangent = tangent.xyz;
				anim_tangent_w = vec3 (tangent.w);
			}
		}
	}
);

//MESHES
static char mesh_vertex_program[] = USE_MESH_ANIM_LIBRARY STRINGIFY (
	uniform vec3 lightPos;
	uniform float time;
	uniform int FOG;
	uniform int TEAM;
	uniform float useShell; // doubles as shell scale
	uniform int useCube;
	uniform vec3 baseColor;
	// For now, only applies to vertexOnly. If 0, don't do the per-vertex shading.
	uniform int doShading; 
	
	const float Eta = 0.66;
	const float FresnelPower = 5.0;
	const float F = ((1.0-Eta) * (1.0-Eta))/((1.0+Eta) * (1.0+Eta));

	varying vec3 LightDir;
	varying vec3 EyeDir;
	varying float fog;
	varying float FresRatio;
	varying vec3 vertPos, lightVec;
	varying vec3 worldNormal;

	void subScatterVS(in vec4 ecVert)
	{
		if(useShell == 0 && useCube == 0)
		{
			lightVec = lightPos - ecVert.xyz;
			vertPos = ecVert.xyz;
		}
	}

	void main()
	{
		vec3 n;
		vec3 t;
		vec3 b;
		vec4 neyeDir;
		
		anim_compute (true, true);
		
		if (useShell > 0)
			anim_vertex += normalize (vec4 (anim_normal, 0)) * useShell;
			
		gl_Position = gl_ModelViewProjectionMatrix * anim_vertex;
		subScatterVS (gl_Position);
		
		n = normalize (gl_NormalMatrix * anim_normal);
		t = normalize (gl_NormalMatrix * anim_tangent);
		b = normalize (gl_NormalMatrix * anim_tangent_w) * cross (n, t);
		
		EyeDir = vec3(gl_ModelViewMatrix * anim_vertex);
		neyeDir = gl_ModelViewMatrix * anim_vertex;
		
		worldNormal = n;

		vec3 v;
		v.x = dot(lightPos, t);
		v.y = dot(lightPos, b);
		v.z = dot(lightPos, n);
		LightDir = normalize(v);

		v.x = dot(EyeDir, t);
		v.y = dot(EyeDir, b);
		v.z = dot(EyeDir, n);
		EyeDir = normalize(v);

		if(useShell > 0)
		{
			gl_TexCoord[0] = vec4 ((anim_vertex[1]+anim_vertex[0])/40.0, anim_vertex[2]/40.0 - time, 0.0, 1.0);
		}
		else
		{
			gl_TexCoord[0] = gl_MultiTexCoord0;
			//for scrolling fx
			vec4 texco = gl_MultiTexCoord0;
			texco.s = texco.s + time*1.0;
			texco.t = texco.t + time*2.0;
			gl_TexCoord[1] = texco;
		}

		// vertexOnly is defined as const, so this branch should get optimized
		// out.
		if (vertexOnly == 1)
		{
			// We try to bias toward light instead of shadow, but then make
			// the contrast between light and shadow more pronounced.
			float lightness;
			if (doShading == 1)
			{
				lightness = max (dot (worldNormal, LightDir), 0.0) + 0.25;
				lightness = lightness * lightness + 0.25;
			}
			else
			{
				lightness = 1.0;
			}
			gl_FrontColor = gl_BackColor = vec4 (baseColor * lightness, 1.0);
			if (FOG == 1) // TODO: team colors with normalmaps disabled!
				gl_FogFragCoord = length (gl_Position);
		}
		else
		{
			if(useCube == 1)
			{
				vec3 refeyeDir = neyeDir.xyz / neyeDir.w;
				refeyeDir = normalize(refeyeDir);

				FresRatio = F + (1.0-F) * pow((1.0-dot(refeyeDir, n)), FresnelPower);
			}
		
			if(TEAM > 0)
			{
				fog = (gl_Position.z - 50.0)/1000.0;
				if(TEAM == 3)
					fog = clamp(fog, 0.0, 0.5);
				else
					fog = clamp(fog, 0.0, 0.75);
			}
			else if(FOG == 1) 
			{
				fog = (gl_Position.z - gl_Fog.start) / (gl_Fog.end - gl_Fog.start);
				fog = clamp(fog, 0.0, 0.3); //any higher and meshes disappear
			}			
		}
	}
);

static char mesh_fragment_program[] = STRINGIFY (
	uniform sampler2D baseTex;
	uniform sampler2D normalTex;
	uniform sampler2D fxTex;
	uniform sampler2D fx2Tex;
	uniform vec3 baseColor;
	uniform int GPUANIM; // 0 for none, 1 for IQM skeletal, 2 for MD2 lerp
	uniform int FOG;
	uniform int TEAM;
	uniform int useFX;
	uniform int useCube;
	uniform int useGlow;
	uniform float useShell;
	uniform int fromView;
	uniform vec3 lightPos;
	const float SpecularFactor = 0.55;
	//next group could be made uniforms if we want to control this 
	const float MaterialThickness = 2.0; //this val seems good for now
	const vec3 ExtinctionCoefficient = vec3(0.80, 0.12, 0.20); //controls subsurface value
	const float RimScalar = 10.0; //intensity of the rim effect

	varying vec3 LightDir;
	varying vec3 EyeDir;
	varying float fog;
	varying float FresRatio;
	varying vec3 vertPos, lightVec, worldNormal; 

	float halfLambert(in vec3 vect1, in vec3 vect2)
	{
		float product = dot(vect1,vect2);
		return product * 0.5 + 0.5;
	}

	float blinnPhongSpecular(in vec3 normalVec, in vec3 lightVec, in float specPower)
	{
		vec3 halfAngle = normalize(normalVec + lightVec);
		return pow(clamp(0.0,1.0,dot(normalVec,halfAngle)),specPower);
	}

	void main()
	{
		vec3 litColor;
		vec4 fx;
		vec4 glow;
		vec4 scatterCol = vec4(0.0);

		vec3 textureColour = texture2D( baseTex, gl_TexCoord[0].xy ).rgb * 1.1;
		vec3 normal = 2.0 * ( texture2D( normalTex, gl_TexCoord[0].xy).xyz - vec3( 0.5 ) );

		vec4 alphamask = texture2D( baseTex, gl_TexCoord[0].xy);
		vec4 specmask = texture2D( normalTex, gl_TexCoord[0].xy);

		if(useShell == 0 && useCube == 0 && specmask.a < 1.0)
		{
			vec4 SpecColor = vec4(baseColor, 1.0)/2.0;

			float attenuation = 2.0 * (1.0 / distance(lightPos, vertPos)); 
			vec3 wNorm = worldNormal;
			vec3 eVec = EyeDir;
			vec3 lVec = normalize(lightVec);

			vec4 dotLN = vec4(halfLambert(lVec, wNorm) * attenuation);

			vec3 indirectLightComponent = vec3(MaterialThickness * max(0.0,dot(-wNorm, lVec)));
			indirectLightComponent += MaterialThickness * halfLambert(-eVec, lVec);
			indirectLightComponent *= attenuation;
			indirectLightComponent.r *= ExtinctionCoefficient.r;
			indirectLightComponent.g *= ExtinctionCoefficient.g;
			indirectLightComponent.b *= ExtinctionCoefficient.b;

			vec3 rim = vec3(1.0 - max(0.0,dot(wNorm, eVec)));
			rim *= rim;
			rim *= max(0.0,dot(wNorm, lVec)) * SpecColor.rgb;

			scatterCol = dotLN + vec4(indirectLightComponent, 1.0);
			scatterCol.rgb += (rim * RimScalar * attenuation * scatterCol.a);
			scatterCol.rgb += vec3(blinnPhongSpecular(wNorm, lVec, SpecularFactor*2.0) * attenuation * SpecColor * scatterCol.a * 0.05);
			scatterCol.rgb *= baseColor;
			scatterCol.rgb /= (specmask.a*specmask.a);//we use the spec mask for scatter mask, presuming non-spec areas are always soft/skin
		}
		
		//moving fx texture
		if(useFX > 0)
			fx = texture2D( fxTex, gl_TexCoord[1].xy );
		else
			fx = vec4(0.0, 0.0, 0.0, 0.0);

		//glowing fx texture
		if(useGlow > 0)
			glow = texture2D(fxTex, gl_TexCoord[0].xy );

		litColor = textureColour * max(dot(normal, LightDir), 0.0);
		vec3 reflectDir = reflect(LightDir, normal);

		float spec = max(dot(EyeDir, reflectDir), 0.0);
		spec = pow(spec, 6.0);
		spec *= (SpecularFactor*specmask.a);
		litColor = min(litColor + spec, vec3(1.0));

		//keep shadows from making meshes completely black
		litColor = max(litColor, (textureColour * vec3(0.15)));

		gl_FragColor = vec4(litColor * baseColor, 1.0);		

		gl_FragColor = mix(fx, gl_FragColor + scatterCol, alphamask.a);

		if(useCube > 0 && specmask.a < 1.0)
		{			
			vec3 relEyeDir;
			
			if(fromView > 0)
				relEyeDir = normalize(LightDir);
			else
				relEyeDir = normalize(EyeDir);
					
			vec3 reflection = reflect(relEyeDir, normal);
			vec3 refraction = refract(relEyeDir, normal, 0.66);

			vec4 Tl = texture2D(fx2Tex, reflection.xy );
			vec4 Tr = texture2D(fx2Tex, refraction.xy );

			vec4 cubemap = mix(Tl,Tr,FresRatio);
			
			cubemap.rgb = max(gl_FragColor.rgb, cubemap.rgb * litColor);

			gl_FragColor = mix(cubemap, gl_FragColor, specmask.a);
		}
		
		if(useGlow > 0)
			gl_FragColor = mix(gl_FragColor, glow, glow.a);

		if(TEAM == 1)
			gl_FragColor = mix(gl_FragColor, vec4(0.3, 0.0, 0.0, 1.0), fog);
		else if(TEAM == 2)
			gl_FragColor = mix(gl_FragColor, vec4(0.0, 0.1, 0.4, 1.0), fog);
		else if(TEAM == 3)
			gl_FragColor = mix(gl_FragColor, vec4(0.0, 0.4, 0.3, 1.0), fog);
		else if(FOG > 0)
			gl_FragColor = mix(gl_FragColor, gl_Fog.color, fog);		
	}
);

//GLASS 
static char glass_vertex_program[] = USE_MESH_ANIM_LIBRARY STRINGIFY (

	uniform int FOG;

	varying vec3 r;
	varying float fog;
	varying vec3 orig_normal, normal, vert;

	void main(void)
	{
		anim_compute (false, true);

		gl_Position = gl_ModelViewProjectionMatrix * anim_vertex;

		vec3 u = normalize( vec3(gl_ModelViewMatrix * anim_vertex) ); 	
		vec3 n = normalize(gl_NormalMatrix * anim_normal); 

		r = reflect( u, n );

		normal = n;
		vert = vec3( gl_ModelViewMatrix * anim_vertex );

		orig_normal = anim_normal;

		//fog
	   if(FOG > 0) 
	   {
			fog = (gl_Position.z - gl_Fog.start) / (gl_Fog.end - gl_Fog.start);
			fog = clamp(fog, 0.0, 0.3); //any higher and meshes disappear
	   }
	   
	   // for mirroring
	   gl_TexCoord[0] = gl_MultiTexCoord0;
	}
);

static char glass_fragment_program[] = STRINGIFY (

	uniform vec3 LightPos;
	uniform vec3 left;
	uniform vec3 up;
	uniform sampler2D refTexture;
	uniform sampler2D mirTexture;
	uniform int FOG;
	uniform int type; // 1 means mirror only, 2 means glass only, 3 means both

	varying vec3 r;
	varying float fog;
	varying vec3 orig_normal, normal, vert;

	void main (void)
	{
		vec3 light_dir = normalize( LightPos - vert );  	
		vec3 eye_dir = normalize( -vert.xyz );  	
		vec3 ref = normalize( -reflect( light_dir, normal ) );  
	
		float ld = max( dot(normal, light_dir), 0.0 ); 	
		float ls = 0.75 * pow( max( dot(ref, eye_dir), 0.0 ), 0.70 ); //0.75 specular, .7 shininess

		float m = -1.0 * sqrt( r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0) );
		
		vec3 n_orig_normal = normalize (orig_normal);
		vec2 coord_offset = vec2 (dot (n_orig_normal, left), dot (n_orig_normal, up));
		vec2 glass_coord = -vec2 (r.x/m + 0.5, r.y/m + 0.5);
		vec2 mirror_coord = vec2 (-gl_TexCoord[0].s, gl_TexCoord[0].t);

		vec4 mirror_color, glass_color;
		if (type == 1)
			mirror_color = texture2D(mirTexture, mirror_coord.st);
		else if (type == 3)
			mirror_color = texture2D(mirTexture, mirror_coord.st + coord_offset.st);
		if (type != 1)
			glass_color = texture2D(refTexture, glass_coord.st + coord_offset.st/2.0);
		
		if (type == 3)
			gl_FragColor = 0.3 * glass_color + 0.3 * mirror_color * vec4 (ld + ls + 0.35);
		else if (type == 2)
			gl_FragColor = glass_color/2.0;
		else if (type == 1)
			gl_FragColor = mirror_color;

		if(FOG > 0)
			gl_FragColor = mix(gl_FragColor, gl_Fog.color, fog);
	}
);

//NO TEXTURE 
static char blankmesh_vertex_program[] = USE_MESH_ANIM_LIBRARY STRINGIFY (
	void main(void)
	{
		anim_compute (false, false);
		gl_Position = gl_ModelViewProjectionMatrix * anim_vertex;
	}
);

static char blankmesh_fragment_program[] = STRINGIFY (
	void main (void)
	{		
		gl_FragColor = vec4(1.0);
	}
);


static char water_vertex_program[] = STRINGIFY (
	uniform mat3 tangentSpaceTransform;
	uniform vec3 Eye; 
	uniform vec3 LightPos;
	uniform float time;
	uniform int	REFLECT;
	uniform int FOG;

	const float Eta = 0.66;
	const float FresnelPower = 2.5;
	const float F = ((1.0-Eta) * (1.0-Eta))/((1.0+Eta) * (1.0+Eta));

	varying float FresRatio;
	varying vec3 lightDir;
	varying vec3 eyeDir;
	varying float fog;

	void main(void)
	{
		gl_Position = ftransform();

		lightDir = tangentSpaceTransform * (LightPos - gl_Vertex.xyz);

		vec4 neyeDir = gl_ModelViewMatrix * gl_Vertex;
		vec3 refeyeDir = neyeDir.xyz / neyeDir.w;
		refeyeDir = normalize(refeyeDir);

		// The normal is always 0, 0, 1 because water is always up. Thus, 
		// dot (refeyeDir,norm) is always refeyeDir.z
		FresRatio = F + (1.0-F) * pow((1.0-refeyeDir.z),FresnelPower);

		eyeDir = tangentSpaceTransform * ( Eye - gl_Vertex.xyz );

		vec4 texco = gl_MultiTexCoord0;
		if(REFLECT > 0) 
		{
			texco.s = texco.s - LightPos.x/256.0;
			texco.t = texco.t + LightPos.y/256.0;
		}
		gl_TexCoord[0] = texco;

		texco = gl_MultiTexCoord0;
		texco.s = texco.s + time*0.05;
		texco.t = texco.t + time*0.05;
		gl_TexCoord[1] = texco;

		texco = gl_MultiTexCoord0;
		texco.s = texco.s - time*0.05;
		texco.t = texco.t - time*0.05;
		gl_TexCoord[2] = texco;

		//fog
	   if(FOG > 0)
	   {
			fog = (gl_Position.z - gl_Fog.start) / (gl_Fog.end - gl_Fog.start);
			fog = clamp(fog, 0.0, 1.0);
	  	}
	}
);

static char water_fragment_program[] = STRINGIFY (
	varying vec3 lightDir;
	varying vec3 eyeDir;
	varying float FresRatio;

	varying float fog;

	uniform sampler2D refTexture;
	uniform sampler2D normalMap;
	uniform sampler2D baseTexture;

	uniform float TRANSPARENT;
	uniform int FOG;
	
	void main (void)
	{
		vec4 refColor;

		vec3 vVec = normalize(eyeDir);

		refColor = texture2D(refTexture, gl_TexCoord[0].xy);

		vec3 bump = normalize( texture2D(normalMap, gl_TexCoord[1].xy).xyz - 0.5);
		vec3 secbump = normalize( texture2D(normalMap, gl_TexCoord[2].xy).xyz - 0.5);
		vec3 modbump = mix(secbump,bump,0.5);

		vec3 reflection = reflect(vVec,modbump);
		vec3 refraction = refract(vVec,modbump,0.66);

		vec4 Tl = texture2D(baseTexture, reflection.xy);

		vec4 Tr = texture2D(baseTexture, refraction.xy);

		vec4 cubemap = mix(Tl,Tr,FresRatio);

		gl_FragColor = mix(cubemap,refColor,0.5);

		gl_FragColor.a = TRANSPARENT;

		if(FOG > 0)
			gl_FragColor = mix(gl_FragColor, gl_Fog.color, fog);

	}
);

//FRAMEBUFFER DISTORTION EFFECTS
static char fb_vertex_program[] = STRINGIFY (
	void main( void )
	{
		gl_Position = ftransform();

		gl_TexCoord[0] = gl_MultiTexCoord0;
	}
);

static char fb_fragment_program[] = STRINGIFY (
	uniform sampler2D fbtexture;
	uniform sampler2D distortiontexture;
	uniform float intensity;

	void main(void)
	{
		vec2 noiseVec;
		vec4 displacement;
		
		noiseVec = normalize(texture2D(distortiontexture, gl_TexCoord[0].st)).xy;
		
		// It's a bit of a rigomorole to do partial screen updates. This can
		// go away as soon as the distort texture is a screen sized buffer.
		displacement = gl_TexCoord[0] + vec4 ((noiseVec * 2.0 - vec2 (0.6389, 0.6339)) * intensity, 0, 0);
		gl_FragColor = texture2D (fbtexture, (gl_TextureMatrix[0] * displacement).st);
	}
);

//COLOR SCALING SHADER - because glColor can't go outside the range 0..1
static char colorscale_fragment_program[] = STRINGIFY (
	uniform vec3		scale;
	uniform sampler2D	textureSource;
	
	void main (void)
	{
		gl_FragColor = texture2D (textureSource, gl_TexCoord[0].st) * vec4 (scale, 1.0);
	}
);

//GAUSSIAN BLUR EFFECTS
static char blur_vertex_program[] = STRINGIFY (
	varying vec2	texcoord1, texcoord2, texcoord3,
					texcoord4, texcoord5, texcoord6,
					texcoord7, texcoord8, texcoord9;
	uniform vec2	ScaleU;
	
	void main()
	{
		gl_Position = ftransform();
		
		// If we do all this math here, and let GLSL do its built-in
		// interpolation of varying variables, the math still comes out right,
		// but it's faster.
		texcoord1 = gl_MultiTexCoord0.xy-4.0*ScaleU;
		texcoord2 = gl_MultiTexCoord0.xy-3.0*ScaleU;
		texcoord3 = gl_MultiTexCoord0.xy-2.0*ScaleU;
		texcoord4 = gl_MultiTexCoord0.xy-ScaleU;
		texcoord5 = gl_MultiTexCoord0.xy;
		texcoord6 = gl_MultiTexCoord0.xy+ScaleU;
		texcoord7 = gl_MultiTexCoord0.xy+2.0*ScaleU;
		texcoord8 = gl_MultiTexCoord0.xy+3.0*ScaleU;
		texcoord9 = gl_MultiTexCoord0.xy+4.0*ScaleU;
	}
);

static char blur_fragment_program[] = STRINGIFY (
	varying vec2	texcoord1, texcoord2, texcoord3,
					texcoord4, texcoord5, texcoord6,
					texcoord7, texcoord8, texcoord9;
	uniform sampler2D textureSource;

	void main()
	{
	   vec4 sum = vec4(0.0);

	   // take nine samples
	   sum += texture2D(textureSource, texcoord1) * 0.05;
	   sum += texture2D(textureSource, texcoord2) * 0.09;
	   sum += texture2D(textureSource, texcoord3) * 0.12;
	   sum += texture2D(textureSource, texcoord4) * 0.15;
	   sum += texture2D(textureSource, texcoord5) * 0.16;
	   sum += texture2D(textureSource, texcoord6) * 0.15;
	   sum += texture2D(textureSource, texcoord7) * 0.12;
	   sum += texture2D(textureSource, texcoord8) * 0.09;
	   sum += texture2D(textureSource, texcoord9) * 0.05;

	   gl_FragColor = sum;
	}
);

//RADIAL BLUR EFFECTS // xy = radial center screen space position, z = radius attenuation, w = blur strength
static char rblur_vertex_program[] = STRINGIFY (
	void main()
	{
		gl_Position = ftransform();
		gl_TexCoord[0] =  gl_MultiTexCoord0;
	}
);

static char rblur_fragment_program[] = STRINGIFY (
	uniform sampler2D rtextureSource;
	uniform vec3 radialBlurParams;

	void main(void)
	{
		float wScissor;
		float hScissor;

		vec2 dir = vec2(radialBlurParams.x - gl_TexCoord[0].x, radialBlurParams.x - gl_TexCoord[0].x);
		float dist = sqrt(dir.x*dir.x + dir.y*dir.y); 
	  	dir = dir/dist;
		vec4 color = texture2D(rtextureSource,gl_TexCoord[0].xy); 
		vec4 sum = color;

		float strength = radialBlurParams.z;
		vec2 pDir = vec2 (0.5) - gl_TexCoord[0].st;
		float pDist = sqrt(pDir.x*pDir.x + pDir.y*pDir.y);
		clamp(pDist, 0.0, 1.0);

		 //the following ugliness is due to ATI's drivers inablity to handle a simple for-loop!
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * -0.06 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * -0.05 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * -0.03 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * -0.02 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * -0.01 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * 0.01 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * 0.02 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * 0.03 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * 0.05 * strength * pDist );
		sum += texture2D( rtextureSource, gl_TexCoord[0].xy + dir * 0.06 * strength * pDist );

		sum *= 1.0/11.0;
 
		float t = clamp (dist, 0.0, 1.0);

		vec4 final = mix (color, sum, t);

		//clamp edges to prevent artifacts
		if(gl_TexCoord[0].s > 0.008 && gl_TexCoord[0].s < 0.992 && gl_TexCoord[0].t > 0.008 && gl_TexCoord[0].t < 0.992)
			gl_FragColor = final;
		else
			gl_FragColor = color;
	}
);

//WATER DROPLETS
static char droplets_vertex_program[] = STRINGIFY (
	uniform float drTime;

	void main( void )
	{
		gl_Position = ftransform();

		 //for vertical scrolling
		 vec4 texco = gl_MultiTexCoord0;
		 texco.t = texco.t + drTime*1.0;
		 gl_TexCoord[1] = texco;

		 texco = gl_MultiTexCoord0;
		 texco.t = texco.t + drTime*0.8;
		 gl_TexCoord[2] = texco;

		gl_TexCoord[0] = gl_MultiTexCoord0;
	}
);

static char droplets_fragment_program[] = STRINGIFY (
	uniform sampler2D drSource;
	uniform sampler2D drTex;

	void main(void)
	{
		vec3 noiseVec;
		vec3 noiseVec2;
		vec2 displacement;

		displacement = gl_TexCoord[1].st;

		noiseVec = normalize(texture2D(drTex, displacement.xy)).xyz;
		noiseVec = (noiseVec * 2.0 - 0.635) * 0.035;

		displacement = gl_TexCoord[2].st;

		noiseVec2 = normalize(texture2D(drTex, displacement.xy)).xyz;
		noiseVec2 = (noiseVec2 * 2.0 - 0.635) * 0.035;

		//clamp edges to prevent artifacts
		if(gl_TexCoord[0].s > 0.1 && gl_TexCoord[0].s < 0.992)
			displacement.x = gl_TexCoord[0].s + noiseVec.x + noiseVec2.x;
		else
			displacement.x = gl_TexCoord[0].s;

		if(gl_TexCoord[0].t > 0.1 && gl_TexCoord[0].t < 0.972) 
			displacement.y = gl_TexCoord[0].t + noiseVec.y + noiseVec2.y;
		else
			displacement.y = gl_TexCoord[0].t;

		gl_FragColor = texture2D (drSource, displacement.xy);
	}
);

static char rgodrays_vertex_program[] = STRINGIFY (
	void main()
	{
		gl_TexCoord[0] =  gl_MultiTexCoord0;
		gl_Position = ftransform();
	}
);

static char rgodrays_fragment_program[] = STRINGIFY (
	uniform vec2 lightPositionOnScreen;
	uniform sampler2D sunTexture;
	uniform float aspectRatio; //width/height
	uniform float sunRadius;

	//note - these could be made uniforms to control externally
	const float exposure = 0.0034;
	const float decay = 1.0;
	const float density = 0.84;
	const float weight = 5.65;
	const int NUM_SAMPLES = 75;

	void main()
	{
		vec2 deltaTextCoord = vec2( gl_TexCoord[0].st - lightPositionOnScreen.xy );
		vec2 textCoo = gl_TexCoord[0].st;
		float adjustedLength = length (vec2 (deltaTextCoord.x*aspectRatio, deltaTextCoord.y));
		deltaTextCoord *= 1.0 /  float(NUM_SAMPLES) * density;
		float illuminationDecay = 1.0;

		int lim = NUM_SAMPLES;

		if (adjustedLength > sunRadius)
		{		
			//first simulate the part of the loop for which we won't get any
			//samples anyway
			float ratio = (adjustedLength-sunRadius)/adjustedLength;
			lim = int (float(lim)*ratio);

			textCoo -= deltaTextCoord*lim;
			illuminationDecay *= pow (decay, lim);

			//next set up the following loop so it gets the correct number of
			//samples.
			lim = NUM_SAMPLES-lim;
		}

		gl_FragColor = vec4(0.0);
		for(int i = 0; i < lim; i++)
		{
			textCoo -= deltaTextCoord;
			
			vec4 sample = texture2D(sunTexture, textCoo );

			sample *= illuminationDecay * weight;

			gl_FragColor += sample;

			illuminationDecay *= decay;
		}
		gl_FragColor *= exposure;
	}
);

// This may eventually become the basis of all particle rendering
static char vegetation_vertex_program[] = STRINGIFY (
	uniform float rsTime;
	uniform vec3 up, right;
	attribute float swayCoef, addup, addright;
	
	void main ()
	{
		// use cosine so that negative swayCoef is different from positive
		float sway = swayCoef * cos (swayCoef * rsTime);
		vec4 swayvec = vec4 (sway, sway, 0, 0);
		vec4 vertex =	gl_Vertex + swayvec +
						addup * vec4 (up, 0) + addright * vec4 (right, 0);
		gl_Position = gl_ModelViewProjectionMatrix * vertex;
		gl_TexCoord[0] = gl_MultiTexCoord0;
		gl_FrontColor = gl_BackColor = gl_Color;
		gl_FogFragCoord = length (gl_Position);
	}
);


typedef struct {
	const char	*name;
	int			index;
} vertex_attribute_t;

// add new vertex attributes here
#define NO_ATTRIBUTES			0
const vertex_attribute_t standard_attributes[] = 
{
	#define	ATTRIBUTE_TANGENT	(1<<0)
	{"tangent",		ATTR_TANGENT_IDX},
	#define	ATTRIBUTE_WEIGHTS	(1<<1)
	{"weights",		ATTR_WEIGHTS_IDX},
	#define ATTRIBUTE_BONES		(1<<2)
	{"bones",		ATTR_BONES_IDX},
	#define ATTRIBUTE_OLDVTX	(1<<3)
	{"oldvertex",	ATTR_OLDVTX_IDX},
	#define ATTRIBUTE_OLDNORM	(1<<4)
	{"oldnormal",	ATTR_OLDNORM_IDX},
	#define ATTRIBUTE_OLDTAN	(1<<5)
	{"oldtangent",	ATTR_OLDTAN_IDX},
	#define ATTRIBUTE_MINIMAP	(1<<6)
	{"colordata",	ATTR_MINIMAP_DATA_IDX},
	#define ATTRIBUTE_SWAYCOEF	(1<<7)
	{"swaycoef",	ATTR_SWAYCOEF_DATA_IDX},
	#define ATTRIBUTE_ADDUP		(1<<8)
	{"addup",		ATTR_ADDUP_DATA_IDX},
	#define ATTRIBUTE_ADDRIGHT	(1<<9)
	{"addright",	ATTR_ADDRIGHT_DATA_IDX}
};
const int num_standard_attributes = sizeof(standard_attributes)/sizeof(vertex_attribute_t);
	
void R_LoadGLSLProgram (const char *name, char *vertex, char *fragment, int attributes, GLhandleARB *program)
{
	char		str[4096];
	const char	*shaderStrings[5];
	int			nResult;
	int			i;
	
	shaderStrings[0] = "#version 120\n";
	
	*program = glCreateProgramObjectARB();
	
	if (vertex != NULL)
	{
		g_vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	
		if (strstr (vertex, USE_MESH_ANIM_LIBRARY))
			shaderStrings[1] = mesh_anim_library;
		else
			shaderStrings[1] = "";
	
		if (fragment == NULL)
			shaderStrings[2] = "const int vertexOnly = 1;";
		else
			shaderStrings[2] = "const int vertexOnly = 0;";
	
		shaderStrings[3] = vertex;
		glShaderSourceARB (g_vertexShader, 4, shaderStrings, NULL);
		glCompileShaderARB (g_vertexShader);
		glGetObjectParameterivARB (g_vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &nResult);

		if (nResult)
		{
			glAttachObjectARB (*program, g_vertexShader);
		}
		else
		{
			Com_Printf ("...%s Vertex Shader Compile Error\n", name);
			if (glGetShaderInfoLog != NULL)
			{
				glGetShaderInfoLog (g_vertexShader, sizeof(str), NULL, str);
				Com_Printf ("%s\n", str);
			}
		}
	}
	
	if (fragment != NULL)
	{
		g_fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
		
		if (gl_state.ati)
			shaderStrings[1] = "#define AMD_GPU\n#define shadowsampler_t sampler2D\n";
		else
			shaderStrings[1] = "#define shadowsampler_t sampler2DShadow\n";
		
		if (strstr (fragment, USE_SHADOWMAP_LIBRARY))
		{
			shaderStrings[2] = shadowmap_header;
			shaderStrings[4] = shadowmap_library;
		}
		else
		{
			shaderStrings[2] = shaderStrings[4] = "";
		}
		
		shaderStrings[3] = fragment;
	
		glShaderSourceARB (g_fragmentShader, 5, shaderStrings, NULL);
		glCompileShaderARB (g_fragmentShader);
		glGetObjectParameterivARB( g_fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &nResult );

		if (nResult)
		{
			glAttachObjectARB (*program, g_fragmentShader);
		}
		else
		{
			Com_Printf ("...%s Fragment Shader Compile Error\n", name);
			if (glGetShaderInfoLog != NULL)
			{
				glGetShaderInfoLog (g_fragmentShader, sizeof(str), NULL, str);
				Com_Printf ("%s\n", str);
			}
		}
	}
	
	for (i = 0; i < num_standard_attributes; i++)
	{
		if (attributes & (1<<i))
			glBindAttribLocationARB(*program, standard_attributes[i].index, standard_attributes[i].name);
	}

	glLinkProgramARB( *program );
	glGetObjectParameterivARB( *program, GL_OBJECT_LINK_STATUS_ARB, &nResult );

	glGetInfoLogARB( *program, sizeof(str), NULL, str );
	if( !nResult )
		Com_Printf("...%s Shader Linking Error\n%s\n", name, str);
}

static void get_dlight_uniform_locations (GLhandleARB programObj, dlight_uniform_location_t *out)
{
	out->enableDynamic = glGetUniformLocationARB (programObj, "DYNAMIC");
	out->lightAmountSquared = glGetUniformLocationARB (programObj, "lightAmount");
	out->lightPosition = glGetUniformLocationARB (programObj, "lightPosition");
}

static void get_mesh_anim_uniform_locations (GLhandleARB programObj, mesh_anim_uniform_location_t *out)
{
	out->useGPUanim = glGetUniformLocationARB (programObj, "GPUANIM");
	out->outframe = glGetUniformLocationARB (programObj, "bonemats");
	out->lerp = glGetUniformLocationARB (programObj, "lerp");
}

static void get_mesh_uniform_locations (GLhandleARB programObj, mesh_uniform_location_t *out)
{
	get_mesh_anim_uniform_locations (programObj, &out->anim_uniforms);
	out->lightPosition = glGetUniformLocationARB (programObj, "lightPos");
	out->baseTex = glGetUniformLocationARB (programObj, "baseTex");
	out->normTex = glGetUniformLocationARB (programObj, "normalTex");
	out->fxTex = glGetUniformLocationARB (programObj, "fxTex");
	out->fx2Tex = glGetUniformLocationARB (programObj, "fx2Tex");
	out->color = glGetUniformLocationARB (programObj, "baseColor");
	out->time = glGetUniformLocationARB (programObj, "time");
	out->fog = glGetUniformLocationARB (programObj, "FOG");
	out->useFX = glGetUniformLocationARB (programObj, "useFX");
	out->useGlow = glGetUniformLocationARB (programObj, "useGlow");
	out->useShell = glGetUniformLocationARB (programObj, "useShell");
	out->useCube = glGetUniformLocationARB (programObj, "useCube");
	out->fromView = glGetUniformLocationARB (programObj, "fromView");
	out->doShading = glGetUniformLocationARB (programObj, "doShading");
	out->team = glGetUniformLocationARB (programObj, "TEAM");
}

void R_LoadGLSLPrograms(void)
{
	int i;
	
	//load glsl (to do - move to own file)
	if (strstr(gl_config.extensions_string,  "GL_ARB_shader_objects" ))
	{
		glCreateProgramObjectARB  = (PFNGLCREATEPROGRAMOBJECTARBPROC)qwglGetProcAddress("glCreateProgramObjectARB");
		glDeleteObjectARB		 = (PFNGLDELETEOBJECTARBPROC)qwglGetProcAddress("glDeleteObjectARB");
		glUseProgramObjectARB	 = (PFNGLUSEPROGRAMOBJECTARBPROC)qwglGetProcAddress("glUseProgramObjectARB");
		glCreateShaderObjectARB   = (PFNGLCREATESHADEROBJECTARBPROC)qwglGetProcAddress("glCreateShaderObjectARB");
		glShaderSourceARB		 = (PFNGLSHADERSOURCEARBPROC)qwglGetProcAddress("glShaderSourceARB");
		glCompileShaderARB		= (PFNGLCOMPILESHADERARBPROC)qwglGetProcAddress("glCompileShaderARB");
		glGetObjectParameterivARB = (PFNGLGETOBJECTPARAMETERIVARBPROC)qwglGetProcAddress("glGetObjectParameterivARB");
		glAttachObjectARB		 = (PFNGLATTACHOBJECTARBPROC)qwglGetProcAddress("glAttachObjectARB");
		glGetInfoLogARB		   = (PFNGLGETINFOLOGARBPROC)qwglGetProcAddress("glGetInfoLogARB");
		glLinkProgramARB		  = (PFNGLLINKPROGRAMARBPROC)qwglGetProcAddress("glLinkProgramARB");
		glGetUniformLocationARB   = (PFNGLGETUNIFORMLOCATIONARBPROC)qwglGetProcAddress("glGetUniformLocationARB");
		glUniform4iARB			= (PFNGLUNIFORM4IARBPROC)qwglGetProcAddress("glUniform4iARB");
		glUniform3fARB			= (PFNGLUNIFORM3FARBPROC)qwglGetProcAddress("glUniform3fARB");
		glUniform2fARB			= (PFNGLUNIFORM2FARBPROC)qwglGetProcAddress("glUniform2fARB");
		glUniform1iARB			= (PFNGLUNIFORM1IARBPROC)qwglGetProcAddress("glUniform1iARB");
		glUniform1fARB		  = (PFNGLUNIFORM1FARBPROC)qwglGetProcAddress("glUniform1fARB");
		glUniformMatrix3fvARB	  = (PFNGLUNIFORMMATRIX3FVARBPROC)qwglGetProcAddress("glUniformMatrix3fvARB");
		glUniformMatrix3x4fvARB	  = (PFNGLUNIFORMMATRIX3X4FVARBPROC)qwglGetProcAddress("glUniformMatrix3x4fv");
		glVertexAttribPointerARB = (PFNGLVERTEXATTRIBPOINTERARBPROC)qwglGetProcAddress("glVertexAttribPointerARB");
		glEnableVertexAttribArrayARB = (PFNGLENABLEVERTEXATTRIBARRAYARBPROC)qwglGetProcAddress("glEnableVertexAttribArrayARB");
		glDisableVertexAttribArrayARB = (PFNGLDISABLEVERTEXATTRIBARRAYARBPROC)qwglGetProcAddress("glDisableVertexAttribArrayARB");
		glBindAttribLocationARB = (PFNGLBINDATTRIBLOCATIONARBPROC)qwglGetProcAddress("glBindAttribLocationARB");
		glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)qwglGetProcAddress("glGetShaderInfoLog");

		if( !glCreateProgramObjectARB || !glDeleteObjectARB || !glUseProgramObjectARB ||
			!glCreateShaderObjectARB || !glCreateShaderObjectARB || !glCompileShaderARB ||
			!glGetObjectParameterivARB || !glAttachObjectARB || !glGetInfoLogARB ||
			!glLinkProgramARB || !glGetUniformLocationARB || !glUniform3fARB ||
				!glUniform4iARB || !glUniform1iARB || !glUniform1fARB ||
				!glUniformMatrix3fvARB || !glUniformMatrix3x4fvARB ||
				!glVertexAttribPointerARB || !glEnableVertexAttribArrayARB ||
				!glBindAttribLocationARB)
		{
			Com_Error (ERR_FATAL, "...One or more GL_ARB_shader_objects functions were not found\n");
		}
	}
	else
	{
		Com_Error (ERR_FATAL, "...One or more GL_ARB_shader_objects functions were not found\n");
	}

	gl_dynamic = Cvar_Get ("gl_dynamic", "1", CVAR_ARCHIVE);

	//standard bsp surfaces
	R_LoadGLSLProgram ("BSP", (char*)bsp_vertex_program, (char*)bsp_fragment_program, NO_ATTRIBUTES, &g_programObj);

	// Locate some parameters by name so we can set them later...
	g_location_surfTexture = glGetUniformLocationARB( g_programObj, "surfTexture" );
	g_location_eyePos = glGetUniformLocationARB( g_programObj, "Eye" );
	g_tangentSpaceTransform = glGetUniformLocationARB( g_programObj, "tangentSpaceTransform");
	g_location_heightTexture = glGetUniformLocationARB( g_programObj, "HeightTexture" );
	g_location_lmTexture = glGetUniformLocationARB( g_programObj, "lmTexture" );
	g_location_normalTexture = glGetUniformLocationARB( g_programObj, "NormalTexture" );
	g_location_bspShadowmapTexture = glGetUniformLocationARB( g_programObj, "ShadowMap" );
	g_location_bspShadowmapTexture2 = glGetUniformLocationARB( g_programObj, "StatShadowMap" );
	g_location_fog = glGetUniformLocationARB( g_programObj, "FOG" );
	g_location_parallax = glGetUniformLocationARB( g_programObj, "PARALLAX" );
	g_location_dynamic = glGetUniformLocationARB( g_programObj, "DYNAMIC" );
	g_location_shadowmap = glGetUniformLocationARB( g_programObj, "SHADOWMAP" );
	g_Location_statshadow = glGetUniformLocationARB( g_programObj, "STATSHADOW" );
	g_location_xOffs = glGetUniformLocationARB( g_programObj, "xPixelOffset" );
	g_location_yOffs = glGetUniformLocationARB( g_programObj, "yPixelOffset" );
	g_location_lightPosition = glGetUniformLocationARB( g_programObj, "lightPosition" );
	g_location_staticLightPosition = glGetUniformLocationARB( g_programObj, "staticLightPosition" );
	g_location_lightColour = glGetUniformLocationARB( g_programObj, "lightColour" );
	g_location_lightCutoffSquared = glGetUniformLocationARB( g_programObj, "lightCutoffSquared" );
	g_location_liquid = glGetUniformLocationARB( g_programObj, "LIQUID" );
	g_location_shiny = glGetUniformLocationARB( g_programObj, "SHINY" );
	g_location_rsTime = glGetUniformLocationARB( g_programObj, "rsTime" );
	g_location_liquidTexture = glGetUniformLocationARB( g_programObj, "liquidTexture" );
	g_location_liquidNormTex = glGetUniformLocationARB( g_programObj, "liquidNormTex" );
	g_location_chromeTex = glGetUniformLocationARB( g_programObj, "chromeTex" );

	//shadowed white bsp surfaces
	R_LoadGLSLProgram ("Shadow", (char*)shadow_vertex_program, (char*)shadow_fragment_program, NO_ATTRIBUTES, &g_shadowprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_entShadow = glGetUniformLocationARB( g_shadowprogramObj, "StatShadowMap" );
	g_location_fadeShadow = glGetUniformLocationARB( g_shadowprogramObj, "fadeShadow" );
	g_location_xOffset = glGetUniformLocationARB( g_shadowprogramObj, "xPixelOffset" );
	g_location_yOffset = glGetUniformLocationARB( g_shadowprogramObj, "yPixelOffset" );
	
	// Old-style per-vertex water effects
	R_LoadGLSLProgram ("Warp", (char*)warp_vertex_program, NULL, NO_ATTRIBUTES, &g_warpprogramObj);
	
	warp_uniforms.time = glGetUniformLocationARB (g_warpprogramObj, "time");
	warp_uniforms.warpvert = glGetUniformLocationARB (g_warpprogramObj, "warpvert");
	warp_uniforms.envmap = glGetUniformLocationARB (g_warpprogramObj, "envmap");
	
	// Minimaps
	R_LoadGLSLProgram ("Minimap", (char*)minimap_vertex_program, NULL, ATTRIBUTE_MINIMAP, &g_minimapprogramObj);
	
	//rscript surfaces
	R_LoadGLSLProgram ("RScript", (char*)rscript_vertex_program, (char*)rscript_fragment_program, ATTRIBUTE_TANGENT, &g_rscriptprogramObj);
	
	// Locate some parameters by name so we can set them later...
	get_dlight_uniform_locations (g_rscriptprogramObj, &rscript_uniforms.dlight_uniforms);
	rscript_uniforms.envmap = glGetUniformLocationARB (g_rscriptprogramObj, "envmap");
	rscript_uniforms.numblendtextures = glGetUniformLocationARB (g_rscriptprogramObj, "numblendtextures");
	rscript_uniforms.lightmap = glGetUniformLocationARB (g_rscriptprogramObj, "lightmap");
	rscript_uniforms.fog = glGetUniformLocationARB (g_rscriptprogramObj, "FOG");
	rscript_uniforms.mainTexture = glGetUniformLocationARB (g_rscriptprogramObj, "mainTexture");
	rscript_uniforms.mainTexture2 = glGetUniformLocationARB (g_rscriptprogramObj, "mainTexture2");
	rscript_uniforms.lightmapTexture = glGetUniformLocationARB (g_rscriptprogramObj, "lightmapTexture");
	rscript_uniforms.blendscales = glGetUniformLocationARB (g_rscriptprogramObj, "blendscales");
	
	for (i = 0; i < 6; i++)
	{
		char uniformname[] = "blendTexture.";
		
		assert (i < 10); // We only have space for one digit.
		uniformname[12] = '0'+i;
		rscript_uniforms.blendTexture[i] = glGetUniformLocationARB (g_rscriptprogramObj, uniformname);
	}

	// per-pixel warp(water) bsp surfaces
	R_LoadGLSLProgram ("Water", (char*)water_vertex_program, (char*)water_fragment_program, NO_ATTRIBUTES, &g_waterprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_baseTexture = glGetUniformLocationARB( g_waterprogramObj, "baseTexture" );
	g_location_normTexture = glGetUniformLocationARB( g_waterprogramObj, "normalMap" );
	g_location_refTexture = glGetUniformLocationARB( g_waterprogramObj, "refTexture" );
	g_location_waterEyePos = glGetUniformLocationARB( g_waterprogramObj, "Eye" );
	g_location_tangentSpaceTransform = glGetUniformLocationARB( g_waterprogramObj, "tangentSpaceTransform");
	g_location_time = glGetUniformLocationARB( g_waterprogramObj, "time" );
	g_location_lightPos = glGetUniformLocationARB( g_waterprogramObj, "LightPos" );
	g_location_reflect = glGetUniformLocationARB( g_waterprogramObj, "REFLECT" );
	g_location_trans = glGetUniformLocationARB( g_waterprogramObj, "TRANSPARENT" );
	g_location_fogamount = glGetUniformLocationARB( g_waterprogramObj, "FOG" );

	//meshes
	R_LoadGLSLProgram ("Mesh", (char*)mesh_vertex_program, (char*)mesh_fragment_program, ATTRIBUTE_TANGENT|ATTRIBUTE_WEIGHTS|ATTRIBUTE_BONES|ATTRIBUTE_OLDVTX|ATTRIBUTE_OLDNORM|ATTRIBUTE_OLDTAN, &g_meshprogramObj);
	
	get_mesh_uniform_locations (g_meshprogramObj, &mesh_uniforms);

	//vertex-only meshes
	R_LoadGLSLProgram ("VertexOnly_Mesh", (char*)mesh_vertex_program, NULL, ATTRIBUTE_TANGENT|ATTRIBUTE_WEIGHTS|ATTRIBUTE_BONES|ATTRIBUTE_OLDVTX|ATTRIBUTE_OLDNORM|ATTRIBUTE_OLDTAN, &g_vertexonlymeshprogramObj);
	
	get_mesh_uniform_locations (g_vertexonlymeshprogramObj, &mesh_vertexonly_uniforms);
	
	//Glass
	R_LoadGLSLProgram ("Glass", (char*)glass_vertex_program, (char*)glass_fragment_program, ATTRIBUTE_WEIGHTS|ATTRIBUTE_BONES|ATTRIBUTE_OLDVTX|ATTRIBUTE_OLDNORM, &g_glassprogramObj);

	// Locate some parameters by name so we can set them later...
	get_mesh_anim_uniform_locations (g_glassprogramObj, &glass_uniforms.anim_uniforms);
	glass_uniforms.fog = glGetUniformLocationARB (g_glassprogramObj, "FOG");
	glass_uniforms.type = glGetUniformLocationARB (g_glassprogramObj, "type");
	glass_uniforms.left = glGetUniformLocationARB (g_glassprogramObj, "left");
	glass_uniforms.up = glGetUniformLocationARB (g_glassprogramObj, "up");
	glass_uniforms.lightPos = glGetUniformLocationARB (g_glassprogramObj, "LightPos");
	glass_uniforms.mirTexture = glGetUniformLocationARB (g_glassprogramObj, "mirTexture");
	glass_uniforms.refTexture = glGetUniformLocationARB (g_glassprogramObj, "refTexture");

	//Blank mesh (for shadowmapping efficiently)
	R_LoadGLSLProgram ("Blankmesh", (char*)blankmesh_vertex_program, (char*)blankmesh_fragment_program, ATTRIBUTE_WEIGHTS|ATTRIBUTE_BONES|ATTRIBUTE_OLDVTX, &g_blankmeshprogramObj);

	// Locate some parameters by name so we can set them later...
	get_mesh_anim_uniform_locations (g_blankmeshprogramObj, &blankmesh_uniforms);
	
	//fullscreen distortion effects
	R_LoadGLSLProgram ("Framebuffer Distort", (char*)fb_vertex_program, (char*)fb_fragment_program, NO_ATTRIBUTES, &g_fbprogramObj);

	// Locate some parameters by name so we can set them later...
	distort_uniforms.framebuffTex = glGetUniformLocationARB (g_fbprogramObj, "fbtexture");
	distort_uniforms.distortTex = glGetUniformLocationARB (g_fbprogramObj, "distortiontexture");
	distort_uniforms.intensity = glGetUniformLocationARB (g_fbprogramObj, "intensity");

	//gaussian blur
	R_LoadGLSLProgram ("Framebuffer Blur", (char*)blur_vertex_program, (char*)blur_fragment_program, NO_ATTRIBUTES, &g_blurprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_scale = glGetUniformLocationARB( g_blurprogramObj, "ScaleU" );
	g_location_source = glGetUniformLocationARB( g_blurprogramObj, "textureSource");
	
	// Color scaling
	R_LoadGLSLProgram ("Color Scaling", NULL, (char*)colorscale_fragment_program, NO_ATTRIBUTES, &g_colorscaleprogramObj);
	colorscale_uniforms.scale = glGetUniformLocationARB (g_colorscaleprogramObj, "scale");
	colorscale_uniforms.source = glGetUniformLocationARB (g_colorscaleprogramObj, "textureSource");

	//radial blur
	R_LoadGLSLProgram ("Framebuffer Radial Blur", (char*)rblur_vertex_program, (char*)rblur_fragment_program, NO_ATTRIBUTES, &g_rblurprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_rsource = glGetUniformLocationARB( g_rblurprogramObj, "rtextureSource");
	g_location_rparams = glGetUniformLocationARB( g_rblurprogramObj, "radialBlurParams");

	//water droplets
	R_LoadGLSLProgram ("Framebuffer Droplets", (char*)droplets_vertex_program, (char*)droplets_fragment_program, NO_ATTRIBUTES, &g_dropletsprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_drSource = glGetUniformLocationARB( g_dropletsprogramObj, "drSource" );
	g_location_drTex = glGetUniformLocationARB( g_dropletsprogramObj, "drTex");
	g_location_drTime = glGetUniformLocationARB( g_dropletsprogramObj, "drTime" );

	//god rays
	R_LoadGLSLProgram ("God Rays", (char*)rgodrays_vertex_program, (char*)rgodrays_fragment_program, NO_ATTRIBUTES, &g_godraysprogramObj);

	// Locate some parameters by name so we can set them later...
	g_location_lightPositionOnScreen = glGetUniformLocationARB( g_godraysprogramObj, "lightPositionOnScreen" );
	g_location_sunTex = glGetUniformLocationARB( g_godraysprogramObj, "sunTexture");
	g_location_godrayScreenAspect = glGetUniformLocationARB( g_godraysprogramObj, "aspectRatio");
	g_location_sunRadius = glGetUniformLocationARB( g_godraysprogramObj, "sunRadius");
	
	//vegetation
	R_LoadGLSLProgram ("Vegetation", (char*)vegetation_vertex_program, NULL, ATTRIBUTE_SWAYCOEF|ATTRIBUTE_ADDUP|ATTRIBUTE_ADDRIGHT, &g_vegetationprogramObj);
	
	vegetation_uniforms.rsTime = glGetUniformLocationARB (g_vegetationprogramObj, "rsTime");
	vegetation_uniforms.up = glGetUniformLocationARB (g_vegetationprogramObj, "up");
	vegetation_uniforms.right = glGetUniformLocationARB (g_vegetationprogramObj, "right");

}
