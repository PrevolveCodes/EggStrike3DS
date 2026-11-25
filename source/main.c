// EggStrike 3DS - menus, bottom-screen UI, profile saving
#include <3ds.h>
#include <stdio.h>

#include "config.h"
#include "game.h"
#include "input.h"
#include "renderer.h"
#include "ui.h"

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	gfxInitDefault();

	if (!renderer_init() || !ui_init()) {
		consoleInit(GFX_BOTTOM, NULL);
		printf("Graphics init failed.\nPress START to exit.\n");
		while (aptMainLoop()) {
			hidScanInput();
			if (hidKeysDown() & KEY_START) break;
			gfxFlushBuffers();
			gfxSwapBuffers();
			gspWaitForVBlank();
		}
		gfxExit();
		return 1;
	}

	game_init();

	u64 last = osGetTime();
	while (aptMainLoop()) {
		u64 now = osGetTime();
		float dt = (float)(now - last) / 1000.0f;
		last = now;
		if (dt > MAX_DT) dt = MAX_DT;
		if (dt < 0.001f) dt = 0.001f;

		input_update();
		if (!game_update(dt)) break;
		game_draw();
	}

	game_shutdown();
	ui_shutdown();
	renderer_shutdown();
	gfxExit();
	return 0;
}
