#include "renderer.h"
#include "camera.h"
#include "config.h"
#include "xform.h"
#include "surface.h"
#include "sky.h"
#include "player.h"

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <string.h>
#include "vshader_shbin.h"

#define MAX_SKY_VERTS SKY_MAX_VERTS
#define MAX_STATIC_VERTS 46000     // map surfaces (patterned faces, see surface.c)
#define MAX_WORLD_VERTS  28000
#define MAX_VIEW_VERTS   6000
#define MAX_OUTLINE_VERTS 8000     // team outlines (a slightly bigger egg drawn behind each bot egg)
#define SKY_BASE         (MAX_STATIC_VERTS + MAX_WORLD_VERTS + MAX_VIEW_VERTS + MAX_OUTLINE_VERTS)
#define TOTAL_VERTS      (SKY_BASE + MAX_SKY_VERTS)
#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

typedef struct { float x, y, z; float r, g, b; } Vertex;

static C3D_RenderTarget* topTarget;
static C3D_RenderTarget* bottomTarget;
static DVLB_s* vshaderDvlb;
static shaderProgram_s program;
static int uLocProjection;
static C3D_Mtx baseProjection;

// One linear buffer: [static map][dynamic world][dynamic view]
static Vertex* vbo;
static int staticCount;

// Dynamic geometry is staged in normal RAM and copied after the frame starts,
// so the GPU is never reading the region while the CPU writes it.
static Vertex stageWorld[MAX_WORLD_VERTS];
static Vertex stageView[MAX_VIEW_VERTS];
static Vertex stageOutline[MAX_OUTLINE_VERTS];
static Vertex stageSky[MAX_SKY_VERTS];
static int worldCount, viewCount, outlineCount, skyCount;
static const SkyDef* skyDef;
static unsigned int clearColor = 0x68B0D8FF;
static int skyFrame;
static Xform viewXf;
static bool viewXfOn;
static Xform worldXf;
static bool worldXfOn;

static void pushQuad(Vertex* arr, int* n, int cap,
                     const float a[3], const float b[3], const float c[3], const float d[3],
                     float r, float g, float bl)
{
	if (*n + 6 > cap) return;
	const float* q[6] = { a, b, c, a, c, d };
	for (int i = 0; i < 6; i++)
		arr[(*n)++] = (Vertex){ q[i][0], q[i][1], q[i][2], r, g, bl };
}

// Corner order: 0 p000, 1 p100, 2 p110, 3 p010, 4 p001, 5 p101, 6 p111, 7 p011
static void pushBoxCorners(Vertex* arr, int* n, int cap, const float c[8][3],
                           float r, float g, float b)
{
	pushQuad(arr, n, cap, c[3], c[2], c[6], c[7], r,         g,         b);         // top
	pushQuad(arr, n, cap, c[0], c[1], c[5], c[4], r * 0.4f,  g * 0.4f,  b * 0.4f);  // bottom
	pushQuad(arr, n, cap, c[4], c[5], c[6], c[7], r * 0.8f,  g * 0.8f,  b * 0.8f);  // +z
	pushQuad(arr, n, cap, c[0], c[1], c[2], c[3], r * 0.8f,  g * 0.8f,  b * 0.8f);  // -z
	pushQuad(arr, n, cap, c[1], c[5], c[6], c[2], r * 0.65f, g * 0.65f, b * 0.65f); // +x
	pushQuad(arr, n, cap, c[0], c[4], c[7], c[3], r * 0.65f, g * 0.65f, b * 0.65f); // -x
}

static void pushBox(Vertex* arr, int* n, int cap,
                    float x0, float y0, float z0, float x1, float y1, float z1,
                    float r, float g, float b)
{
	float c[8][3] = {
		{ x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
		{ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }
	};
	pushBoxCorners(arr, n, cap, c, r, g, b);
}

static void pushBoxXf(Vertex* arr, int* n, int cap, const Xform* xf,
                      float x0, float y0, float z0, float x1, float y1, float z1,
                      float r, float g, float b)
{
	float local[8][3] = {
		{ x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
		{ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 }
	};
	float c[8][3];
	for (int i = 0; i < 8; i++) xform_point(xf, local[i], c[i]);
	pushBoxCorners(arr, n, cap, c, r, g, b);
}

static C3D_TexEnv savedEnv[6];

// citro2d keeps its own texenv/blend/depth setup. Remember it before the 3D pass
// and put it back afterwards, otherwise 2D text loses its transparency.
static void saveC2dState(void)
{
	for (int i = 0; i < 6; i++) savedEnv[i] = *C3D_GetTexEnv(i);
}

static void restoreC2dState(void)
{
	for (int i = 0; i < 6; i++) C3D_SetTexEnv(i, &savedEnv[i]);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
	               GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

static void apply3dState(void)
{
	// citro2d changes program, vertex layout, texenv, depth and blend state; restore ours.
	C3D_BindProgram(&program);

	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);

	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo, sizeof(Vertex), 2, 0x10);

	for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	C3D_CullFace(GPU_CULL_NONE);
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	C3D_AlphaTest(false, GPU_ALWAYS, 0);
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

bool renderer_init(void)
{
	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) return false;

	topTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	if (!topTarget) return false;
	C3D_RenderTargetSetOutput(topTarget, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (!bottomTarget) return false;

	vshaderDvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshaderDvlb->DVLE[0]);
	uLocProjection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

	vbo = (Vertex*)linearAlloc(sizeof(Vertex) * TOTAL_VERTS);
	if (!vbo) return false;

	Mtx_PerspTilt(&baseProjection, C3D_AngleFromDegrees(BASE_FOV), C3D_AspectRatioTop, 0.2f, 200.0f, false);
	return true;
}

void renderer_load_map(const Map* m)
{
	// surface.c cuts every visible face into small coloured patches (bricks, planks, tiles...)
	// and lowers the detail itself if the map would not fit in the buffer.
	_Static_assert(sizeof(Vertex) == sizeof(SurfVertex), "vertex layouts must match");
	staticCount = surface_build_map(m, (SurfVertex*)vbo, MAX_STATIC_VERTS, NULL);
	GSPGPU_FlushDataCache(vbo, sizeof(Vertex) * staticCount);
	skyDef = sky_preset(m->sky);
	clearColor = sky_clear_rgba(skyDef);
}

static void skyEmit(const float p[3][3], const float c[3][3])
{
	if (skyCount + 3 > MAX_SKY_VERTS) return;
	for (int i = 0; i < 3; i++) stageSky[skyCount++] = (Vertex){ p[i][0], p[i][1], p[i][2], c[i][0], c[i][1], c[i][2] };
}

void renderer_dyn_begin(void)
{
	worldCount = 0;
	viewCount = 0;
	outlineCount = 0;
	viewXfOn = false;
	worldXfOn = false;
}

void renderer_world_box(float cx, float cy, float cz, float hx, float hy, float hz,
                        float r, float g, float b)
{
	if (worldXfOn)
		pushBoxXf(stageWorld, &worldCount, MAX_WORLD_VERTS, &worldXf,
		          cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz, r, g, b);
	else
		pushBox(stageWorld, &worldCount, MAX_WORLD_VERTS,
		        cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz, r, g, b);
}

void renderer_world_xform(float x, float y, float z, float pitch, float yaw, float roll)
{
	xform_set(&worldXf, x, y, z, pitch, yaw, roll);
	worldXfOn = true;
}

void renderer_world_xform_clear(void)
{
	worldXfOn = false;
}

static void pushTri(Vertex* arr, int* n, int cap, const Xform* xf, bool xfOn,
                    const float p[3][3], const float c[3][3])
{
	if (*n + 3 > cap) return;
	for (int i = 0; i < 3; i++) {
		float q[3] = { p[i][0], p[i][1], p[i][2] };
		if (xfOn) xform_point(xf, p[i], q);
		arr[(*n)++] = (Vertex){ q[0], q[1], q[2], c[i][0], c[i][1], c[i][2] };
	}
}

void renderer_world_tri(const float p[3][3], const float c[3][3])
{
	pushTri(stageWorld, &worldCount, MAX_WORLD_VERTS, &worldXf, worldXfOn, p, c);
}

void renderer_outline_tri(const float p[3][3], const float c[3][3])
{
	pushTri(stageOutline, &outlineCount, MAX_OUTLINE_VERTS, NULL, false, p, c);
}

void renderer_view_tri(const float p[3][3], const float c[3][3])
{
	pushTri(stageView, &viewCount, MAX_VIEW_VERTS, &viewXf, viewXfOn, p, c);
}

void renderer_view_box(float cx, float cy, float cz, float hx, float hy, float hz,
                       float r, float g, float b)
{
	if (viewXfOn)
		pushBoxXf(stageView, &viewCount, MAX_VIEW_VERTS, &viewXf,
		          cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz, r, g, b);
	else
		pushBox(stageView, &viewCount, MAX_VIEW_VERTS,
		        cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz, r, g, b);
}

void renderer_view_xform(float x, float y, float z, float pitch, float yaw, float roll)
{
	xform_set(&viewXf, x, y, z, pitch, yaw, roll);
	viewXfOn = true;
}

void renderer_view_xform_clear(void)
{
	viewXfOn = false;
}

void renderer_frame_begin(void)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}

void renderer_top_3d(const Player* p, float zoom)
{
	if (zoom < 1.0f) zoom = 1.0f;

	C3D_Mtx zoomProj, view, mvp;
	Mtx_PerspTilt(&zoomProj, C3D_AngleFromDegrees(BASE_FOV / zoom), C3D_AspectRatioTop, 0.2f, 200.0f, false);
	camera_view_matrix(&view, p);
	Mtx_Multiply(&mvp, &zoomProj, &view);

	// Upload this frame's dynamic geometry (the GPU has finished the previous frame)
	Vertex* dyn = vbo + MAX_STATIC_VERTS;
	if (worldCount) memcpy(dyn, stageWorld, sizeof(Vertex) * worldCount);
	if (viewCount)  memcpy(dyn + worldCount, stageView, sizeof(Vertex) * viewCount);
	if (outlineCount) memcpy(dyn + worldCount + viewCount, stageOutline, sizeof(Vertex) * outlineCount);
	if (worldCount + viewCount + outlineCount)
		GSPGPU_FlushDataCache(dyn, sizeof(Vertex) * (worldCount + viewCount + outlineCount));

	// The sky is rebuilt around the camera every frame and drawn first, without depth, behind everything
	skyCount = 0;
	if (skyDef) {
		float eye[3];
		player_eye(p, eye);
		sky_build(skyDef, eye, skyFrame++, skyEmit);
		if (skyCount) {
			memcpy(vbo + SKY_BASE, stageSky, sizeof(Vertex) * skyCount);
			GSPGPU_FlushDataCache(vbo + SKY_BASE, sizeof(Vertex) * skyCount);
		}
	}

	C3D_RenderTargetClear(topTarget, C3D_CLEAR_ALL, clearColor, 0);
	C3D_FrameDrawOn(topTarget);
	saveC2dState();
	apply3dState();

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLocProjection, &mvp);
	if (skyCount) {
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		C3D_DrawArrays(GPU_TRIANGLES, SKY_BASE, skyCount);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	}
	C3D_DrawArrays(GPU_TRIANGLES, 0, staticCount);

	// Team outlines: slightly bigger eggs drawn first without writing depth, so the real egg drawn
	// afterwards covers them everywhere except a thin rim. Walls still hide them.
	if (outlineCount) {
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
		C3D_DrawArrays(GPU_TRIANGLES, MAX_STATIC_VERTS + worldCount + viewCount, outlineCount);
		C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
	}
	if (worldCount)
		C3D_DrawArrays(GPU_TRIANGLES, MAX_STATIC_VERTS, worldCount);

	if (viewCount) {
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLocProjection, &baseProjection);
		C3D_DrawArrays(GPU_TRIANGLES, MAX_STATIC_VERTS + worldCount, viewCount);
	}

	restoreC2dState();
}

void renderer_top_2d(unsigned int clearColor)
{
	C2D_Prepare();
	C2D_TargetClear(topTarget, clearColor);
	C2D_SceneBegin(topTarget);
}

void renderer_top_overlay(void)
{
	// 2D drawing on top of the 3D scene already on the top screen (nothing is cleared)
	C2D_Prepare();
	C2D_SceneBegin(topTarget);
}

void renderer_bottom_2d(unsigned int clearColor)
{
	C2D_Prepare();
	C2D_TargetClear(bottomTarget, clearColor);
	C2D_SceneBegin(bottomTarget);
}

void renderer_frame_end(void)
{
	C3D_FrameEnd(0);
}

void renderer_shutdown(void)
{
	if (vbo) linearFree(vbo);
	shaderProgramFree(&program);
	if (vshaderDvlb) DVLB_Free(vshaderDvlb);
	C2D_Fini();
	C3D_Fini();
}
