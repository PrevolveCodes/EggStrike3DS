#ifndef SKY_H
#define SKY_H

#include <stdbool.h>

// Skies. There are no textures, so a sky is built from coloured triangles drawn around the camera
// every frame before the map: a colour-graded dome, stars or sun/moon with a glow, far and near
// silhouettes (hills, peaks, mesas, a factory skyline, palace domes) and drifting cloud puffs.
// Plain C with no 3DS calls: triangles go out through a callback (renderer_sky_tri on the console).

enum { RIDGE_NONE, RIDGE_HILLS, RIDGE_PEAKS, RIDGE_MESAS, RIDGE_SKYLINE, RIDGE_PALACE };

typedef struct {
	const char* name;
	float zenith[3], mid[3], horizon[3], ground[3];   // dome colours from straight up to below the horizon
	float sunAz, sunEl, sunSize;                      // radians; size is the angular radius of the disc
	float sunColor[3], glowColor[3];
	bool  moon;                                       // the 'sun' is a pale moon with craters
	float cloudLit[3], cloudShade[3];
	int   clouds;                                     // number of cloud puffs
	float cloudEl0, cloudEl1;                         // elevation range of the clouds (radians)
	float cloudSpeed;                                 // drift in radians per frame
	int   stars;
	int   farStyle;  float farColor[3];  float farHeight;     // distant silhouette (metres, tops above the floor)
	int   nearStyle; float nearColor[3]; float nearHeight;    // closer, darker silhouette
	bool  nearLights;                                 // little lit windows in the near silhouette
} SkyDef;

#define SKY_PRESETS 7
#define SKY_MAX_VERTS 5200      // upper bound on the triangles a sky can emit (checked in the host test)

const SkyDef* sky_preset(int id);                     // 0 Eggworks, 1 Sunny Side, 2 Cracked Canyon, 3 Pan Palace, 4 Fridge Factory, 5 Diner Rooftops, 6 Henhouse Hollow
unsigned int  sky_clear_rgba(const SkyDef* s);        // colour at the horizon, for clearing the screen

typedef void (*SkyEmit)(const float p[3][3], const float c[3][3]);
// Emits the whole sky centred on 'eye'. frame advances the cloud drift and star twinkle.
void sky_build(const SkyDef* s, const float eye[3], int frame, SkyEmit emit);

#endif
