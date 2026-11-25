#include "rng.h"

static unsigned int state = 2463534242u;

void rng_seed(unsigned int seed)
{
	state = seed ? seed : 1u;
}

float rng_float(void)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return (float)(state >> 8) * (1.0f / 16777216.0f);
}
