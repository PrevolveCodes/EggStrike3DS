# EggStrike 3DS

A fast, colourful egg-shooter for the Nintendo 3DS. You're an egg with a gun: crack the other eggs before they crack you.

## Features

- **7 maps** built from chunky blocks: Eggworks, Sunny Side, Cracked Canyon, Pan Palace, Fridge Factory, Diner Rooftops and Henhouse Hollow, each with its own sky
- **6 modes**: Free for All, Team Deathmatch, Capture the Flag, King of the Pan, Team Elimination and Bot Practice
- **7 weapons**, with damage that depends on how centred your hit is, plus grenades and a whisk for melee (D-Pad Down)
- **Bots** with their own play styles (rushers, holders, flankers) and three difficulty levels
- **Kill-streak perks**: Shell Shield, Hot Yolk, Crack Shot and Golden Egg
- **Local wireless multiplayer** for up to 4 consoles, with bots filling the empty seats
- **Cosmetics**: shells, hats, accessories and weapon skins bought with coins you earn in matches

## Controls

| Button | Action |
|---|---|
| Circle Pad | Move |
| C-Stick or touch-drag | Look |
| R | Fire |
| L | Aim / scope |
| A | Jump |
| B | Reload |
| Y | Throw grenade |
| X / D-Pad Left/Right | Switch weapon |
| D-Pad Down, ZL, ZR | Whisk (melee) |
| SELECT | Scoreboard |
| START | Pause |

L and R can be swapped in Settings.

## Installing

1. Download `EggStrike3DS.3dsx` from the [latest release](../../releases/latest).
2. Copy it to the `3ds` folder on your SD card.
3. Launch it from the Homebrew Launcher.

## Building

Needs [devkitPro](https://devkitpro.org/) with devkitARM, libctru, citro2d and citro3d installed.

```sh
make
```

This produces `EggStrike3DS.3dsx`.

## License

MIT. See [LICENSE](LICENSE).
