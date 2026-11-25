#include "save.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_MAGIC   "EGGS"
#define SAVE_VERSION 2u

typedef struct {
	char     magic[4];
	uint32_t version;
	uint32_t size;
	uint32_t checksum;
} SaveHeader;

static char savePath[256] = "sdmc:/3ds/EggStrike3DS/save.bin";

void save_set_path(const char* path)
{
	strncpy(savePath, path, sizeof(savePath) - 1);
	savePath[sizeof(savePath) - 1] = '\0';
}

static uint32_t checksum(const void* data, size_t n)
{
	const unsigned char* b = (const unsigned char*)data;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
	return h;
}

static void ensureDirs(void)
{
	char tmp[256];
	strncpy(tmp, savePath, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	for (size_t i = 1; tmp[i]; i++) {
		if (tmp[i] == '/' && tmp[i - 1] != ':') {
			tmp[i] = '\0';
			mkdir(tmp, 0777);   // fails harmlessly if it already exists
			tmp[i] = '/';
		}
	}
}

bool save_load(PlayerProfile* p)
{
	FILE* f = fopen(savePath, "rb");
	if (!f) return false;

	SaveHeader h;
	PlayerProfile tmp;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 &&
	          memcmp(h.magic, SAVE_MAGIC, 4) == 0 &&
	          h.version == SAVE_VERSION &&
	          h.size == sizeof(PlayerProfile) &&
	          fread(&tmp, sizeof(tmp), 1, f) == 1 &&
	          checksum(&tmp, sizeof(tmp)) == h.checksum;
	fclose(f);
	if (!ok) return false;

	profile_sanitize(&tmp);
	*p = tmp;
	return true;
}

bool save_write(const PlayerProfile* p)
{
	ensureDirs();
	FILE* f = fopen(savePath, "wb");
	if (!f) return false;

	SaveHeader h;
	memcpy(h.magic, SAVE_MAGIC, 4);
	h.version = SAVE_VERSION;
	h.size = sizeof(PlayerProfile);
	h.checksum = checksum(p, sizeof(PlayerProfile));

	bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(p, sizeof(PlayerProfile), 1, f) == 1;
	ok = (fclose(f) == 0) && ok;
	return ok;
}
