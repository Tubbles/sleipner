#ifndef ASSETS_H
#define ASSETS_H

#ifdef __ANDROID__

/*
 * On Android, assets are loaded at runtime from the APK via raylib's
 * asset manager integration. These stubs exist so main.c compiles;
 * the actual data is unused since we call LoadTexture() with paths
 * that raylib resolves through AAssetManager.
 */
static const unsigned char asset_gamecontrollerdb[] = {0};
static const unsigned char asset_zlog_conf[] = {0};
static const unsigned char asset_player_png[] = {0};
static const unsigned char asset_grass_png[] = {0};
static const unsigned char asset_tree_png[] = {0};
static const unsigned char asset_chest_png[] = {0};
static const unsigned char asset_house_png[] = {0};
static const unsigned char asset_fence_png[] = {0};

#else

static const unsigned char asset_gamecontrollerdb[] = {
#embed "../assets/gamecontrollerdb.txt"
};

static const unsigned char asset_zlog_conf[] = {
#embed "../assets/zlog.conf"
};

static const unsigned char asset_player_png[] = {
#embed "../assets/sprites/player.png"
};

static const unsigned char asset_grass_png[] = {
#embed "../assets/sprites/grass.png"
};

static const unsigned char asset_tree_png[] = {
#embed "../assets/sprites/tree.png"
};

static const unsigned char asset_chest_png[] = {
#embed "../assets/sprites/chest.png"
};

static const unsigned char asset_house_png[] = {
#embed "../assets/sprites/house.png"
};

static const unsigned char asset_fence_png[] = {
#embed "../assets/sprites/fence.png"
};

#endif /* __ANDROID__ */

#endif /* ASSETS_H */
