#include "viewmodel.h"
#include "renderer.h"
#include "cosmetics.h"
#include "config.h"
#include <math.h>
#include <string.h>

// Weapon models, built from two kinds of part in weapon space:
// +X right, +Y up, -Z toward the muzzle, origin at the receiver.
//
//   P_BOX  a box. hx/hy/hz are half sizes. 'taper' scales the front (-Z) end, so a
//          stock can narrow toward the receiver.
//   P_CYL  a round bar along Z (barrels, scopes, tubes). hx/hy are the radii at the back
//          (+Z) end, 'taper' scales the front end (a cone has a small taper, a flared
//          scope bell has a taper above 1).
//   P_RND  a rounded box: like P_BOX (half sizes, taper) but with softly rounded corners, for
//          stocks, receivers, forends, grips and magazines.
//   P_SPH  an ellipsoid (hx/hy/hz are its radii); used for the floating egg hands.
//
// Boxes and cylinders can be tilted about the X axis with 'rot' (radians, around the part centre):
//   rot > 0 lowers the back and raises the front. Magazines lean this way (bottom forward).
//   rot < 0 tilts the bottom backward, which is how pistol grips lean.
//
// Parts are listed most important first. Bots only draw the first 'core' parts, with
// cheaper 6-sided cylinders, because they are small and far away.

enum { P_BOX, P_CYL, P_SPH, P_RND };
enum { C_MAIN, C_DARK, C_METAL, C_ACC, C_WOOD, C_LENS, C_BRASS, C_HAND, C_COUNT };

typedef struct {
	int   kind, col;
	float cx, cy, cz, hx, hy, hz, rot, taper;
} Part;

#define BOXTR(c, x, y, z, hx, hy, hz, t, r) { P_BOX, c, x, y, z, hx, hy, hz, r, t }
#define BOX(c, x, y, z, hx, hy, hz)         BOXTR(c, x, y, z, hx, hy, hz, 1.0f, 0.0f)
#define BOXR(c, x, y, z, hx, hy, hz, r)     BOXTR(c, x, y, z, hx, hy, hz, 1.0f, r)
#define BOXT(c, x, y, z, hx, hy, hz, t)     BOXTR(c, x, y, z, hx, hy, hz, t, 0.0f)
#define RNDTR(c, x, y, z, hx, hy, hz, t, r) { P_RND, c, x, y, z, hx, hy, hz, r, t }
#define RND(c, x, y, z, hx, hy, hz)         RNDTR(c, x, y, z, hx, hy, hz, 1.0f, 0.0f)
#define RNDR(c, x, y, z, hx, hy, hz, r)     RNDTR(c, x, y, z, hx, hy, hz, 1.0f, r)
#define RNDT(c, x, y, z, hx, hy, hz, t)     RNDTR(c, x, y, z, hx, hy, hz, t, 0.0f)
#define CYLT(c, x, y, z, rad, hz, t)        { P_CYL, c, x, y, z, rad, rad, hz, 0.0f, t }
#define CYL(c, x, y, z, rad, hz)            CYLT(c, x, y, z, rad, hz, 1.0f)

typedef struct {
	const Part* parts;
	int   count;
	int   core;            // parts a bot's held gun draws
	float accent[3];
	float muzzleY, muzzleZ;
	float handR[3], handL[3];
	float zShift;          // pushes the whole first-person model forward (long rear ends)
} Model;

#define MODEL_SCALE 1.25f
// Guns held by other eggs are drawn much bigger than life so you can see from far away which way they point
#define WORLD_GUN_SCALE 2.3f
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Gun models. Generated from an audited layout: every part overlaps its neighbours by 1-3 mm,
// nothing floats, nothing is buried, and no flat faces share a plane (no flicker).
// The first `core` parts are the ones a bot's held gun draws.
#define PART(k, c, x, y, z, hx, hy, hz, r, t) { k, c, x, y, z, hx, hy, hz, r, t }

// Gun models. Generated from an audited layout: every part overlaps its neighbours by 1-3 mm,
// nothing floats, nothing is buried, and no flat faces share a plane (no flicker).
// The first `core` parts are the ones a bot's held gun draws.
#define PART(k, c, x, y, z, hx, hy, hz, r, t) { k, c, x, y, z, hx, hy, hz, r, t }

// ------------------------------------------------------------------ Splatterpop
// Pump shotgun: wooden forend and stock, tube magazine, vent rib and bead sight.
static const Part splatterpop[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0000f,  0.0000f, 0.0280f, 0.0360f, 0.1000f,  0.000f, 1.000f),   // receiver
	PART(P_CYL, C_METAL,  0.0000f,  0.0160f, -0.3500f, 0.0170f, 0.0170f, 0.2700f,  0.000f, 1.000f),   // barrel
	PART(P_CYL, C_DARK,  0.0000f, -0.0125f, -0.3200f, 0.0135f, 0.0135f, 0.2400f,  0.000f, 1.000f),   // magazine tube
	PART(P_RND, C_WOOD,  0.0000f,  0.0040f, -0.2600f, 0.0310f, 0.0340f, 0.0900f,  0.000f, 0.940f),   // pump forend
	PART(P_RND, C_WOOD,  0.0000f, -0.0220f,  0.2650f, 0.0280f, 0.0520f, 0.1750f,  0.100f, 0.620f),   // stock
	PART(P_RND, C_WOOD,  0.0000f, -0.0710f,  0.0850f, 0.0170f, 0.0420f, 0.0200f, -0.280f, 1.000f),   // grip
	PART(P_CYL, C_ACC,   0.0000f,  0.0160f, -0.6120f, 0.0215f, 0.0215f, 0.0120f,  0.000f, 1.000f),   // muzzle ring
	PART(P_RND, C_DARK,  0.0000f, -0.0400f,  0.4420f, 0.0290f, 0.0560f, 0.0050f,  0.100f, 1.000f),   // butt pad
	PART(P_RND, C_DARK,  0.0000f,  0.0040f, -0.3000f, 0.0325f, 0.0355f, 0.0040f,  0.000f, 1.000f),   // forend groove
	PART(P_RND, C_DARK,  0.0000f,  0.0040f, -0.2650f, 0.0325f, 0.0355f, 0.0040f,  0.000f, 1.000f),   // forend groove
	PART(P_RND, C_DARK,  0.0000f,  0.0040f, -0.2300f, 0.0325f, 0.0355f, 0.0040f,  0.000f, 1.000f),   // forend groove
	PART(P_CYL, C_METAL,  0.0000f, -0.0125f, -0.5660f, 0.0150f, 0.0150f, 0.0060f,  0.000f, 1.000f),   // magazine cap
	PART(P_BOX, C_DARK,  0.0000f, -0.0490f,  0.0050f, 0.0040f, 0.0170f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0600f,  0.0370f, 0.0058f, 0.0030f, 0.0330f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0450f,  0.0355f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
	PART(P_BOX, C_DARK,  0.0000f, -0.0385f, -0.0100f, 0.0120f, 0.0030f, 0.0300f,  0.000f, 1.000f),   // loading gate
	PART(P_BOX, C_DARK,  0.0285f,  0.0140f, -0.0050f, 0.0012f, 0.0120f, 0.0340f,  0.000f, 1.000f),   // ejection port
	PART(P_BOX, C_DARK, -0.0285f,  0.0140f, -0.0050f, 0.0012f, 0.0120f, 0.0340f,  0.000f, 1.000f),   // ejection port
	PART(P_BOX, C_DARK,  0.0000f,  0.0345f, -0.3500f, 0.0050f, 0.0035f, 0.2700f,  0.000f, 1.000f),   // vent rib
	PART(P_BOX, C_DARK,  0.0000f,  0.0410f, -0.6000f, 0.0030f, 0.0040f, 0.0030f,  0.000f, 1.000f),   // bead stem
	PART(P_CYL, C_BRASS,  0.0000f,  0.0470f, -0.6000f, 0.0055f, 0.0055f, 0.0055f,  0.000f, 1.000f),   // bead sight
};

// ------------------------------------------------------------------ Yolkbuster
// Rocket tube with a scope, rocket nose, bands and a forward grip.
static const Part yolkbuster[] = {
	PART(P_CYL, C_MAIN,  0.0000f,  0.0200f, -0.1200f, 0.0540f, 0.0540f, 0.3600f,  0.000f, 1.000f),   // tube
	PART(P_CYL, C_METAL,  0.0000f,  0.0200f,  0.2750f, 0.0680f, 0.0680f, 0.0450f,  0.000f, 0.800f),   // flared rear
	PART(P_CYL, C_ACC,   0.0000f,  0.0200f, -0.5850f, 0.0500f, 0.0500f, 0.1050f,  0.000f, 0.170f),   // rocket nose
	PART(P_CYL, C_DARK,  0.0000f,  0.0200f, -0.4650f, 0.0620f, 0.0620f, 0.0180f,  0.000f, 1.000f),   // front rim
	PART(P_RND, C_DARK,  0.0000f, -0.0450f,  0.0300f, 0.0210f, 0.0180f, 0.0750f,  0.000f, 1.000f),   // grip housing
	PART(P_RND, C_DARK,  0.0000f, -0.0870f,  0.0400f, 0.0170f, 0.0450f, 0.0200f, -0.250f, 1.000f),   // grip
	PART(P_RND, C_DARK,  0.0000f, -0.0450f, -0.2400f, 0.0210f, 0.0180f, 0.0420f,  0.000f, 1.000f),   // fore housing
	PART(P_RND, C_DARK,  0.0000f, -0.0900f, -0.2400f, 0.0160f, 0.0480f, 0.0180f,  0.100f, 1.000f),   // fore grip
	PART(P_CYL, C_ACC,   0.0000f,  0.0200f, -0.2000f, 0.0580f, 0.0580f, 0.0120f,  0.000f, 1.000f),   // band
	PART(P_CYL, C_ACC,   0.0000f,  0.0200f,  0.0800f, 0.0580f, 0.0580f, 0.0120f,  0.000f, 1.000f),   // band
	PART(P_CYL, C_DARK,  0.0000f,  0.0200f,  0.3180f, 0.0340f, 0.0340f, 0.0060f,  0.000f, 1.000f),   // nozzle
	PART(P_BOX, C_METAL,  0.0000f,  0.0785f, -0.0050f, 0.0120f, 0.0060f, 0.0850f,  0.000f, 1.000f),   // scope rail
	PART(P_CYL, C_DARK,  0.0000f,  0.1120f, -0.0160f, 0.0220f, 0.0220f, 0.1280f,  0.000f, 1.000f),   // scope body
	PART(P_CYL, C_DARK,  0.0000f,  0.1120f, -0.1575f, 0.0220f, 0.0220f, 0.0215f,  0.000f, 1.300f),   // scope bell
	PART(P_CYL, C_DARK,  0.0000f,  0.1120f,  0.1190f, 0.0260f, 0.0260f, 0.0110f,  0.000f, 1.000f),   // eyepiece
	PART(P_CYL, C_LENS,  0.0000f,  0.1120f,  0.1285f, 0.0208f, 0.0208f, 0.0030f,  0.000f, 1.000f),   // rear glass
	PART(P_CYL, C_LENS,  0.0000f,  0.1120f, -0.1775f, 0.0243f, 0.0243f, 0.0030f,  0.000f, 1.000f),   // front glass
	PART(P_CYL, C_DARK,  0.0000f,  0.1360f, -0.0532f, 0.0080f, 0.0080f, 0.0100f,  1.571f, 1.000f),   // scope turret
	PART(P_BOX, C_DARK,  0.0000f,  0.0855f, -0.0656f, 0.0100f, 0.0115f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_BOX, C_DARK,  0.0000f,  0.0855f,  0.0460f, 0.0100f, 0.0115f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_BOX, C_DARK,  0.0000f, -0.0630f, -0.0200f, 0.0040f, 0.0100f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0670f,  0.0065f, 0.0058f, 0.0030f, 0.0275f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0660f,  0.0050f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
};

// ------------------------------------------------------------------ Longbeak
// Bolt-action sniper: long tapered barrel with muzzle brake and a scope.
static const Part longbeak[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0000f,  0.0000f, 0.0260f, 0.0330f, 0.1200f,  0.000f, 1.000f),   // receiver
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f, -0.5100f, 0.0160f, 0.0160f, 0.4100f,  0.000f, 0.680f),   // barrel
	PART(P_RND, C_MAIN,  0.0000f, -0.0140f,  0.2950f, 0.0240f, 0.0500f, 0.1950f,  0.050f, 0.550f),   // stock
	PART(P_RND, C_MAIN,  0.0000f, -0.0120f, -0.2225f, 0.0220f, 0.0270f, 0.1275f,  0.000f, 0.920f),   // forend
	PART(P_RND, C_DARK,  0.0000f, -0.0720f,  0.0900f, 0.0160f, 0.0460f, 0.0200f, -0.300f, 1.000f),   // grip
	PART(P_CYL, C_METAL,  0.0000f,  0.0120f, -0.9000f, 0.0210f, 0.0210f, 0.0350f,  0.000f, 1.000f),   // muzzle brake
	PART(P_CYL, C_DARK,  0.0000f,  0.0780f, -0.0235f, 0.0240f, 0.0240f, 0.1955f,  0.000f, 1.000f),   // scope body
	PART(P_CYL, C_DARK,  0.0000f,  0.0780f, -0.2325f, 0.0240f, 0.0240f, 0.0215f,  0.000f, 1.300f),   // scope bell
	PART(P_CYL, C_DARK,  0.0000f,  0.0780f,  0.1790f, 0.0283f, 0.0283f, 0.0110f,  0.000f, 1.000f),   // eyepiece
	PART(P_CYL, C_LENS,  0.0000f,  0.0780f,  0.1885f, 0.0227f, 0.0227f, 0.0030f,  0.000f, 1.000f),   // rear glass
	PART(P_CYL, C_LENS,  0.0000f,  0.0780f, -0.2525f, 0.0265f, 0.0265f, 0.0030f,  0.000f, 1.000f),   // front glass
	PART(P_CYL, C_DARK,  0.0000f,  0.1040f, -0.0809f, 0.0080f, 0.0080f, 0.0100f,  1.571f, 1.000f),   // scope turret
	PART(P_BOX, C_DARK,  0.0000f,  0.0455f, -0.1001f, 0.0100f, 0.0155f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_BOX, C_DARK,  0.0000f,  0.0455f,  0.0723f, 0.0100f, 0.0155f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_RND, C_DARK,  0.0000f,  0.0320f,  0.2650f, 0.0210f, 0.0100f, 0.0850f,  0.000f, 1.000f),   // cheek riser
	PART(P_RND, C_DARK,  0.0000f, -0.0237f,  0.4970f, 0.0255f, 0.0520f, 0.0060f,  0.050f, 1.000f),   // butt pad
	PART(P_RND, C_DARK,  0.0000f, -0.0520f, -0.0100f, 0.0140f, 0.0340f, 0.0220f,  0.120f, 1.000f),   // magazine
	PART(P_BOX, C_DARK,  0.0000f,  0.0120f, -0.8900f, 0.0235f, 0.0060f, 0.0050f,  0.000f, 1.000f),   // brake slot
	PART(P_BOX, C_DARK,  0.0000f,  0.0120f, -0.9120f, 0.0235f, 0.0060f, 0.0050f,  0.000f, 1.000f),   // brake slot
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f,  0.1260f, 0.0140f, 0.0140f, 0.0130f,  0.000f, 1.000f),   // bolt cap
	PART(P_BOX, C_METAL,  0.0400f,  0.0140f,  0.0700f, 0.0160f, 0.0040f, 0.0040f,  0.000f, 1.000f),   // bolt handle
	PART(P_CYL, C_METAL,  0.0620f,  0.0140f,  0.0700f, 0.0090f, 0.0090f, 0.0090f,  0.000f, 1.000f),   // bolt knob
	PART(P_BOX, C_DARK,  0.0000f, -0.0465f,  0.0150f, 0.0040f, 0.0175f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0580f,  0.0440f, 0.0058f, 0.0030f, 0.0300f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0420f,  0.0425f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
};

// ------------------------------------------------------------------ Peckmatic
// Rifle: curved magazine, rail, iron sights, buffer tube and stock.
static const Part peckmatic[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0120f,  0.0000f, 0.0280f, 0.0260f, 0.1300f,  0.000f, 1.000f),   // upper receiver
	PART(P_RND, C_DARK,  0.0000f, -0.0260f,  0.0120f, 0.0260f, 0.0200f, 0.1100f,  0.000f, 1.000f),   // lower receiver
	PART(P_RND, C_DARK,  0.0000f,  0.0070f, -0.2400f, 0.0260f, 0.0310f, 0.1200f,  0.000f, 0.920f),   // handguard
	PART(P_CYL, C_METAL,  0.0000f,  0.0120f, -0.4200f, 0.0110f, 0.0110f, 0.0800f,  0.000f, 1.000f),   // barrel
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f, -0.5200f, 0.0160f, 0.0160f, 0.0250f,  0.000f, 0.800f),   // flash hider
	PART(P_RND, C_DARK,  0.0000f, -0.0720f, -0.0200f, 0.0150f, 0.0370f, 0.0200f,  0.100f, 1.000f),   // magazine (upper)
	PART(P_RND, C_DARK,  0.0000f, -0.1425f, -0.0320f, 0.0150f, 0.0370f, 0.0200f,  0.240f, 1.000f),   // magazine (curved lower)
	PART(P_RND, C_DARK,  0.0000f, -0.0880f,  0.0920f, 0.0170f, 0.0480f, 0.0210f, -0.320f, 1.000f),   // pistol grip
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f,  0.2100f, 0.0160f, 0.0160f, 0.0900f,  0.000f, 1.000f),   // buffer tube
	PART(P_RND, C_MAIN,  0.0000f, -0.0040f,  0.3500f, 0.0210f, 0.0460f, 0.0650f,  0.000f, 0.750f),   // stock
	PART(P_BOX, C_METAL,  0.0000f,  0.0415f, -0.1200f, 0.0120f, 0.0035f, 0.2400f,  0.000f, 1.000f),   // top rail
	PART(P_RND, C_ACC,   0.0000f,  0.0070f, -0.2000f, 0.0275f, 0.0325f, 0.0080f,  0.000f, 1.000f),   // handguard band
	PART(P_RND, C_DARK,  0.0000f, -0.0040f,  0.4230f, 0.0220f, 0.0490f, 0.0080f,  0.000f, 1.000f),   // butt pad
	PART(P_BOX, C_METAL,  0.0000f, -0.1765f, -0.0405f, 0.0165f, 0.0040f, 0.0225f,  0.240f, 1.000f),   // magazine floor
	PART(P_CYL, C_DARK,  0.0000f,  0.0140f, -0.3720f, 0.0170f, 0.0170f, 0.0120f,  0.000f, 1.000f),   // gas collar
	PART(P_BOX, C_DARK,  0.0000f,  0.0500f, -0.3300f, 0.0070f, 0.0070f, 0.0120f,  0.000f, 1.000f),   // front sight tower
	PART(P_BOX, C_DARK,  0.0000f,  0.0640f, -0.3300f, 0.0030f, 0.0120f, 0.0030f,  0.000f, 1.000f),   // front sight post
	PART(P_BOX, C_DARK,  0.0000f,  0.0495f,  0.0850f, 0.0110f, 0.0050f, 0.0120f,  0.000f, 1.000f),   // rear sight base
	PART(P_BOX, C_DARK, -0.0070f,  0.0560f,  0.0850f, 0.0025f, 0.0095f, 0.0040f,  0.000f, 1.000f),   // rear sight ear
	PART(P_BOX, C_DARK,  0.0070f,  0.0560f,  0.0850f, 0.0025f, 0.0095f, 0.0040f,  0.000f, 1.000f),   // rear sight ear
	PART(P_BOX, C_METAL,  0.0000f,  0.0470f,  0.1150f, 0.0120f, 0.0035f, 0.0100f,  0.000f, 1.000f),   // charging handle
	PART(P_BOX, C_DARK,  0.0000f, -0.0530f,  0.0150f, 0.0040f, 0.0110f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0580f,  0.0465f, 0.0058f, 0.0030f, 0.0325f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0550f,  0.0450f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
};

// ------------------------------------------------------------------ Farmhand
// Three-shot scoped rifle: wood furniture, brass band, bolt handle.
static const Part farmhand[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0000f,  0.0000f, 0.0240f, 0.0320f, 0.1100f,  0.000f, 1.000f),   // receiver
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f, -0.4000f, 0.0120f, 0.0120f, 0.3100f,  0.000f, 1.000f),   // barrel
	PART(P_RND, C_WOOD,  0.0000f, -0.0020f, -0.2000f, 0.0260f, 0.0300f, 0.1050f,  0.000f, 0.920f),   // forend
	PART(P_RND, C_WOOD,  0.0000f, -0.0160f,  0.2650f, 0.0240f, 0.0500f, 0.1600f,  0.080f, 0.650f),   // stock
	PART(P_RND, C_WOOD,  0.0000f, -0.0680f,  0.0900f, 0.0160f, 0.0430f, 0.0200f, -0.300f, 1.000f),   // grip
	PART(P_CYL, C_DARK,  0.0000f,  0.0640f, -0.0210f, 0.0180f, 0.0180f, 0.0980f,  0.000f, 1.000f),   // scope body
	PART(P_CYL, C_DARK,  0.0000f,  0.0640f, -0.1325f, 0.0180f, 0.0180f, 0.0215f,  0.000f, 1.250f),   // scope bell
	PART(P_CYL, C_DARK,  0.0000f,  0.0640f,  0.0840f, 0.0212f, 0.0212f, 0.0110f,  0.000f, 1.000f),   // eyepiece
	PART(P_CYL, C_LENS,  0.0000f,  0.0640f,  0.0935f, 0.0170f, 0.0170f, 0.0030f,  0.000f, 1.000f),   // rear glass
	PART(P_CYL, C_LENS,  0.0000f,  0.0640f, -0.1525f, 0.0191f, 0.0191f, 0.0030f,  0.000f, 1.000f),   // front glass
	PART(P_CYL, C_DARK,  0.0000f,  0.0840f, -0.0492f, 0.0080f, 0.0080f, 0.0100f,  1.571f, 1.000f),   // scope turret
	PART(P_BOX, C_DARK,  0.0000f,  0.0405f, -0.0680f, 0.0100f, 0.0125f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_BOX, C_DARK,  0.0000f,  0.0405f,  0.0260f, 0.0100f, 0.0125f, 0.0120f,  0.000f, 1.000f),   // scope mount
	PART(P_CYL, C_BRASS,  0.0000f,  0.0120f, -0.3120f, 0.0165f, 0.0165f, 0.0080f,  0.000f, 1.000f),   // barrel band
	PART(P_CYL, C_DARK,  0.0000f,  0.0120f, -0.7150f, 0.0160f, 0.0160f, 0.0150f,  0.000f, 1.000f),   // muzzle
	PART(P_RND, C_WOOD,  0.0000f,  0.0360f,  0.2300f, 0.0200f, 0.0120f, 0.0700f,  0.000f, 1.000f),   // cheek piece
	PART(P_RND, C_DARK,  0.0000f, -0.0288f,  0.4280f, 0.0255f, 0.0520f, 0.0070f,  0.080f, 1.000f),   // butt plate
	PART(P_RND, C_METAL,  0.0000f, -0.0500f,  0.0000f, 0.0120f, 0.0300f, 0.0180f,  0.120f, 1.000f),   // magazine
	PART(P_BOX, C_METAL,  0.0340f,  0.0120f,  0.0400f, 0.0140f, 0.0030f, 0.0040f,  0.000f, 1.000f),   // bolt handle
	PART(P_CYL, C_METAL,  0.0520f,  0.0120f,  0.0400f, 0.0080f, 0.0080f, 0.0080f,  0.000f, 1.000f),   // bolt knob
	PART(P_BOX, C_DARK,  0.0000f,  0.0300f, -0.6900f, 0.0030f, 0.0060f, 0.0030f,  0.000f, 1.000f),   // front sight
	PART(P_BOX, C_DARK,  0.0000f, -0.0450f,  0.0300f, 0.0040f, 0.0170f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0560f,  0.0505f, 0.0058f, 0.0030f, 0.0215f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0410f,  0.0490f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
};

// ------------------------------------------------------------------ Triplet
// Three-barrel gun: barrels in a triangle, collars, carry handle.
static const Part triplet[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0000f,  0.0000f, 0.0320f, 0.0380f, 0.1300f,  0.000f, 1.000f),   // body
	PART(P_CYL, C_METAL,  0.0000f,  0.0325f, -0.2875f, 0.0125f, 0.0125f, 0.1925f,  0.000f, 1.000f),   // top barrel
	PART(P_CYL, C_METAL, -0.0125f,  0.0105f, -0.2850f, 0.0125f, 0.0125f, 0.1850f,  0.000f, 1.000f),   // left barrel
	PART(P_CYL, C_METAL,  0.0125f,  0.0105f, -0.2850f, 0.0125f, 0.0125f, 0.1850f,  0.000f, 1.000f),   // right barrel
	PART(P_RND, C_DARK,  0.0000f, -0.0740f, -0.0200f, 0.0170f, 0.0460f, 0.0240f,  0.080f, 1.000f),   // magazine
	PART(P_RND, C_DARK,  0.0000f, -0.0770f,  0.0880f, 0.0170f, 0.0460f, 0.0200f, -0.300f, 1.000f),   // grip
	PART(P_RND, C_MAIN,  0.0000f, -0.0120f,  0.2500f, 0.0220f, 0.0400f, 0.1200f,  0.050f, 0.700f),   // stock
	PART(P_BOX, C_DARK,  0.0000f,  0.0820f, -0.0200f, 0.0120f, 0.0060f, 0.0850f,  0.000f, 1.000f),   // carry handle
	PART(P_RND, C_ACC,   0.0000f,  0.0215f, -0.2050f, 0.0285f, 0.0285f, 0.0100f,  0.000f, 1.000f),   // barrel collar
	PART(P_RND, C_ACC,   0.0000f,  0.0215f, -0.4520f, 0.0285f, 0.0285f, 0.0120f,  0.000f, 1.000f),   // muzzle collar
	PART(P_BOX, C_DARK,  0.0000f,  0.0580f,  0.0600f, 0.0090f, 0.0220f, 0.0100f,  0.000f, 1.000f),   // handle post
	PART(P_BOX, C_DARK,  0.0000f,  0.0580f, -0.1000f, 0.0090f, 0.0220f, 0.0100f,  0.000f, 1.000f),   // handle post
	PART(P_RND, C_ACC,   0.0000f, -0.1175f, -0.0237f, 0.0175f, 0.0040f, 0.0245f,  0.080f, 1.000f),   // magazine floor
	PART(P_RND, C_DARK,  0.0000f, -0.0180f,  0.3760f, 0.0225f, 0.0430f, 0.0070f,  0.050f, 1.000f),   // butt pad
	PART(P_BOX, C_DARK,  0.0000f, -0.0510f,  0.0180f, 0.0040f, 0.0170f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0620f,  0.0435f, 0.0058f, 0.0030f, 0.0265f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0470f,  0.0420f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
};

// ------------------------------------------------------------------ Eggstol
// Pistol (the fixed secondary): slide with sights and a gold band, frame, trigger guard, wood grip panels.
static const Part eggstol[] = {
	PART(P_RND, C_MAIN,  0.0000f,  0.0120f, -0.0400f, 0.0190f, 0.0200f, 0.1150f,  0.000f, 1.000f),   // slide
	PART(P_RND, C_DARK,  0.0000f, -0.0160f, -0.0300f, 0.0170f, 0.0140f, 0.0950f,  0.000f, 1.000f),   // frame
	PART(P_RND, C_DARK,  0.0000f, -0.0820f,  0.0750f, 0.0190f, 0.0580f, 0.0260f, -0.220f, 1.000f),   // grip
	PART(P_CYL, C_METAL,  0.0000f,  0.0120f, -0.1850f, 0.0085f, 0.0085f, 0.0350f,  0.000f, 1.000f),   // barrel
	PART(P_CYL, C_ACC,   0.0000f,  0.0120f, -0.2175f, 0.0115f, 0.0115f, 0.0060f,  0.000f, 1.000f),   // muzzle ring
	PART(P_RND, C_WOOD,  0.0185f, -0.0820f,  0.0750f, 0.0035f, 0.0460f, 0.0190f, -0.220f, 1.000f),   // grip panel R
	PART(P_RND, C_WOOD, -0.0185f, -0.0820f,  0.0750f, 0.0035f, 0.0460f, 0.0190f, -0.220f, 1.000f),   // grip panel L
	PART(P_RND, C_METAL,  0.0000f, -0.1385f,  0.0876f, 0.0210f, 0.0040f, 0.0280f, -0.220f, 1.000f),   // magazine floor
	PART(P_BOX, C_DARK,  0.0000f,  0.0360f, -0.1400f, 0.0030f, 0.0060f, 0.0040f,  0.000f, 1.000f),   // front sight
	PART(P_BOX, C_DARK,  0.0000f,  0.0360f,  0.0620f, 0.0090f, 0.0060f, 0.0050f,  0.000f, 1.000f),   // rear sight
	PART(P_BOX, C_ACC,   0.0000f,  0.0200f, -0.0400f, 0.0205f, 0.0040f, 0.0400f,  0.000f, 1.000f),   // slide band
	PART(P_BOX, C_DARK,  0.0000f, -0.0420f, -0.0450f, 0.0040f, 0.0160f, 0.0040f,  0.000f, 1.000f),   // trigger guard (front)
	PART(P_BOX, C_DARK,  0.0000f, -0.0520f,  0.0040f, 0.0058f, 0.0030f, 0.0500f,  0.000f, 1.000f),   // trigger guard (bar)
	PART(P_BOX, C_METAL,  0.0000f, -0.0390f,  0.0025f, 0.0030f, 0.0110f, 0.0040f, -0.250f, 1.000f),   // trigger
	PART(P_BOX, C_DARK,  0.0000f,  0.0125f,  0.0680f, 0.0045f, 0.0080f, 0.0080f,  0.400f, 1.000f),   // hammer
};

static const Model models[WEAPON_COUNT] = {
	[W_SPLATTERPOP] = { splatterpop, COUNT(splatterpop), 7, { 0.95f, 0.78f, 0.25f }, 0.016f, -0.63f,
		{ 0.000f, -0.095f, 0.085f }, { -0.004f, -0.050f, -0.260f } },
	[W_YOLKBUSTER] = { yolkbuster, COUNT(yolkbuster), 6, { 1.00f, 0.85f, 0.25f }, 0.020f, -0.69f,
		{ 0.000f, -0.115f, 0.040f }, { 0.000f, -0.120f, -0.240f } },
	[W_LONGBEAK] = { longbeak, COUNT(longbeak), 6, { 0.85f, 0.66f, 0.26f }, 0.012f, -0.94f,
		{ 0.000f, -0.092f, 0.092f }, { 0.000f, -0.052f, -0.240f } },
	[W_PECKMATIC] = { peckmatic, COUNT(peckmatic), 10, { 0.95f, 0.45f, 0.20f }, 0.012f, -0.53f,
		{ 0.000f, -0.098f, 0.092f }, { 0.000f, -0.045f, -0.220f } },
	[W_FARMHAND] = { farmhand, COUNT(farmhand), 5, { 0.85f, 0.66f, 0.26f }, 0.012f, -0.74f,
		{ 0.000f, -0.092f, 0.092f }, { 0.000f, -0.045f, -0.190f } },
	[W_TRIPLET] = { triplet, COUNT(triplet), 8, { 0.95f, 0.85f, 0.30f }, 0.020f, -0.48f,
		{ 0.000f, -0.095f, 0.088f }, { 0.000f, -0.040f, -0.200f } },
	[W_EGGSTOL] = { eggstol, COUNT(eggstol), 5, { 0.95f, 0.80f, 0.30f }, 0.012f, -0.23f,
		{ 0.004f, -0.078f, 0.082f }, { -0.020f, -0.108f, 0.090f } },
};

// ------------------------------------------------------------------ colours
typedef struct { float c[C_COUNT][3]; } Palette;

static void makePalette(Palette* pal, const float mainCol[3], const float accentCol[3], const float hand[3])
{
	static const float dark[3]  = { 0.17f, 0.17f, 0.20f };
	static const float metal[3] = { 0.60f, 0.62f, 0.66f };
	static const float wood[3]  = { 0.48f, 0.32f, 0.18f };
	static const float lens[3]  = { 0.35f, 0.68f, 0.88f };
	static const float brass[3] = { 0.85f, 0.66f, 0.26f };
	memcpy(pal->c[C_MAIN],  mainCol,   sizeof(float) * 3);
	memcpy(pal->c[C_DARK],  dark,      sizeof(float) * 3);
	memcpy(pal->c[C_METAL], metal,     sizeof(float) * 3);
	memcpy(pal->c[C_ACC],   accentCol, sizeof(float) * 3);
	memcpy(pal->c[C_WOOD],  wood,      sizeof(float) * 3);
	memcpy(pal->c[C_LENS],  lens,      sizeof(float) * 3);
	memcpy(pal->c[C_BRASS], brass,     sizeof(float) * 3);
	memcpy(pal->c[C_HAND],  hand,      sizeof(float) * 3);
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// ------------------------------------------------------------------ triangles
// Parts become flat or smooth-shaded triangles handed to a sink, so the same code
// draws first-person models and bot guns.
typedef void (*TriSink)(const float p[3][3], const float c[3][3]);

static void emitTri(TriSink sink, const float a[3], const float b[3], const float c[3],
                    const float col[3], float sa, float sb, float sc)
{
	float p[3][3], k[3][3];
	memcpy(p[0], a, sizeof(float) * 3);
	memcpy(p[1], b, sizeof(float) * 3);
	memcpy(p[2], c, sizeof(float) * 3);
	const float s[3] = { sa, sb, sc };
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) k[i][j] = col[j] * s[i];
	sink(p, k);
}

static void emitQuad(TriSink sink, const float a[3], const float b[3], const float c[3], const float d[3],
                     const float col[3], float shade)
{
	emitTri(sink, a, b, c, col, shade, shade, shade);
	emitTri(sink, a, c, d, col, shade, shade, shade);
}

// A point in part space (x, y, z from the part centre), tilted about X, scaled by S.
static void place(const Part* pt, float rc, float rs, float S, float x, float y, float z, float out[3])
{
	out[0] = (pt->cx + x) * S;
	out[1] = (pt->cy + y * rc - z * rs) * S;
	out[2] = (pt->cz + y * rs + z * rc) * S;
}

static void emitBox(const Part* pt, float S, const float col[3], TriSink sink)
{
	float rc = cosf(pt->rot), rs = sinf(pt->rot);
	float c[8][3];
	for (int i = 0; i < 8; i++) {
		float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f, sz = (i & 4) ? 1.0f : -1.0f;
		float k = sz < 0.0f ? pt->taper : 1.0f;
		place(pt, rc, rs, S, sx * pt->hx * k, sy * pt->hy * k, sz * pt->hz, c[i]);
	}
	// top, bottom, back (+z), front (-z), +x, -x
	static const int faces[6][4] = {
		{ 2, 3, 7, 6 }, { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 1, 3, 2 }, { 1, 3, 7, 5 }, { 0, 2, 6, 4 } };
	static const float shade[6] = { 1.0f, 0.4f, 0.8f, 0.8f, 0.65f, 0.65f };
	for (int f = 0; f < 6; f++)
		emitQuad(sink, c[faces[f][0]], c[faces[f][1]], c[faces[f][2]], c[faces[f][3]], col, shade[f]);
}

#define MAX_SIDES 12

// expo 1 gives a circle; 0.5 gives a rounded square (superellipse with exponent 4).
static void emitCyl(const Part* pt, float S, const float col[3], int sides, float expo, TriSink sink)
{
	float rc = cosf(pt->rot), rs = sinf(pt->rot);
	float back[MAX_SIDES][3], front[MAX_SIDES][3], sh[MAX_SIDES];
	for (int i = 0; i < sides; i++) {
		float a = 6.2831853f * (float)i / (float)sides;
		float c0 = cosf(a), s0 = sinf(a);
		float px = c0, py = s0, nx = c0, ny = s0;
		if (expo != 1.0f) {
			float ac = fabsf(c0), as = fabsf(s0);
			px = copysignf(powf(ac, expo), c0);
			py = copysignf(powf(as, expo), s0);
			float n = 2.0f / expo - 1.0f;        // outward normal of the rounded square
			nx = copysignf(powf(fabsf(px), n), px);
			ny = copysignf(powf(fabsf(py), n), py);
			float len = sqrtf(nx * nx + ny * ny);
			nx /= len; ny /= len;
		}
		place(pt, rc, rs, S, px * pt->hx, py * pt->hy, pt->hz, back[i]);
		place(pt, rc, rs, S, px * pt->hx * pt->taper, py * pt->hy * pt->taper, -pt->hz, front[i]);
		// light from the upper left: smooth shading round the bar makes it read as round
		float d = nx * -0.362f + ny * rc * 0.932f;
		sh[i] = 0.66f + 0.34f * d;
	}
	for (int i = 0; i < sides; i++) {
		int j = (i + 1) % sides;
		emitTri(sink, back[i], front[i], front[j], col, sh[i], sh[i], sh[j]);
		emitTri(sink, back[i], front[j], back[j], col, sh[i], sh[j], sh[j]);
	}
	// end caps (glass stays bright, everything else is a bit darker than the sides)
	float cb[3], cf[3];
	place(pt, rc, rs, S, 0.0f, 0.0f,  pt->hz, cb);
	place(pt, rc, rs, S, 0.0f, 0.0f, -pt->hz, cf);
	bool glass = (col[2] > col[0] + 0.3f);
	float sBack = glass ? 1.0f : 0.62f, sFront = glass ? 1.0f : 0.5f;
	for (int i = 0; i < sides; i++) {
		int j = (i + 1) % sides;
		emitTri(sink, cb, back[i], back[j], col, sBack, sBack, sBack);
		emitTri(sink, cf, front[i], front[j], col, sFront, sFront, sFront);
	}
}

#define SPH_SIDES 10
#define SPH_RINGS 5

// Smooth-shaded ellipsoid: rings of vertices from the bottom pole to the top pole.
static void emitSphere(const Part* pt, float S, const float col[3], TriSink sink)
{
	float v[SPH_RINGS + 1][SPH_SIDES][3], sh[SPH_RINGS + 1][SPH_SIDES];
	for (int r = 0; r <= SPH_RINGS; r++) {
		float lat = -1.5707963f + 3.1415927f * (float)r / (float)SPH_RINGS;
		float cl = cosf(lat), sl = sinf(lat);
		for (int i = 0; i < SPH_SIDES; i++) {
			float a = 6.2831853f * (float)i / (float)SPH_SIDES;
			float nx = cl * cosf(a), ny = sl, nz = cl * sinf(a);
			v[r][i][0] = (pt->cx + nx * pt->hx) * S;
			v[r][i][1] = (pt->cy + ny * pt->hy) * S;
			v[r][i][2] = (pt->cz + nz * pt->hz) * S;
			float d = nx * -0.362f + ny * 0.932f + nz * 0.25f;
			sh[r][i] = 0.70f + 0.30f * d;
		}
	}
	for (int r = 0; r < SPH_RINGS; r++)
		for (int i = 0; i < SPH_SIDES; i++) {
			int j = (i + 1) % SPH_SIDES;
			emitTri(sink, v[r][i], v[r + 1][i], v[r + 1][j], col, sh[r][i], sh[r + 1][i], sh[r + 1][j]);
			emitTri(sink, v[r][i], v[r + 1][j], v[r][j], col, sh[r][i], sh[r + 1][j], sh[r][j]);
		}
}

static void emitPart(const Part* pt, float S, const Palette* pal, bool distant, TriSink sink)
{
	const float* col = pal->c[pt->col];
	if (pt->kind == P_BOX) {
		emitBox(pt, S, col, sink);
	} else if (pt->kind == P_RND) {
		emitCyl(pt, S, col, distant ? 8 : 12, 0.5f, sink);
	} else if (pt->kind == P_SPH) {
		emitSphere(pt, S, col, sink);
	} else {
		emitCyl(pt, S, col, distant ? 6 : 12, 1.0f, sink);
	}
}

// ------------------------------------------------------------------ first person
// The camera's near plane is 0.2 m, and the back of a gun is only a few tens of centimetres
// in front of the eye, so long stocks would be cut open by it and fill the screen. In first
// person everything behind this weapon-space Z is trimmed away (the butt is off-screen anyway).
#define VIEW_REAR_Z 0.26f

static bool trimRear(const Part* in, Part* out, float cutZ)
{
	*out = *in;
	float front = in->cz - in->hz, back = in->cz + in->hz;
	if (front >= cutZ) return false;       // entirely behind the cut: not drawn
	if (back <= cutZ) return true;
	float t  = (cutZ - front) / (2.0f * in->hz);   // how far along the part the cut falls
	float fb = in->taper + (1.0f - in->taper) * t;        // size factor at the cut
	out->hz = (cutZ - front) * 0.5f;
	out->cz = front + out->hz;
	out->hx = in->hx * fb;
	out->hy = in->hy * fb;
	out->taper = in->taper / fb;
	return true;
}

static void viewSink(const float p[3][3], const float c[3][3]) { renderer_view_tri(p, c); }
static void worldSink(const float p[3][3], const float c[3][3]) { renderer_world_tri(p, c); }

// ------------------------------------------------------------------ muzzle flash
// A flat, spiky star with a white-hot centre fading to orange at the tips, plus a thin forward
// streak. Flat triangles with no thickness, so it reads as a quick flash and not as a block.
// The star lies in a plane facing along +Z (toward the shooter); 'plane' 0 = XY, 1 = ZY (side on).
#define FLASH_SPIKES 6
static void emitFlashStar(TriSink sink, float cx, float cy, float cz, float R, float rot, int plane)
{
	static const float hot[3] = { 1.0f, 0.98f, 0.80f };
	static const float tip[3] = { 1.0f, 0.55f, 0.08f };
	static const float mid[3] = { 1.0f, 0.78f, 0.25f };
	float pt[FLASH_SPIKES * 2][3], col[FLASH_SPIKES * 2][3];
	for (int i = 0; i < FLASH_SPIKES * 2; i++) {
		bool spike = (i % 2) == 0;
		float a = rot + 3.1415927f * (float)i / (float)FLASH_SPIKES;
		float r = spike ? R * (((i / 2) % 2) ? 0.70f : 1.0f) : R * 0.30f;   // alternate long and short spikes
		float u = cosf(a) * r, v = sinf(a) * r;
		pt[i][0] = cx + (plane == 0 ? u : 0.0f);
		pt[i][1] = cy + v;
		pt[i][2] = cz + (plane == 0 ? 0.0f : u);
		memcpy(col[i], spike ? tip : mid, sizeof tip);
	}
	float centre[3] = { cx, cy, cz };
	for (int i = 0; i < FLASH_SPIKES * 2; i++) {
		int j = (i + 1) % (FLASH_SPIKES * 2);
		float p[3][3], c[3][3];
		memcpy(p[0], centre, 12); memcpy(c[0], hot, 12);
		memcpy(p[1], pt[i], 12);  memcpy(c[1], col[i], 12);
		memcpy(p[2], pt[j], 12);  memcpy(c[2], col[j], 12);
		sink(p, c);
	}
}

// Thin streak shooting out of the muzzle along -Z (two crossed slivers)
static void emitFlashStreak(TriSink sink, float cx, float cy, float cz, float len, float w)
{
	static const float hot[3] = { 1.0f, 0.95f, 0.70f };
	static const float tip[3] = { 1.0f, 0.50f, 0.05f };
	for (int k = 0; k < 2; k++) {
		float p[3][3] = {
			{ cx - (k == 0 ? w : 0.0f), cy - (k == 1 ? w : 0.0f), cz },
			{ cx + (k == 0 ? w : 0.0f), cy + (k == 1 ? w : 0.0f), cz },
			{ cx, cy, cz - len } };
		float c[3][3];
		memcpy(c[0], hot, 12); memcpy(c[1], hot, 12); memcpy(c[2], tip, 12);
		sink(p, c);
	}
}


// Floating egg hands, not attached to anything: a round fist with a round thumb.
static void emitHand(const float at[3], float side, float S, const Palette* pal)
{
	Part fist  = { P_SPH, C_HAND, at[0], at[1], at[2], 0.040f, 0.038f, 0.044f, 0.0f, 1.0f };
	Part thumb = { P_SPH, C_HAND, at[0] + side * 0.030f, at[1] + 0.022f, at[2] - 0.016f, 0.017f, 0.017f, 0.026f, 0.0f, 1.0f };
	emitPart(&fist,  S, pal, false, viewSink);
	emitPart(&thumb, S, pal, false, viewSink);
}


// ------------------------------------------------------------------ reload animations
// Every gun has its own reload, played over its reload time (t = 0..1). The pose of the gun, the
// two hands, the moving parts (magazines, bolts, slides, pump) and the ammo in the hand are all
// worked out from t here, in weapon space, so the animation always ends exactly where it began.
//   Splatterpop  tilts over and feeds two shells one by one, then pumps
//   Yolkbuster   swings up and a rocket is pushed into the back of the tube
//   Longbeak     opens the bolt, feeds three rounds from below, closes the bolt
//   Peckmatic    drops the magazine, slaps in a new one, pulls the charging handle
//   Farmhand     swaps the magazine, then works the bolt
//   Triplet      swaps the magazine and gives the gun a flick
//   Eggstol      drops the magazine, loads a new one, racks the slide
typedef struct { float t, a, b, c, d, e, f; } Key;
#define KP(t, x, y, z)                    { t, x, y, z, 0, 0, 0 }
#define KG(t, x, y, z, pi, ya, ro)        { t, x, y, z, pi, ya, ro }

typedef enum { IT_NONE, IT_SHELL, IT_ROUND, IT_ROCKET } ItemKind;

typedef struct {
	float gun[6];        // x, y, z, pitch, yaw, roll added to the normal hold
	float hl[3], hr[3];  // hands, absolute weapon-space positions
	float g[2][3];       // offsets of the two groups of moving parts
	int   item;          // ammo shown (in hand or on its way in)
	float itemPos[3];    // where it is (weapon space, its centre)
	float tilt;          // extra camera-facing tilt of the item (rocket)
	float cut;           // rear trim: the gun is pushed forward, so more of its back may show
} RAnim;

static float smooth01(float u)
{
	u = clampf(u, 0.0f, 1.0f);
	return u * u * (3.0f - 2.0f * u);
}
static float ramp(float t, float a, float b) { return smooth01((t - a) / (b - a)); }

// Smooth path through keys (values a..f), eased between neighbours
static void path(const Key* k, int n, float t, float out[6])
{
	const float* v0 = &k[0].a;
	if (t <= k[0].t) { for (int i = 0; i < 6; i++) out[i] = v0[i]; return; }
	for (int j = 1; j < n; j++) {
		if (t <= k[j].t) {
			float u = smooth01((t - k[j - 1].t) / (k[j].t - k[j - 1].t));
			const float* p = &k[j - 1].a;
			const float* q = &k[j].a;
			for (int i = 0; i < 6; i++) out[i] = p[i] + (q[i] - p[i]) * u;
			return;
		}
	}
	const float* e = &k[n - 1].a;
	for (int i = 0; i < 6; i++) out[i] = e[i];
}
#define PATH(keys, t, out) path(keys, COUNT(keys), t, out)

static void v3set(float d[3], float x, float y, float z) { d[0] = x; d[1] = y; d[2] = z; }

// Which moving group (0 or 1) a part belongs to, or -1 for a part that stays put
static int groupOf(int w, int i)
{
	switch (w) {
	case W_SPLATTERPOP: return (i == 3 || i == 8 || i == 9 || i == 10) ? 0 : -1;          // pump forend
	case W_LONGBEAK:    return (i >= 19 && i <= 21) ? 1 : -1;                             // bolt
	case W_PECKMATIC:   return (i == 5 || i == 6 || i == 13) ? 0 : (i == 20 ? 1 : -1);   // magazine / charging handle
	case W_FARMHAND:    return i == 17 ? 0 : ((i == 18 || i == 19) ? 1 : -1);             // magazine / bolt
	case W_TRIPLET:     return (i == 4 || i == 12) ? 0 : -1;                              // magazine
	case W_EGGSTOL:     return i == 7 ? 0 : ((i == 0 || i == 8 || i == 9 || i == 10 || i == 14) ? 1 : -1);   // magazine floor / slide
	default:            return -1;
	}
}

static void reloadAnim(int w, float t, const Model* m, RAnim* r)
{
	memset(r, 0, sizeof(*r));
	v3set(r->hl, m->handL[0], m->handL[1], m->handL[2]);
	v3set(r->hr, m->handR[0], m->handR[1], m->handR[2]);
	r->cut = VIEW_REAR_Z;
	float tmp[6];

	switch (w) {
	case W_SPLATTERPOP: {
		float hold = ramp(t, 0.0f, 0.09f) - ramp(t, 0.92f, 1.0f);
		r->gun[0] = -0.14f * hold; r->gun[1] = 0.10f * hold; r->gun[2] = -0.16f * hold; r->gun[3] = 0.0f * hold; r->gun[4] = 0.30f * hold; r->gun[5] = -0.55f * hold;
		const float gate[3]  = { -0.035f, -0.065f, -0.005f };
		const float pouch[3] = { -0.115f, -0.150f,  0.100f };
		float pos[3] = { m->handL[0], m->handL[1], m->handL[2] };
		// two shells, one after the other
		const float c0 = 0.11f, c1 = 0.78f;
		float cyc = (t - c0) / (c1 - c0) * 2.0f;
		float pump = 0.10f * (ramp(t, 0.84f, 0.89f) - ramp(t, 0.90f, 0.96f));
		if (cyc >= 0.0f && cyc < 2.0f) {
			float ph = cyc - floorf(cyc);
			float tri = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f;
			float s = smooth01(tri * 1.15f);
			for (int i = 0; i < 3; i++) pos[i] = pouch[i] + (gate[i] - pouch[i]) * s;
			if (ph < 0.58f) {
				r->item = IT_SHELL;
				v3set(r->itemPos, pos[0] + 0.004f, pos[1] + 0.052f, pos[2] - 0.020f);
			}
		} else if (t >= c1) {
			float u = ramp(t, c1, 0.84f);
			float from[3] = { pouch[0], pouch[1], pouch[2] };
			float to[3] = { m->handL[0], m->handL[1], m->handL[2] + pump };
			for (int i = 0; i < 3; i++) pos[i] = from[i] + (to[i] - from[i]) * u;
			if (t > 0.84f) pos[2] = m->handL[2] + pump;
		} else {
			float u = ramp(t, 0.0f, c0);
			for (int i = 0; i < 3; i++) pos[i] = m->handL[i] + (pouch[i] - m->handL[i]) * u;
		}
		for (int i = 0; i < 3; i++) r->hl[i] = pos[i];
		r->g[0][2] = pump;
		break;
	}
	case W_YOLKBUSTER: {
		// Swing the launcher forward and nose-down so its back end comes into view, push a rocket in
		float hold = ramp(t, 0.0f, 0.16f) - ramp(t, 0.86f, 1.0f);
		r->gun[0] = -0.17f * hold; r->gun[1] = 0.10f * hold; r->gun[2] = -0.42f * hold;
		r->gun[3] = -0.15f * hold; r->gun[4] = 0.34f * hold; r->gun[5] = 0.0f * hold;
		float shove = ramp(t, 0.66f, 0.72f) - ramp(t, 0.76f, 0.86f);
		r->gun[2] += -0.02f * shove;
		r->cut = 0.40f;
		static const Key rocket[] = {
			KP(0.00, -0.20f, -0.22f, 0.55f), KP(0.18, -0.20f, -0.22f, 0.55f), KP(0.42, -0.08f, -0.06f, 0.62f),
			KP(0.58, -0.01f,  0.02f, 0.62f), KP(0.64,  0.00f,  0.02f, 0.61f), KP(0.74,  0.00f,  0.02f, 0.535f) };
		PATH(rocket, t, tmp);
		if (t > 0.17f && t < 0.745f) {
			r->item = IT_ROCKET;
			v3set(r->itemPos, tmp[0], tmp[1], tmp[2]);
			// the left hand carries it, then lets go and comes back to the fore grip
			if (t < 0.64f) v3set(r->hl, tmp[0] + 0.07f, tmp[1] - 0.01f, tmp[2] + 0.03f);
		}
		if (t >= 0.64f) {
			float u = ramp(t, 0.64f, 0.70f), v = ramp(t, 0.78f, 0.92f);
			float a0[3] = { 0.07f, 0.01f, 0.58f };
			float a1[3] = { 0.07f, 0.01f, 0.52f };
			for (int i = 0; i < 3; i++) {
				float p = a0[i] + (a1[i] - a0[i]) * u;
				r->hl[i] = p + (m->handL[i] - p) * v;
			}
		}
		break;
	}
	case W_LONGBEAK: {
		float hold = ramp(t, 0.0f, 0.08f) - ramp(t, 0.92f, 1.0f);
		r->gun[0] = -0.17f * hold; r->gun[1] = 0.15f * hold; r->gun[2] = -0.12f * hold; r->gun[3] = 0.05f * hold; r->gun[4] = 0.28f * hold; r->gun[5] = -0.45f * hold;
		// bolt: lift, pull back, (rounds go in), push forward, lock down
		static const Key bolt[] = {
			KP(0.08, 0, 0, 0), KP(0.14, 0, 0.022f, 0), KP(0.28, 0, 0.022f, 0.115f), KP(0.76, 0, 0.022f, 0.115f),
			KP(0.88, 0, 0.022f, 0), KP(0.93, 0, 0, 0) };
		PATH(bolt, t, tmp);
		v3set(r->g[1], tmp[0], tmp[1], tmp[2]);
		// right hand goes to the bolt knob and works it
		float k0[3] = { 0.062f, 0.014f, 0.070f };
		float grab = ramp(t, 0.06f, 0.13f) - ramp(t, 0.92f, 1.0f);
		for (int i = 0; i < 3; i++) r->hr[i] = m->handR[i] + (k0[i] + r->g[1][i] - m->handR[i]) * grab;
		// left hand feeds three rounds into the magazine from below
		const float gate[3]  = { -0.030f, -0.090f, -0.010f };
		const float pouch[3] = { -0.100f, -0.190f, 0.090f };
		const float c0 = 0.30f, c1 = 0.72f;
		float cyc = (t - c0) / (c1 - c0) * 3.0f;
		float pos[3] = { m->handL[0], m->handL[1], m->handL[2] };
		if (cyc >= 0.0f && cyc < 3.0f) {
			float ph = cyc - floorf(cyc);
			float tri = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f;
			float s = smooth01(tri * 1.15f);
			for (int i = 0; i < 3; i++) pos[i] = pouch[i] + (gate[i] - pouch[i]) * s;
			if (ph < 0.58f) { r->item = IT_ROUND; v3set(r->itemPos, pos[0] + 0.004f, pos[1] + 0.055f, pos[2] - 0.020f); }
		} else if (t >= c1) {
			float u = ramp(t, c1, 0.82f);
			for (int i = 0; i < 3; i++) pos[i] = pouch[i] + (m->handL[i] - pouch[i]) * u;
		} else {
			float u = ramp(t, 0.12f, c0);
			for (int i = 0; i < 3; i++) pos[i] = m->handL[i] + (pouch[i] - m->handL[i]) * u;
		}
		for (int i = 0; i < 3; i++) r->hl[i] = pos[i];
		break;
	}
	case W_PECKMATIC: {
		static const Key gun[] = { KG(0.0, 0, 0, 0, 0, 0, 0), KG(0.10, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f),
			KG(0.86, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f), KG(1.0, 0, 0, 0, 0, 0, 0) };
		PATH(gun, t, r->gun);
		static const Key hand[] = {
			KP(0.00,  0.000f, -0.045f, -0.220f), KP(0.10, 0.000f, -0.120f, -0.035f), KP(0.14, 0.000f, -0.125f, -0.035f),
			KP(0.30,  0.000f, -0.520f, -0.035f), KP(0.46, 0.000f, -0.520f, -0.035f), KP(0.64, 0.000f, -0.125f, -0.035f),
			KP(0.68,  0.000f, -0.120f, -0.035f), KP(0.74, -0.030f, 0.050f, 0.120f), KP(0.80, -0.030f, 0.050f, 0.185f),
			KP(0.86, -0.030f,  0.050f,  0.120f), KP(0.95, 0.000f, -0.045f, -0.220f) };
		PATH(hand, t, tmp);
		v3set(r->hl, tmp[0], tmp[1], tmp[2]);
		static const Key mag[] = { KP(0.12, 0, 0, 0), KP(0.14, 0, -0.004f, 0), KP(0.30, 0, -0.395f, 0), KP(0.46, 0, -0.395f, 0),
			KP(0.64, 0, 0.010f, 0), KP(0.69, 0, 0, 0) };
		PATH(mag, t, tmp);
		v3set(r->g[0], tmp[0], tmp[1], tmp[2]);
		static const Key ch[] = { KP(0.74, 0, 0, 0), KP(0.80, 0, 0, 0.065f), KP(0.86, 0, 0, 0) };
		PATH(ch, t, tmp);
		v3set(r->g[1], tmp[0], tmp[1], tmp[2]);
		break;
	}
	case W_FARMHAND: {
		static const Key gun[] = { KG(0.0, 0, 0, 0, 0, 0, 0), KG(0.09, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f),
			KG(0.88, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f), KG(1.0, 0, 0, 0, 0, 0, 0) };
		PATH(gun, t, r->gun);
		static const Key hand[] = {
			KP(0.00, 0.000f, -0.045f, -0.190f), KP(0.08, 0.000f, -0.075f, -0.010f), KP(0.12, 0.000f, -0.080f, -0.010f),
			KP(0.26, 0.000f, -0.480f, -0.010f), KP(0.36, 0.000f, -0.480f, -0.010f), KP(0.50, 0.000f, -0.080f, -0.010f),
			KP(0.54, 0.000f, -0.075f, -0.010f), KP(0.64, 0.000f, -0.045f, -0.190f) };
		PATH(hand, t, tmp);
		v3set(r->hl, tmp[0], tmp[1], tmp[2]);
		static const Key mag[] = { KP(0.10, 0, 0, 0), KP(0.12, 0, -0.004f, 0), KP(0.26, 0, -0.400f, 0), KP(0.36, 0, -0.400f, 0),
			KP(0.50, 0, 0.008f, 0), KP(0.54, 0, 0, 0) };
		PATH(mag, t, tmp);
		v3set(r->g[0], tmp[0], tmp[1], tmp[2]);
		static const Key bolt[] = {
			KP(0.64, 0, 0, 0), KP(0.68, 0, 0.020f, 0), KP(0.76, 0, 0.020f, 0.095f), KP(0.80, 0, 0.020f, 0.095f),
			KP(0.88, 0, 0.020f, 0), KP(0.92, 0, 0, 0) };
		PATH(bolt, t, tmp);
		v3set(r->g[1], tmp[0], tmp[1], tmp[2]);
		float k0[3] = { 0.055f, 0.016f, 0.040f };
		float grab = ramp(t, 0.56f, 0.64f) - ramp(t, 0.92f, 1.0f);
		for (int i = 0; i < 3; i++) r->hr[i] = m->handR[i] + (k0[i] + r->g[1][i] - m->handR[i]) * grab;
		break;
	}
	case W_TRIPLET: {
		static const Key gun[] = { KG(0.0, 0, 0, 0, 0, 0, 0), KG(0.10, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f),
			KG(0.74, -0.16f, 0.16f, -0.10f, 0.05f, 0.24f, -0.42f), KG(0.82, -0.12f, 0.17f, -0.10f, 0.20f, 0.20f, -0.30f),
			KG(0.90, 0, -0.01f, 0, -0.04f, 0, 0), KG(1.0, 0, 0, 0, 0, 0, 0) };
		PATH(gun, t, r->gun);
		static const Key hand[] = {
			KP(0.00, 0.000f, -0.040f, -0.200f), KP(0.10, 0.000f, -0.115f, -0.030f), KP(0.14, 0.000f, -0.120f, -0.030f),
			KP(0.30, 0.000f, -0.520f, -0.030f), KP(0.44, 0.000f, -0.520f, -0.030f), KP(0.62, 0.000f, -0.120f, -0.030f),
			KP(0.66, 0.000f, -0.115f, -0.030f), KP(0.78, 0.000f, -0.040f, -0.200f) };
		PATH(hand, t, tmp);
		v3set(r->hl, tmp[0], tmp[1], tmp[2]);
		static const Key mag[] = { KP(0.12, 0, 0, 0), KP(0.14, 0, -0.004f, 0), KP(0.30, 0, -0.400f, 0), KP(0.44, 0, -0.400f, 0),
			KP(0.62, 0, 0.010f, 0), KP(0.67, 0, 0, 0) };
		PATH(mag, t, tmp);
		v3set(r->g[0], tmp[0], tmp[1], tmp[2]);
		break;
	}
	case W_EGGSTOL: {
		static const Key gun[] = { KG(0.0, 0, 0, 0, 0, 0, 0), KG(0.10, -0.14f, 0.14f, -0.10f, 0.25f, 0.20f, -0.50f),
			KG(0.90, -0.14f, 0.14f, -0.10f, 0.25f, 0.20f, -0.50f), KG(1.0, 0, 0, 0, 0, 0, 0) };
		PATH(gun, t, r->gun);
		static const Key hand[] = {
			KP(0.00, -0.020f, -0.108f, 0.090f), KP(0.10, -0.035f, -0.130f, 0.085f), KP(0.16, -0.035f, -0.140f, 0.085f),
			KP(0.32, -0.035f, -0.520f, 0.085f), KP(0.44, -0.035f, -0.520f, 0.085f), KP(0.60, -0.035f, -0.140f, 0.085f),
			KP(0.64, -0.035f, -0.135f, 0.085f), KP(0.70, -0.045f, 0.030f, 0.040f), KP(0.76, -0.045f, 0.030f, 0.095f),
			KP(0.82, -0.045f, 0.030f, 0.040f), KP(0.92, -0.020f, -0.108f, 0.090f) };
		PATH(hand, t, tmp);
		v3set(r->hl, tmp[0], tmp[1], tmp[2]);
		static const Key mag[] = { KP(0.14, 0, 0, 0), KP(0.16, 0, -0.004f, 0), KP(0.32, 0, -0.380f, 0), KP(0.44, 0, -0.380f, 0),
			KP(0.60, 0, 0.008f, 0), KP(0.64, 0, 0, 0) };
		PATH(mag, t, tmp);
		v3set(r->g[0], tmp[0], tmp[1], tmp[2]);
		static const Key slide[] = { KP(0.70, 0, 0, 0), KP(0.76, 0, 0, 0.050f), KP(0.82, 0, 0, 0) };
		PATH(slide, t, tmp);
		v3set(r->g[1], tmp[0], tmp[1], tmp[2]);
		break;
	}
	default: break;
	}
}

// The ammo being handled: shells, rounds and the rocket are drawn as small parts, in weapon space
static void emitItem(const RAnim* r, const Palette* pal, float S)
{
	const float* c = r->itemPos;
	if (r->item == IT_SHELL) {
		Part body = { P_CYL, C_ACC,   c[0], c[1], c[2], 0.012f, 0.012f, 0.028f, 0.0f, 1.0f };
		Part base = { P_CYL, C_BRASS, c[0], c[1], c[2] + 0.034f, 0.0125f, 0.0125f, 0.010f, 0.0f, 1.0f };
		emitPart(&body, S, pal, false, viewSink);
		emitPart(&base, S, pal, false, viewSink);
	} else if (r->item == IT_ROUND) {
		Part body = { P_CYL, C_BRASS, c[0], c[1], c[2], 0.0085f, 0.0085f, 0.022f, 0.0f, 1.0f };
		Part tip  = { P_CYL, C_METAL, c[0], c[1], c[2] - 0.033f, 0.0075f, 0.0075f, 0.014f, 0.0f, 0.35f };
		emitPart(&body, S, pal, false, viewSink);
		emitPart(&tip,  S, pal, false, viewSink);
	} else if (r->item == IT_ROCKET) {
		Part body = { P_CYL, C_MAIN, c[0], c[1], c[2], 0.046f, 0.046f, 0.115f, 0.0f, 1.0f };
		Part nose = { P_CYL, C_ACC,  c[0], c[1], c[2] - 0.165f, 0.046f, 0.046f, 0.050f, 0.0f, 0.15f };
		Part fin1 = { P_BOX, C_DARK, c[0], c[1] + 0.056f, c[2] + 0.085f, 0.004f, 0.020f, 0.030f, 0.0f, 1.0f };
		Part fin2 = { P_BOX, C_DARK, c[0] + 0.056f, c[1], c[2] + 0.085f, 0.020f, 0.004f, 0.030f, 0.0f, 1.0f };
		Part fin3 = { P_BOX, C_DARK, c[0] - 0.056f, c[1], c[2] + 0.085f, 0.020f, 0.004f, 0.030f, 0.0f, 1.0f };
		emitPart(&body, S, pal, false, viewSink);
		emitPart(&nose, S, pal, false, viewSink);
		emitPart(&fin1, S, pal, false, viewSink);
		emitPart(&fin2, S, pal, false, viewSink);
		emitPart(&fin3, S, pal, false, viewSink);
	}
}

// How far through the reload the gun is (0..1), or -1 when it is not reloading
static float reloadProgress(const WeaponDef* d, const WeaponState* ws)
{
	if (ws->reloadLeft <= 0.0f) return -1.0f;
	return clampf(1.0f - ws->reloadLeft / (ws->reloadTotal > 0.0f ? ws->reloadTotal : d->reloadTime), 0.0f, 1.0f);
}

// ------------------------------------------------------------------ the whisk (melee)
// A kitchen whisk: a red handle, a metal collar, and four wire loops that bulge toward the tip.
// Weapon space as for the guns: the hand holds it at the origin and the tip points to -Z.
#define WHISK_LOOPS 4
#define WHISK_SEGS  6
#define WHISK_ROOT  (-0.095f)
#define WHISK_TIP   (-0.360f)

// A thin wire from a to b: two crossed ribbons, so it looks round from any side
static void emitWire(const float a[3], const float b[3], float w, const float col[3], TriSink sink)
{
	float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
	float n1[3];
	// pick a side vector that is not parallel to the wire
	if (fabsf(d[2]) > 0.8f * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) { n1[0] = 0.0f; n1[1] = d[2]; n1[2] = -d[1]; }   // d x (1,0,0)
	else { n1[0] = d[1]; n1[1] = -d[0]; n1[2] = 0.0f; }                                                                    // d x (0,0,1)
	float l1 = sqrtf(n1[0] * n1[0] + n1[1] * n1[1] + n1[2] * n1[2]);
	if (l1 < 1.0e-6f) return;
	for (int i = 0; i < 3; i++) n1[i] /= l1;
	float n2[3] = { d[1] * n1[2] - d[2] * n1[1], d[2] * n1[0] - d[0] * n1[2], d[0] * n1[1] - d[1] * n1[0] };
	float l2 = sqrtf(n2[0] * n2[0] + n2[1] * n2[1] + n2[2] * n2[2]);
	for (int i = 0; i < 3; i++) n2[i] /= l2;
	const float* ns[2] = { n1, n2 };
	const float shade[2] = { 1.0f, 0.72f };
	for (int k = 0; k < 2; k++) {
		float q[4][3];
		for (int i = 0; i < 3; i++) {
			q[0][i] = a[i] - ns[k][i] * w; q[1][i] = a[i] + ns[k][i] * w;
			q[2][i] = b[i] + ns[k][i] * w; q[3][i] = b[i] - ns[k][i] * w;
		}
		emitQuad(sink, q[0], q[1], q[2], q[3], col, shade[k]);
	}
}

static void emitWhisk(float S, const Palette* pal, bool distant, TriSink sink)
{
	Part handle = { P_CYL, C_ACC,   0.0f, 0.0f, -0.005f, 0.017f, 0.017f, 0.080f, 0.0f, 0.85f };
	Part cap    = { P_SPH, C_ACC,   0.0f, 0.0f,  0.075f, 0.017f, 0.017f, 0.012f, 0.0f, 1.0f };
	Part collar = { P_CYL, C_METAL, 0.0f, 0.0f, -0.093f, 0.016f, 0.016f, 0.016f, 0.0f, 0.80f };
	emitPart(&handle, S, pal, distant, sink);
	emitPart(&cap,    S, pal, distant, sink);
	emitPart(&collar, S, pal, distant, sink);

	const int loops = distant ? 2 : WHISK_LOOPS, segs = distant ? 4 : WHISK_SEGS;
	const float* wire = pal->c[C_METAL];
	for (int k = 0; k < loops; k++) {
		float ang = 3.1415927f * (float)k / (float)loops;
		float cx = cosf(ang), cy = sinf(ang);
		for (int side = -1; side <= 1; side += 2) {
			float prev[3];
			for (int s = 0; s <= segs; s++) {
				float u = (float)s / (float)segs;
				float r = 0.066f * sqrtf(sinf(3.1415927f * u)) * (0.30f + 0.70f * u) + 0.006f * (1.0f - u);
				float pt[3] = { side * cx * r * S, side * cy * r * S, (WHISK_ROOT + (WHISK_TIP - WHISK_ROOT) * u) * S };
				if (s > 0) emitWire(prev, pt, 0.0032f * S, wire, sink);
				memcpy(prev, pt, sizeof(prev));
			}
		}
	}
}

static void viewmodel_draw_lowered(const Player* p, float low);

// The swing over t = 0..1: in from the lower right, a wind-up, a sweep across the middle, out to the left
static void whiskPose(float t, float out[6])
{
	static const Key keys[] = {
		KG(0.00f,  0.34f, -0.52f, -0.50f,  1.20f, -0.40f, 0.0f),
		KG(0.14f,  0.30f, -0.36f, -0.52f,  1.30f, -0.60f, 0.0f),
		KG(0.30f,  0.02f, -0.34f, -0.56f,  1.05f,  0.25f, 0.0f),
		KG(0.46f, -0.26f, -0.30f, -0.48f,  0.45f,  1.05f, 0.0f),
		KG(1.00f, -0.22f, -0.56f, -0.42f,  0.10f,  1.00f, 0.0f),
	};
	path(keys, COUNT(keys), t, out);
}

static void drawWhiskView(const Player* p, const Palette* pal)
{
	float t = clampf(1.0f - p->meleeT / MELEE_TIME, 0.0f, 1.0f);
	float pose[6];
	whiskPose(t, pose);
	if (p->meleeConnected && t > 0.27f && t < 0.42f) {     // it bounces off the egg it hit
		float k = sinf((t - 0.27f) / 0.15f * 3.1415927f);
		pose[4] -= 0.35f * k;
		pose[2] += 0.04f * k;
	}
	renderer_view_xform(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
	emitWhisk(MODEL_SCALE, pal, false, viewSink);
	const float grip[3] = { 0.0f, -0.004f, 0.020f };
	emitHand(grip, 1.0f, MODEL_SCALE, pal);
}

void viewmodel_draw(const Player* p)
{
	if (!p->alive) return;
	if (p->meleeT > 0.0f) {                              // swinging: the gun ducks away and the whisk comes in
		float t = clampf(1.0f - p->meleeT / MELEE_TIME, 0.0f, 1.0f);
		float low = ramp(t, 0.0f, 0.12f) * (1.0f - ramp(t, 0.72f, 1.0f));
		static const float red[3] = { 0.86f, 0.24f, 0.20f }, cream[3] = { 0.96f, 0.93f, 0.85f };
		float hand[3];
		cos_shell_rgb(p->shell, hand);
		Palette wp;
		makePalette(&wp, cream, red, hand);
		drawWhiskView(p, &wp);
		renderer_view_xform_clear();
		if (low > 0.6f) return;                          // the gun is out of sight
		viewmodel_draw_lowered(p, low);
		return;
	}
	viewmodel_draw_lowered(p, 0.0f);
}

// The gun in first person; 'low' 0..1 drops it out of the way (during a whisk swing)
static void viewmodel_draw_lowered(const Player* p, float low)
{
	const WeaponDef* d = weapon_def(p->cur);
	if (p->aiming && d->scoped) return; // looking through a scope hides the model

	const Model* m = &models[p->cur];
	const WeaponState* ws = &p->ws[p->cur];

	// 0..1 how far we are into aiming down sights (follows the zoom, so it is smooth)
	float a = clampf((p->zoom - 1.0f) / (d->zoom - 1.0f), 0.0f, 1.0f);
	float bb = p->bobBlend * (1.0f - 0.6f * a);
	float ph = p->bobPhase;

	float x = 0.19f * (1.0f - a);
	float y = -0.20f + 0.075f * a;
	float z = -0.60f + 0.08f * a + m->zShift;
	float pitch = 0.0f, yaw = 0.05f * (1.0f - a), roll = 0.0f;

	// walking bob and idle breathing
	x += cosf(ph) * 0.014f * bb;
	y += sinf(ph * 2.0f) * 0.010f * bb;
	roll += sinf(ph) * 0.030f * bb;
	y += sinf(p->animTime * 1.8f) * 0.003f * (1.0f - p->bobBlend);

	// look sway (weapon lags behind turning) and jump lift
	x += p->swayX * 0.8f;
	y += p->swayY * 0.8f;
	yaw += p->swayX * 2.0f;
	pitch += p->swayY * 2.0f;
	y += clampf(-p->vy * 0.004f, -0.04f, 0.04f);

	// recoil kick
	z += p->kick * 0.9f;
	pitch += p->kick * 2.5f;

	// reload: each gun has its own animation (see reloadAnim)
	RAnim ra;
	bool reloading = false;
	float rt = reloadProgress(d, ws);
	if (rt >= 0.0f) {
		reloading = true;
		reloadAnim(p->cur, rt, m, &ra);
		x += ra.gun[0]; y += ra.gun[1]; z += ra.gun[2];
		pitch += ra.gun[3]; yaw += ra.gun[4]; roll += ra.gun[5];
	}

	// weapon switch: raise from below
	float sw = clampf(p->switchAnim / 0.25f, 0.0f, 1.0f);
	y -= 0.28f * sw;
	pitch -= 0.70f * sw;

	// whisk swing: drop the gun out of the way
	x += 0.22f * low;
	y -= 0.55f * low;
	z += 0.10f * low;

	renderer_view_xform(x, y, z, pitch, yaw, roll);

	float mainCol[3], accentCol[3], hand[3];
	cos_skin_tint(p->skin, d->color, mainCol);
	cos_skin_tint(p->skin, m->accent, accentCol);
	cos_shell_rgb(p->shell, hand);
	Palette pal;
	makePalette(&pal, mainCol, accentCol, hand);

	const float cut = reloading ? ra.cut : VIEW_REAR_Z;
	for (int i = 0; i < m->count; i++) {
		Part src = m->parts[i];
		if (reloading) {
			int g = groupOf(p->cur, i);
			if (g >= 0) { src.cx += ra.g[g][0]; src.cy += ra.g[g][1]; src.cz += ra.g[g][2]; }
		}
		Part tp;
		if (trimRear(&src, &tp, cut)) emitPart(&tp, MODEL_SCALE, &pal, false, viewSink);
	}

	// Egg hands (floating, no arms) and the ammo they are handling
	if (reloading) {
		emitHand(ra.hr, 1.0f, MODEL_SCALE, &pal);
		emitHand(ra.hl, -1.0f, MODEL_SCALE, &pal);
		emitItem(&ra, &pal, MODEL_SCALE);
	} else {
		emitHand(m->handR,  1.0f, MODEL_SCALE, &pal);
		emitHand(m->handL, -1.0f, MODEL_SCALE, &pal);
	}

	// Muzzle flash: a small flat star and a short streak, rotated a little every frame
	if (p->flashT > 0.0f) {
		static int flashFrame;
		flashFrame++;
		float life = clampf(p->flashT / 0.06f, 0.0f, 1.0f);
		float k = 0.6f + 0.4f * life;                 // shrinks as it dies out
		float fz = (m->muzzleZ - 0.012f) * MODEL_SCALE;
		float fy = m->muzzleY * MODEL_SCALE;
		float rot = (float)(flashFrame % 12) * 0.52f;
		emitFlashStar(viewSink, 0.0f, fy, fz, 0.072f * k * MODEL_SCALE, rot, 0);
		emitFlashStreak(viewSink, 0.0f, fy, fz - 0.002f, 0.110f * k * MODEL_SCALE, 0.012f * MODEL_SCALE);
	}

	renderer_view_xform_clear();
}

// ------------------------------------------------------------------ menu icons
// Side view, muzzle to the left. Each part becomes one or two quads (a cylinder is split
// into a bright top half and a darker bottom half so it still looks round).
static int sideQuads(const Part* pt, float q[2][4][2], float shade[2])
{
	float rc = cosf(pt->rot), rs = sinf(pt->rot);
	float t = pt->taper, hy = pt->hy, hz = pt->hz;
	// local (z, y) outlines: front top, back top, back bottom, front bottom
	float loc[2][4][2];
	int n;
	if (pt->kind == P_BOX) {
		float b[4][2] = { { -hz, t * hy }, { hz, hy }, { hz, -hy }, { -hz, -t * hy } };
		memcpy(loc[0], b, sizeof b);
		shade[0] = 1.0f;
		n = 1;
	} else {
		float u[4][2] = { { -hz, t * hy }, { hz, hy }, { hz, 0.0f }, { -hz, 0.0f } };
		float l[4][2] = { { -hz, 0.0f }, { hz, 0.0f }, { hz, -hy }, { -hz, -t * hy } };
		memcpy(loc[0], u, sizeof u);
		memcpy(loc[1], l, sizeof l);
		shade[0] = 1.0f;
		shade[1] = 0.70f;
		n = 2;
	}
	for (int k = 0; k < n; k++)
		for (int i = 0; i < 4; i++) {
			float lz = loc[k][i][0], ly = loc[k][i][1];
			q[k][i][0] = pt->cz + ly * rs + lz * rc;     // horizontal axis: Z
			q[k][i][1] = pt->cy + ly * rc - lz * rs;     // vertical axis: Y
		}
	return n;
}

void viewmodel_icon(int weapon, float x, float y, float w, float h, IconQuadFn fn, void* user)
{
	if (weapon < 0 || weapon >= WEAPON_COUNT) return;
	const Model* m = &models[weapon];
	const WeaponDef* d = weapon_def(weapon);

	float minZ = 1e9f, maxZ = -1e9f, minY = 1e9f, maxY = -1e9f;
	for (int i = 0; i < m->count; i++) {
		float q[2][4][2], sh[2];
		int n = sideQuads(&m->parts[i], q, sh);
		for (int k = 0; k < n; k++)
			for (int v = 0; v < 4; v++) {
				if (q[k][v][0] < minZ) minZ = q[k][v][0];
				if (q[k][v][0] > maxZ) maxZ = q[k][v][0];
				if (q[k][v][1] < minY) minY = q[k][v][1];
				if (q[k][v][1] > maxY) maxY = q[k][v][1];
			}
	}
	float sx = w / (maxZ - minZ), sy = h / (maxY - minY);
	float scale = sx < sy ? sx : sy;
	float midZ = (minZ + maxZ) * 0.5f, midY = (minY + maxY) * 0.5f;

	float hand[3] = { 0.95f, 0.90f, 0.80f };
	Palette pal;
	makePalette(&pal, d->color, m->accent, hand);

	for (int i = 0; i < m->count; i++) {
		const Part* pt = &m->parts[i];
		float q[2][4][2], sh[2];
		int n = sideQuads(pt, q, sh);
		for (int k = 0; k < n; k++) {
			float scr[4][2], col[3];
			for (int v = 0; v < 4; v++) {
				scr[v][0] = x + w * 0.5f + (q[k][v][0] - midZ) * scale;
				scr[v][1] = y + h * 0.5f - (q[k][v][1] - midY) * scale;
			}
			for (int j = 0; j < 3; j++) col[j] = pal.c[pt->col][j] * sh[k];
			fn((const float (*)[2])scr, col, user);
		}
	}
}

// ------------------------------------------------------------------ bots
void viewmodel_draw_world(int weapon, float x, float y, float z, float yaw, float pitch, bool flash)
{
	if (weapon < 0 || weapon >= WEAPON_COUNT) return;
	const Model* m = &models[weapon];
	const WeaponDef* d = weapon_def(weapon);

	float hand[3] = { 0.95f, 0.90f, 0.80f };
	Palette pal;
	makePalette(&pal, d->color, m->accent, hand);

	// xform: positive yaw turns left, the game's yaw turns right
	renderer_world_xform(x, y, z, pitch, -yaw, 0.0f);
	int n = m->core < m->count ? m->core : m->count;   // the main parts are enough at a distance
	for (int i = 0; i < n; i++)
		emitPart(&m->parts[i], WORLD_GUN_SCALE, &pal, true, worldSink);

	// A bright tip on the end of the barrel: it shows where the gun points, even from far away
	static const float tipCol[3] = { 1.0f, 0.86f, 0.25f };
	Part tip = { P_BOX, C_ACC, 0.0f, m->muzzleY, m->muzzleZ - 0.03f, 0.028f, 0.028f, 0.035f, 0.0f, 1.0f };
	emitBox(&tip, WORLD_GUN_SCALE, tipCol, worldSink);

	if (flash)
	{
		static int botFlashFrame;
		botFlashFrame++;
		float rot = (float)(botFlashFrame % 12) * 0.52f;
		float fy = m->muzzleY * WORLD_GUN_SCALE;
		float fz = (m->muzzleZ - 0.06f) * WORLD_GUN_SCALE;
		emitFlashStar(worldSink, 0.0f, fy, fz, 0.26f, rot, 0);            // seen from behind or in front
		emitFlashStar(worldSink, 0.0f, fy, fz, 0.23f, rot + 0.3f, 1);     // seen from the side
		emitFlashStreak(worldSink, 0.0f, fy, fz - 0.005f, 0.55f, 0.045f);
	}
	renderer_world_xform_clear();
}

void viewmodel_draw_world_whisk(float x, float y, float z, float yaw, float t)
{
	static const float red[3] = { 0.86f, 0.24f, 0.20f }, cream[3] = { 0.96f, 0.93f, 0.85f }, hand[3] = { 0.95f, 0.90f, 0.80f };
	Palette pal;
	makePalette(&pal, cream, red, hand);
	// The arm sweeps from the egg's right side across its front to the left (game yaw turns right)
	float off = 1.25f - 2.4f * ramp(t, 0.10f, 0.50f);
	float a = yaw + off;
	float hx = x + sinf(a) * 0.55f, hz = z - cosf(a) * 0.55f;
	float lift = 0.10f * sinf(3.1415927f * clampf(t / 0.5f, 0.0f, 1.0f));
	renderer_world_xform(hx, y + lift, hz, -0.15f, -a, -1.4f);
	emitWhisk(1.9f, &pal, true, worldSink);
	renderer_world_xform_clear();
}

void viewmodel_muzzle_view(int weapon, float aim, float out[3])
{
	if (weapon < 0 || weapon >= WEAPON_COUNT) weapon = 0;
	const Model* m = &models[weapon];
	out[0] = 0.19f * (1.0f - aim);
	out[1] = -0.20f + 0.075f * aim + m->muzzleY * MODEL_SCALE;
	out[2] = 0.60f - 0.08f * aim - m->zShift - m->muzzleZ * MODEL_SCALE;   // muzzleZ is negative: ahead
}

float viewmodel_muzzle_ahead(int weapon)
{
	if (weapon < 0 || weapon >= WEAPON_COUNT) weapon = 0;
	return -models[weapon].muzzleZ * WORLD_GUN_SCALE;
}
