#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5), 
								               float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct	surface_bumped
{
	float4	base;
	float3	normal;
	float	gloss;
	float	height;
	float	metalness;
	float	occlusion;

};

float4 tbase( float2 tc )
{
   return	s_base.Sample( smp_base, tc);
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

static const float fParallaxStartFade = 8.0f;
static const float fParallaxStopFade = 12.0f;

void UpdateTC( inout p_bumped I)
{
	if (I.position.z < fParallaxStopFade)
	{
		const float maxSamples = 25;
		const float minSamples = 5;
		const float fParallaxOffset = -0.013;

		float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
									 I.M1.y, I.M2.y, I.M3.y,
									 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);
		
		//	Calculate number of steps
		float nNumSteps = lerp( maxSamples, minSamples, eye.z );

		float	fStepSize			= 1.0 / nNumSteps;
		float2	vDelta				= eye.xy * fParallaxOffset*1.2;
		float2	vTexOffsetPerStep	= fStepSize * vDelta;

		//	Prepare start data for cycle
		float2	vTexCurrentOffset	= I.tcdh;
		float	fCurrHeight			= 0.0;
		float	fCurrentBound		= 1.0;

		for( int i=0; i<nNumSteps; ++i )
		{
			if (fCurrHeight < fCurrentBound)
			{	
				vTexCurrentOffset += vTexOffsetPerStep;		
				fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
				fCurrentBound -= fStepSize;
			}
		}

/*
		[unroll(25)]	//	Doesn't work with [loop]
		for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
		{
			vTexCurrentOffset += vTexOffsetPerStep;		
			fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
		}
*/
		//	Reconstruct previouse step's data
		vTexCurrentOffset -= vTexOffsetPerStep;
		float fPrevHeight = s_bumpX.Sample( smp_base, vTexCurrentOffset.xy ).a;

		//	Smooth tc position between current and previouse step
		float	fDelta2 = ((fCurrentBound + fStepSize) - fPrevHeight);
		float	fDelta1 = (fCurrentBound - fCurrHeight);
		float	fParallaxAmount = (fCurrentBound * fDelta2 - (fCurrentBound + fStepSize) * fDelta1 ) / ( fDelta2 - fDelta1 );
		float	fParallaxFade 	= smoothstep(fParallaxStopFade, fParallaxStartFade, I.position.z);
		float2	vParallaxOffset = vDelta * ((1- fParallaxAmount )*fParallaxFade);
		float2	vTexCoord = I.tcdh + vParallaxOffset;
	
		//	Output the result
		I.tcdh = vTexCoord;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params.xy;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC( inout p_bumped I)
{
	float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								 I.M1.y, I.M2.y, I.M3.y,
								 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);
								 
	float	height	= s_bumpX.Sample( smp_base, I.tcdh).w;	//
			//height  /= 2;
			//height  *= 0.8;
			height	= height*(parallax.x) + (parallax.y);	//
	float2	new_tc  = I.tcdh + height * normalize(eye).xy;	//

	//	Output the result
	I.tcdh	= new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC( inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

surface_bumped sload_i( p_bumped I)
{
	surface_bumped	S;
   
	UpdateTC(I);	//	All kinds of parallax are applied here.

	half4 	Nu			= s_bump.Sample( smp_base, I.tcdh );	// IN:	normal.gloss
	half4 	NuE			= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

			S.normal 	= (Nu.wzy + NuE.xyz - 1.0h) * 2;
			S.normal.xy	+= half2(-4, -8) / 255; // Texture artifacts compensation. It has impact on SSLR!

			S.base 		= tbase(I.tcdh);
			S.metalness	= Nu.x;
			S.gloss 	= 1 - Nu.y; 							// from roughness map to gloss
			S.occlusion	= NuE.z * 2;
			S.height 	= NuE.w;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
			S.normal	+= (NDetail.wzy + NDetailX.xyz - 1.0h) * 2; //	(Nu.wzyx - .5h) + (E-.5)
			S.normal.xy	+= half2(-4, -8) / 255; // Texture artifacts compensation. It has impact on SSLR!
#endif        //	USE_TDETAIL_BUMP
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
			S.base.rgb	= S.base.rgb * detail.rgb * 2;
			S.gloss		= S.gloss * detail.a * 2;
#endif

			S.normal.z 	= sqrt(saturate(1.0 - S.normal.x * S.normal.x - S.normal.y * S.normal.y));
	// S.normal	= float3(0,0,1);
			// S.metalness	= 0.0;
			// S.gloss 		= 1.0;
			// S.metalness		= 1.0;
			// S.base			= 1.0;
	return S;
}

surface_bumped sload_i( p_bumped I, float2 pixeloffset )
{
	surface_bumped	S;
   
   // apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
   I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu			= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE			= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

			S.base		= tbase(I.tcdh);				//	IN:  rgb.a
			S.normal 	= (Nu.wzy + (NuE.xyz - 1.0h)) * 2;
			S.metalness	= Nu.x;
			S.gloss 	= 1 - Nu.y; 							// from roughness map to gloss
			S.occlusion	= NuE.z * 2;
	// S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
			S.normal	+= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
#endif        //	USE_TDETAIL_BUMP

	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
			S.base.rgb	= S.base.rgb * detail.rgb * 2;
			S.gloss		= S.gloss * detail.a * 2;
#endif

	S.normal.z 	= sqrt(saturate(1.0 - S.normal.x * S.normal.x - S.normal.y * S.normal.y));
	return S;
}

surface_bumped sload ( p_bumped I)
{
      surface_bumped      S   = sload_i	(I);

#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

surface_bumped sload ( p_bumped I, float2 pixeloffset )
{
      surface_bumped      S   = sload_i	(I, pixeloffset );

#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

#endif
