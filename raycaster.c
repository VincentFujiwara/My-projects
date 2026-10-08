#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SW 1280  
#define SH 720
#define MW 200
#define MH 200
#define TS 32
#define SKY_W 1024
#define SKY_H 720

// ============================================================
//  SYNTHESIZER SETTINGS FOR DIGITAL TERROR + VOLUME
// ============================================================
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BPM 140
#define AUDIO_TICK_RATE (AUDIO_SAMPLE_RATE * 60 / (AUDIO_BPM * 4))

#define N_E2  82.41
#define N_G2  98.00
#define N_A2  110.00
#define N_Bb2 116.54
#define N_B2  123.47
#define N_D3  146.83
#define N_E3  164.81

static double global_audio_time = 0.0;
static int global_audio_tick = 0;
static float music_volume = 0.2f;
static float volume_ui_timer = 0.0f;

void AudioInputCallback(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    double bass_line[] = {
        N_E2,  N_E2,  N_G2,  N_E2,
        N_A2,  N_E2,  N_Bb2, N_B2,
        N_E2,  N_E2,  N_D3,  N_E3,
        N_Bb2, N_A2,  N_G2,  N_E2
    };
    
    for (unsigned int i = 0; i < frames; i++) {
        global_audio_time += 1.0 / (double)AUDIO_SAMPLE_RATE;
        global_audio_tick++;
        
        int step = (global_audio_tick / AUDIO_TICK_RATE) % 16;
        int tick = global_audio_tick % AUDIO_TICK_RATE;
        double step_t = (double)tick / (double)AUDIO_SAMPLE_RATE;
        
        float left = 0.0f, right = 0.0f;
        
        double freq = bass_line[step];
        double env = exp(-7.0 * step_t);
        double raw_bass = fmod(global_audio_time * freq, 1.0) * 2.0 - 1.0;
        double filter = sin(step_t * 28.0) * env * 1.5;
        raw_bass = (raw_bass + filter) * 4.0f;
        
        if (raw_bass > 1.0) raw_bass = 1.0;
        if (raw_bass < -1.0) raw_bass = -1.0;
        
        float bass_out = (float)(raw_bass * env * 0.35f);
        left += bass_out * 0.9f; 
        right += bass_out * 1.1f;
        
        if (step % 4 == 0) {
            double k_env = exp(-22.0 * step_t);
            float kick = (float)(sin(2.0 * PI * 160.0 * exp(-40.0 * step_t) * step_t) * k_env);
            left += kick * 0.45f; right += kick * 0.45f;
        }
        if (step == 4 || step == 12) {
            double s_env = exp(-15.0 * step_t);
            float noise = (float)((global_audio_tick * 1103515245 + 12345) % 65535) / 65535.0f * 2.0f - 1.0f;
            float snare = (float)((sin(2.0 * PI * 165.0 * s_env * step_t) * 0.2f + noise * 0.4f) * s_env);
            left += snare * 0.4f; right += snare * 0.4f;
        }
        
        if (left > 1.0f) left = 1.0f;   if (left < -1.0f) left = -1.0f;
        if (right > 1.0f) right = 1.0f; if (right < -1.0f) right = -1.0f;
        
        out[i * 2] = (short)(left * 32767.0f * music_volume);     
        out[i * 2 + 1] = (short)(right * 32767.0f * music_volume); 
    }
}

// ============================================================
//  GAME ENGINE DATA AND RENDER ARRAYS
// ============================================================
int map[MW][MH];
float px = 100.5f, py = 100.5f, pa = 0.0f;
float pitch = 0.0f;
float fov = 66.0f * (PI / 180.0f);
float depth = 64.0f;
bool showMap = true; 

Color wallTex[20][TS][TS];  
Color floorTex[6][TS][TS];  
Color ceilTex[6][TS][TS];   
Color skyTex[SKY_H][SKY_W];
float zbuf[SW];

int floorMap[MW][MH];
int ceilMap[MW][MH];

void Carve(int x1, int y1, int x2, int y2, int val) {
    for (int x = x1; x <= x2; x++)
        for (int y = y1; y <= y2; y++)
            if (x >= 0 && x < MW && y >= 0 && y < MH)
                map[x][y] = val;
}

void SetWalls(int x1, int y1, int x2, int y2, int val) {
    for (int x = x1; x <= x2; x++) {
        if (x >= 0 && x < MW) {
            if (y1 >= 0 && y1 < MH && map[x][y1] != 0) map[x][y1] = val;
            if (y2 >= 0 && y2 < MH && map[x][y2] != 0) map[x][y2] = val;
        }
    }
    for (int y = y1; y <= y2; y++) {
        if (y >= 0 && y < MH) {
            if (x1 >= 0 && x1 < MW && map[x1][y] != 0) map[x1][y] = val;
            if (x2 >= 0 && x2 < MW && map[x2][y] != 0) map[x2][y] = val;
        }
    }
}

void GenMap() {
    for (int x = 0; x < MW; x++) {
        for (int y = 0; y < MH; y++) {
            map[x][y] = 1;
            floorMap[x][y] = 0;
            ceilMap[x][y] = 0;
        }
    }
    
    Carve(80, 80, 120, 120, 0);
    Carve(98, 98, 102, 102, 2);
    for (int x = 80; x <= 120; x++) {
        for (int y = 80; y <= 120; y++) {
            if (map[x][y] == 0) { floorMap[x][y] = 1; ceilMap[x][y] = 1; }
        }
    }
    
    Carve(20, 95, 80, 105, 0);
    Carve(120, 95, 180, 105, 0);
    Carve(95, 20, 105, 80, 0);
    Carve(95, 120, 105, 180, 0);
    for (int x = 20; x <= 180; x++) for (int y = 95; y <= 105; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 2; ceilMap[x][y] = 2; }
    for (int y = 20; y <= 180; y++) for (int x = 95; x <= 105; x++)
        if (map[x][y] == 0) { floorMap[x][y] = 2; ceilMap[x][y] = 2; }
    
    Carve(25, 85, 75, 115, 0);
    Carve(30, 90, 70, 92, 2);
    Carve(30, 108, 70, 110, 2);
    Carve(28, 97, 32, 103, 0);
    for (int x = 25; x <= 75; x++) for (int y = 85; y <= 115; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 3; ceilMap[x][y] = 3; }
    
    Carve(125, 85, 175, 115, 0);
    Carve(147, 97, 153, 103, 3);
    Carve(170, 97, 174, 103, 0);
    Carve(135, 120, 165, 145, 0);
    Carve(145, 115, 155, 120, 0);
    for (int x = 125; x <= 175; x++) for (int y = 85; y <= 145; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 4; ceilMap[x][y] = 4; }
    
    Carve(85, 25, 115, 55, 0);
    Carve(80, 55, 120, 75, 0);
    Carve(95, 20, 105, 25, 0);
    for (int x = 80; x <= 120; x++) for (int y = 20; y <= 75; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 5; ceilMap[x][y] = 5; }
    
    Carve(85, 125, 115, 155, 0);
    Carve(120, 130, 150, 150, 0);
    Carve(50, 130, 80, 150, 0);
    Carve(115, 135, 120, 140, 0);
    Carve(80, 135, 85, 140, 0);
    for (int x = 50; x <= 150; x++) for (int y = 125; y <= 155; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 3; ceilMap[x][y] = 3; }
    for (int x = 120; x <= 150; x++) for (int y = 130; y <= 150; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 4; ceilMap[x][y] = 4; }
    for (int x = 50; x <= 80; x++) for (int y = 130; y <= 150; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 4; ceilMap[x][y] = 4; }
    
    Carve(30, 30, 70, 50, 0);
    Carve(30, 50, 35, 80, 0);
    Carve(130, 30, 170, 50, 0);
    Carve(165, 50, 170, 80, 0);
    Carve(30, 150, 70, 170, 0);
    Carve(30, 145, 35, 150, 0);
    Carve(130, 150, 170, 170, 0);
    Carve(165, 145, 170, 150, 0);
    for (int x = 30; x <= 170; x++) for (int y = 30; y <= 170; y++)
        if (map[x][y] == 0) { floorMap[x][y] = 5; ceilMap[x][y] = 5; }
    
    SetWalls(80, 80, 120, 120, 2);
    SetWalls(25, 85, 75, 115, 3);
    SetWalls(125, 85, 175, 115, 4);
    SetWalls(85, 25, 115, 55, 2);
    SetWalls(80, 55, 120, 75, 2);
    SetWalls(85, 125, 115, 155, 3);
    SetWalls(30, 30, 70, 50, 2);
    SetWalls(130, 30, 170, 50, 2);
    SetWalls(30, 150, 70, 170, 3);
    SetWalls(130, 150, 170, 170, 3);
    SetWalls(120, 130, 150, 150, 4);
    SetWalls(50, 130, 80, 150, 4);
    SetWalls(135, 120, 165, 145, 4);
    
    for (int i = 0; i < 8; i++) {
        map[112+i][80] = 2; map[80][112+i] = 2;
        map[88-i][120] = 2; map[120][88-i] = 2;
    }
    
    map[100][20] = 5;
    
    for (int x = 0; x < MW; x++) { map[x][0] = 1; map[x][MH-1] = 1; }
    for (int y = 0; y < MH; y++) { map[0][y] = 1; map[MW-1][y] = 1; }
    
    for (int dx = -3; dx <= 3; dx++)
        for (int dy = -3; dy <= 3; dy++)
            map[100+dx][100+dy] = 0;
}

// ============================================================
//  SKY TEXTURE GENERATION
// ============================================================
void GenerateSky() {
    for (int y = 0; y < SKY_H; y++) {
        float t = (float)y / (float)SKY_H;
        for (int x = 0; x < SKY_W; x++) {
            Color c;
            if (t < 0.45f) {
                // Night sky gradient — dark at top, lighter toward horizon
                float st = t / 0.45f;
                c.r = (unsigned char)(8 + st * 25);
                c.g = (unsigned char)(12 + st * 30);
                c.b = (unsigned char)(30 + st * 65);
            } else if (t < 0.55f) {
                // Horizon glow — warm orange/blue band
                float ht = (t - 0.45f) / 0.10f;
                c.r = (unsigned char)(33 + ht * 90);
                c.g = (unsigned char)(42 + ht * 45);
                c.b = (unsigned char)(95 - ht * 35);
            } else {
                // Below horizon — dark ground haze (rarely visible)
                c = (Color){123, 87, 60, 255};
            }
            
            // Stars in upper portion
            if (t < 0.42f) {
                int hash = (x * 73856093) ^ (y * 19349663);
                if ((unsigned int)(hash % 10000) < 22) {
                    float br = (float)((hash >> 8) % 100) / 100.0f;
                    unsigned char s = (unsigned char)(130 + br * 125);
                    c = (Color){s, s, (unsigned char)(s * 0.95f), 255};
                }
            }
            skyTex[y][x] = c;
        }
    }
    
    // Moon
    int moonCX = (int)(SKY_W * 0.72);
    int moonCY = (int)(SKY_H * 0.18);
    int moonR = 28;
    for (int dy = -moonR; dy <= moonR; dy++) {
        for (int dx = -moonR; dx <= moonR; dx++) {
            int dist2 = dx*dx + dy*dy;
            if (dist2 <= moonR * moonR) {
                int sx = (moonCX + dx) % SKY_W;
                if (sx < 0) sx += SKY_W;
                int sy = moonCY + dy;
                if (sy >= 0 && sy < SKY_H) {
                    float edge = 1.0f - sqrtf((float)dist2) / (float)moonR;
                    edge = edge * 0.4f + 0.6f;
                    unsigned char c = (unsigned char)(160 + edge * 90);
                    skyTex[sy][sx] = (Color){c, c, (unsigned char)(c * 0.92f), 255};
                }
            }
        }
    }
}

// ============================================================
//  PROCEDURAL TEXTURE GENERATION — NATURAL MATERIALS ONLY
// ============================================================
void GenerateAllTextures() {
    // --- WALL TEXTURES: brick, stone, wood, mossy stone ---
    for (int t = 0; t < 20; t++) {
        for (int y = 0; y < TS; y++) {
            for (int x = 0; x < TS; x++) {
                int type = t % 4;
                if (type == 0) { 
                    // Red brick wall
                    if (x % 16 == 0 || y % 8 == 0) {
                        wallTex[t][y][x] = (Color){45, 45, 48, 255}; // mortar
                    } else {
                        int rOffset = (x + y * 3 + t * 7) % 20;
                        wallTex[t][y][x] = (Color){(unsigned char)(140 + rOffset), (unsigned char)(45 + rOffset / 2), 35, 255};
                    }
                } 
                else if (type == 1) { 
                    // Rough stone wall
                    int noise = (x * 127 + y * 231 + t * 97) % 35;
                    if ((x + y) % 16 == 0 || (x - y) % 16 == 0 || x == 0 || y == 0 || x == TS-1 || y == TS-1) {
                        wallTex[t][y][x] = (Color){50, 50, 50, 255}; // mortar cracks
                    } else {
                        wallTex[t][y][x] = (Color){(unsigned char)(100 + noise), (unsigned char)(95 + noise / 2), (unsigned char)(85 + noise / 3), 255};
                    }
                } 
                else if (type == 2) { 
                    // Old wooden planks
                    if (y % 8 == 0) {
                        wallTex[t][y][x] = (Color){35, 25, 15, 255}; // plank gaps
                    } else {
                        int noise = (x * 13 + y * 7 + t * 3) % 18;
                        int grain = (x * 3) % 7;
                        wallTex[t][y][x] = (Color){(unsigned char)(85 + noise - grain), (unsigned char)(60 + noise - grain), (unsigned char)(38 + noise - grain), 255};
                    }
                } 
                else { 
                    // Mossy stone blocks
                    if (x % 32 == 0 || y % 16 == 0 || (y % 16 == 8 && x % 16 == 0)) {
                        wallTex[t][y][x] = (Color){25, 28, 25, 255};
                    } else {
                        int noise = (x * 9 + y * 17) % 30;
                        int moss = ((x ^ y) % 7 == 0) ? 25 : 0;
                        wallTex[t][y][x] = (Color){(unsigned char)(70 + noise - moss), (unsigned char)(75 + noise + moss), (unsigned char)(70 + noise - moss), 255};
                    }
                }
            }
        }
    }

    // --- FLOOR TEXTURES: stone tiles, dirt, cobblestone ---
    for (int t = 0; t < 6; t++) {
        for (int y = 0; y < TS; y++) {
            for (int x = 0; x < TS; x++) {
                if (t % 3 == 0) {
                    // Stone tiles (checkered)
                    if (x == 0 || y == 0 || x == TS-1 || y == TS-1) {
                        floorTex[t][y][x] = (Color){15, 15, 18, 255};
                    } else {
                        bool tile = ((x / 16) + (y / 16)) % 2 == 0;
                        unsigned char c = tile ? 65 : 45;
                        floorTex[t][y][x] = (Color){c, c, (unsigned char)(c + 3), 255};
                    }
                }
                else if (t % 3 == 1) {
                    // Packed dirt / earth
                    int noise = (x * 71 + y * 13) % 22;
                    floorTex[t][y][x] = (Color){(unsigned char)(55 + noise), (unsigned char)(40 + noise), (unsigned char)(25 + noise), 255};
                }
                else {
                    // Cobblestone
                    int noise = (x * 37 + y * 59) % 28;
                    if ((x % 8 == 0) || (y % 8 == 0)) {
                        floorTex[t][y][x] = (Color){30, 28, 25, 255}; // gaps between stones
                    } else {
                        floorTex[t][y][x] = (Color){(unsigned char)(78 + noise), (unsigned char)(73 + noise), (unsigned char)(68 + noise), 255};
                    }
                }
            }
        }
    }

    // --- CEILING TEXTURES (kept for compatibility, not rendered) ---
    for (int t = 0; t < 6; t++) {
        for (int y = 0; y < TS; y++) {
            for (int x = 0; x < TS; x++) {
                ceilTex[t][y][x] = (Color){20, 20, 25, 255};
            }
        }
    }
}

int main(void) {
    InitWindow(SW, SH, "Raycaster - Digital Terror Edition");
    InitAudioDevice();
    
    GenMap();
    GenerateAllTextures();
    GenerateSky();

    AudioStream stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 2);
    SetAudioStreamCallback(stream, AudioInputCallback);
    PlayAudioStream(stream);

    Image renderBuffer = GenImageColor(SW, SH, BLACK);
    Texture2D screenTexture = LoadTextureFromImage(renderBuffer);

    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (volume_ui_timer > 0.0f) volume_ui_timer -= dt;

        // --- AUDIO VOLUME CONTROLS ---
        if (IsKeyDown(KEY_LEFT_BRACKET)) {
            music_volume -= 0.5f * dt;
            if (music_volume < 0.0f) music_volume = 0.0f;
            volume_ui_timer = 2.0f;
        }
        if (IsKeyDown(KEY_RIGHT_BRACKET)) {
            music_volume += 0.5f * dt;
            if (music_volume > 1.0f) music_volume = 1.0f;
            volume_ui_timer = 2.0f;
        }
        if (IsKeyPressed(KEY_M)) showMap = !showMap;

        // --- MOUSE LOOK (horizontal + vertical) ---
        Vector2 mouseDelta = GetMouseDelta();
        float mouseSensitivity = 0.0025f;
        pa += mouseDelta.x * mouseSensitivity;
        pitch -= mouseDelta.y * 0.08f;
        
        // Clamp pitch so you can't flip upside down
        if (pitch > 250.0f) pitch = 250.0f;
        if (pitch < -250.0f) pitch = -250.0f;
        
        if (pa < 0.0f) pa += 2.0f * PI;
        if (pa > 2.0f * PI) pa -= 2.0f * PI;

        // --- KEYBOARD MOVEMENT ---
        float moveSpeed = 6.0f * dt;
        float npx = px;
        float npy = py;

        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
            npx += cosf(pa) * moveSpeed;
            npy += sinf(pa) * moveSpeed;
        }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
            npx -= cosf(pa) * moveSpeed;
            npy -= sinf(pa) * moveSpeed;
        }
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
            npx += sinf(pa) * moveSpeed;
            npy -= cosf(pa) * moveSpeed;
        }
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
            npx -= sinf(pa) * moveSpeed;
            npy += cosf(pa) * moveSpeed;
        }

        if (map[(int)npx][(int)py] == 0) px = npx;
        if (map[(int)px][(int)npy] == 0) py = npy;

        // --- RAYCASTING RENDER ---
        Color *pixels = (Color *)renderBuffer.data;

        for (int i = 0; i < SW * SH; i++) pixels[i] = BLACK;

        float horizon = SH / 2.0f + pitch;
        float lightRadius = 16.0f;

        for (int x = 0; x < SW; x++) {
            float rayAngle = (pa - fov / 2.0f) + ((float)x / (float)SW) * fov;
            float eyeX = cosf(rayAngle);
            float eyeY = sinf(rayAngle);

            bool hitWall = false;
            int side = 0;
            int texID = 0;
            float sampleX = 0.0f;
            float distanceToWall = 0.0f;

            // DDA Setup
            float mapX = (int)px;
            float mapY = (int)py;
            float deltaDistX = fabsf(1.0f / eyeX);
            float deltaDistY = fabsf(1.0f / eyeY);
            float sideDistX, sideDistY;
            int stepX, stepY;

            if (eyeX < 0) { stepX = -1; sideDistX = (px - mapX) * deltaDistX; }
            else { stepX = 1; sideDistX = (mapX + 1.0f - px) * deltaDistX; }
            if (eyeY < 0) { stepY = -1; sideDistY = (py - mapY) * deltaDistY; }
            else { stepY = 1; sideDistY = (mapY + 1.0f - py) * deltaDistY; }

            // DDA Execution
            while (!hitWall && distanceToWall < depth) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                if (mapX >= 0 && mapX < MW && mapY >= 0 && mapY < MH) {
                    if (map[(int)mapX][(int)mapY] > 0) {
                        hitWall = true;
                        texID = map[(int)mapX][(int)mapY] % 20;
                    }
                } else {
                    break; // out of bounds
                }
            }

            // Distance and wall sample position
            float correctedDist;
            if (hitWall) {
                if (side == 0) {
                    distanceToWall = (mapX - px + (1 - stepX) / 2.0f) / eyeX;
                    sampleX = py + distanceToWall * eyeY;
                } else {
                    distanceToWall = (mapY - py + (1 - stepY) / 2.0f) / eyeY;
                    sampleX = px + distanceToWall * eyeX;
                }
                sampleX -= floorf(sampleX);
                correctedDist = distanceToWall * cosf(rayAngle - pa);
                if (correctedDist < 0.01f) correctedDist = 0.01f;
            } else {
                correctedDist = depth;
            }
            zbuf[x] = correctedDist;

            // Wall column boundaries (with pitch offset)
            int ceiling, floorLine;
            if (hitWall) {
                ceiling = (int)(horizon - (float)SH / correctedDist);
                floorLine = (int)(horizon + (float)SH / correctedDist);
            } else {
                // No wall hit: sky above horizon, floor below
                ceiling = (int)horizon;
                floorLine = (int)horizon;
            }

            // Sky texture x-coordinate (wraps around full 360 degrees)
            int skyX = (int)(fmodf(rayAngle / (2.0f * PI), 1.0f) * (float)SKY_W);
            if (skyX < 0) skyX += SKY_W;

            for (int y = 0; y < SH; y++) {
                if (y < ceiling) {
                    // === SKYBOX ===
                    // Sky Y: shifts with pitch so the horizon always aligns
                    int skyY = (int)((float)y - pitch);
                    if (skyY < 0) skyY = 0;
                    if (skyY >= SKY_H) skyY = SKY_H - 1;
                    pixels[y * SW + x] = skyTex[skyY][skyX];
                } 
                else if (hitWall && y >= ceiling && y <= floorLine) {
                    // === WALL TEXTURING ===
                    int wallH = floorLine - ceiling;
                    if (wallH < 1) wallH = 1;
                    float texY = (float)(y - ceiling) / (float)wallH;
                    int tx = (int)(sampleX * (float)TS) % TS;
                    int ty = (int)(texY * (float)TS) % TS;
                    if (tx < 0) tx += TS;
                    if (ty < 0) ty += TS;
                    
                    Color c = wallTex[texID][ty][tx];
                    
                    // Distance shading
                    float wallShade = 1.0f - (correctedDist / lightRadius);
                    if (wallShade < 0.0f) wallShade = 0.0f;
                    if (wallShade > 1.0f) wallShade = 1.0f;
                    c.r = (unsigned char)(c.r * wallShade);
                    c.g = (unsigned char)(c.g * wallShade);
                    c.b = (unsigned char)(c.b * wallShade);
                    
                    // Directional shading for horizontal walls
                    if (side == 1) { 
                        c.r = (unsigned char)(c.r * 0.7f); 
                        c.g = (unsigned char)(c.g * 0.7f); 
                        c.b = (unsigned char)(c.b * 0.7f);
                    }
                    pixels[y * SW + x] = c;
                } 
                else {
                    // === FLOOR TEXTURING (fixed floor casting) ===
                    float dy = (float)y - horizon;
                    if (dy < 0.001f) dy = 0.001f;
                    float rowDistance = (SH / 2.0f) / dy;
                    float floorX = px + rowDistance * eyeX;
                    float floorY = py + rowDistance * eyeY;
                    
                    int tx = (int)(floorX * TS) % TS;
                    int ty = (int)(floorY * TS) % TS;
                    if (tx < 0) tx += TS;
                    if (ty < 0) ty += TS;
                    
                    int fMapX = (int)floorX, fMapY = (int)floorY;
                    int tIdx = 0;
                    if (fMapX >= 0 && fMapX < MW && fMapY >= 0 && fMapY < MH)
                        tIdx = floorMap[fMapX][fMapY] % 6;

                    Color c = floorTex[tIdx][ty][tx];
                    
                    float floorShade = 1.0f - (rowDistance / lightRadius);
                    if (floorShade < 0.0f) floorShade = 0.0f;
                    c.r = (unsigned char)(c.r * floorShade);
                    c.g = (unsigned char)(c.g * floorShade);
                    c.b = (unsigned char)(c.b * floorShade);
                    
                    pixels[y * SW + x] = c;
                }
            }
        }

        UpdateTexture(screenTexture, pixels);

        // --- HUD ---
        BeginDrawing();
            ClearBackground(BLACK);
            DrawTexture(screenTexture, 0, 0, WHITE);

            if (showMap) {
                int mmSize = 4;
                DrawRectangle(SW - 220, 20, 200, 200, Fade(BLACK, 0.6f));
                for (int mx = -25; mx < 25; mx++) {
                    for (int my = -25; my < 25; my++) {
                        int cx = (int)px + mx;
                        int cy = (int)py + my;
                        if (cx >= 0 && cx < MW && cy >= 0 && cy < MH) {
                            if (map[cx][cy] > 0) {
                                DrawRectangle(SW - 120 + mx * mmSize, 120 + my * mmSize, mmSize, mmSize, DARKGRAY);
                            }
                        }
                    }
                }
                DrawCircle(SW - 120, 120, 4, RED);
                DrawLine(SW - 120, 120, SW - 120 + cosf(pa) * 15, 120 + sinf(pa) * 15, RED);
                DrawText("M: Toggle Minimap", SW - 210, 230, 12, GRAY);
            }

            if (volume_ui_timer > 0.0f) {
                DrawRectangle(20, SH - 40, 200, 20, Fade(BLACK, 0.5f));
                DrawRectangle(25, SH - 35, (int)(190 * music_volume), 10, GREEN);
                DrawText("Volume [ ]", 25, SH - 55, 12, WHITE);
            }

            DrawText("WASD: Move / Mouse: Look (up/down too!)", 20, 20, 16, RAYWHITE);
            DrawText("[ / ]: Volume  M: Map", 20, 40, 16, GRAY);
            DrawFPS(20, 65);
        EndDrawing();
    }

    UnloadTexture(screenTexture);
    UnloadImage(renderBuffer);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}