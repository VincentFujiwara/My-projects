#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define SW 1280
#define SH 720
#define MW 200
#define MH 200
#define TS 32
#define SKY_W 1024
#define SKY_H 720

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
static unsigned int global_audio_tick = 0;
static float music_volume = 0.2f;
static float volume_ui_timer = 0.0f;

void AudioInputCallback(void *buffer, unsigned int frames) {
	short *out = (short *)buffer;
	double bass_line[] = {
		N_E2, N_E2, N_G2, N_E2,
		N_A2, N_E2, N_Bb2, N_B2,
		N_E2, N_E2, N_D3, N_E3,
		N_Bb2, N_A2, N_G2, N_E2
	};
	
	for (unsigned int i = 0; i < frames; i++) {
		global_audio_time += 1.0 / (double)AUDIO_SAMPLE_RATE;
		global_audio_tick++;
		if (global_audio_tick > 100000000u) global_audio_tick = 0;
		
		int step = (int)((global_audio_tick / AUDIO_TICK_RATE) % 16);
		int tick = (int)(global_audio_tick % AUDIO_TICK_RATE);
		double step_t = (double)tick / (double)AUDIO_SAMPLE_RATE;
		
		float left = 0.0f, right = 0.0f;
		
		double freq = bass_line[step];
		double env = exp(-7.0 * step_t);
		double raw_bass = fmod(global_audio_time * freq, 1.0) * 2.0 - 1.0;
		double filter = sin(step_t * 28.0) * env * 1.5;
		raw_bass = (raw_bass + filter) * 4.0;
		if (raw_bass > 1.0) raw_bass = 1.0;
		if (raw_bass < -1.0) raw_bass = -1.0;
		
		float bass_out = (float)(raw_bass * env * 0.35);
		left += bass_out * 0.9f;
		right += bass_out * 1.1f;
		
		if (step % 4 == 0) {
			double k_env = exp(-22.0 * step_t);
			float kick = (float)(sin(2.0 * PI * 160.0 * exp(-40.0 * step_t) * step_t) * k_env);
			left += kick * 0.45f; right += kick * 0.45f;
		}
		if (step == 4 || step == 12) {
			double s_env = exp(-15.0 * step_t);
			float noise = (float)((global_audio_tick * 1103515245u + 12345u) % 65535u) / 65535.0f * 2.0f - 1.0f;
			float snare = (float)((sin(2.0 * PI * 165.0 * s_env * step_t) * 0.2f + noise * 0.4f) * s_env);
			left += snare * 0.4f; right += snare * 0.4f;
		}
		
		if (left > 1.0f) left = 1.0f;
		if (left < -1.0f) left = -1.0f;
		if (right > 1.0f) right = 1.0f;
		if (right < -1.0f) right = -1.0f;
		
		out[i * 2]     = (short)(left  * 32767.0f * music_volume);
		out[i * 2 + 1] = (short)(right * 32767.0f * music_volume);
	}
}

int map[MW][MH];
float px = 100.5f, py = 100.5f, pa = 0.0f;
float pitch = 0.0f;
float fov = 66.0f * (PI / 180.0f);
float depth = 64.0f;
bool showMap = true;

// Bob (покачивание при ходьбе)
float bobPhase = 0.0f;
float bobAmount = 0.0f;   // текущая амплитуда (плавно нарастает/затухает)

Color wallTex[20][TS][TS];
Color floorTex[6][TS][TS];
Color ceilTex[6][TS][TS];
Color skyTex[SKY_H][SKY_W];
float zbuf[SW];

int floorMap[MW][MH];
int ceilMap[MW][MH];

void Carve(int x1, int y1, int x2, int y2, int val) {
	if (x1 > x2) { int t=x1; x1=x2; x2=t; }
	if (y1 > y2) { int t=y1; y1=y2; y2=t; }
	for (int x = x1; x <= x2; x++)
		for (int y = y1; y <= y2; y++)
			if (x >= 0 && x < MW && y >= 0 && y < MH)
				map[x][y] = val;
}

void SetWalls(int x1, int y1, int x2, int y2, int val) {
	if (x1 > x2) { int t=x1; x1=x2; x2=t; }
	if (y1 > y2) { int t=y1; y1=y2; y2=t; }
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
	for (int x = 0; x < MW; x++)
		for (int y = 0; y < MH; y++) {
			map[x][y] = 1;
			floorMap[x][y] = 0;
			ceilMap[x][y] = 0;
		}
	
	Carve(80, 80, 120, 120, 0);
	Carve(98, 98, 102, 102, 2);
	for (int x = 80; x <= 120; x++)
		for (int y = 80; y <= 120; y++)
			if (map[x][y] == 0) { floorMap[x][y] = 1; ceilMap[x][y] = 1; }
	
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

void GenerateSky() {
	for (int y = 0; y < SKY_H; y++) {
		float t = (float)y / (float)SKY_H;
		for (int x = 0; x < SKY_W; x++) {
			Color c;
			if (t < 0.45f) {
				float st = t / 0.45f;
				c = (Color){(unsigned char)(8 + st * 25), (unsigned char)(12 + st * 30), (unsigned char)(30 + st * 65), 255};
			} else if (t < 0.55f) {
				float ht = (t - 0.45f) / 0.10f;
				c = (Color){(unsigned char)(33 + ht * 90), (unsigned char)(42 + ht * 45), (unsigned char)(95 - ht * 35), 255};
			} else {
				c = (Color){123, 87, 60, 255};
			}
			
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

void GenerateAllTextures() {
	for (int t = 0; t < 20; t++) {
		for (int y = 0; y < TS; y++) {
			for (int x = 0; x < TS; x++) {
				int type = t % 4;
				if (type == 0) {
					if (x % 16 == 0 || y % 8 == 0) {
						wallTex[t][y][x] = (Color){45, 45, 48, 255};
					} else {
						int rOffset = (x + y * 3 + t * 7) % 20;
						wallTex[t][y][x] = (Color){(unsigned char)(140 + rOffset), (unsigned char)(45 + rOffset / 2), 35, 255};
					}
				} else if (type == 1) {
					int noise = (x * 127 + y * 231 + t * 97) % 35;
					if ((x + y) % 16 == 0 || (x - y) % 16 == 0 || x == 0 || y == 0 || x == TS-1 || y == TS-1) {
						wallTex[t][y][x] = (Color){50, 50, 50, 255};
					} else {
						wallTex[t][y][x] = (Color){(unsigned char)(100 + noise), (unsigned char)(95 + noise / 2), (unsigned char)(85 + noise / 3), 255};
					}
				} else if (type == 2) {
					if (y % 8 == 0) {
						wallTex[t][y][x] = (Color){35, 25, 15, 255};
					} else {
						int noise = (x * 13 + y * 7 + t * 3) % 18;
						int grain = (x * 3) % 7;
						wallTex[t][y][x] = (Color){(unsigned char)(85 + noise - grain), (unsigned char)(60 + noise - grain), (unsigned char)(38 + noise - grain), 255};
					}
				} else {
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
	
	for (int t = 0; t < 6; t++) {
		for (int y = 0; y < TS; y++) {
			for (int x = 0; x < TS; x++) {
				if (t % 3 == 0) {
					if (x == 0 || y == 0 || x == TS-1 || y == TS-1) {
						floorTex[t][y][x] = (Color){15, 15, 18, 255};
					} else {
						bool tile = ((x / 16) + (y / 16)) % 2 == 0;
						unsigned char c = tile ? 65 : 45;
						floorTex[t][y][x] = (Color){c, c, (unsigned char)(c + 3), 255};
					}
				} else if (t % 3 == 1) {
					int noise = (x * 71 + y * 13) % 22;
					floorTex[t][y][x] = (Color){(unsigned char)(55 + noise), (unsigned char)(40 + noise), (unsigned char)(25 + noise), 255};
				} else {
					int noise = (x * 37 + y * 59) % 28;
					if ((x % 8 == 0) || (y % 8 == 0)) {
						floorTex[t][y][x] = (Color){30, 28, 25, 255};
					} else {
						floorTex[t][y][x] = (Color){(unsigned char)(78 + noise), (unsigned char)(73 + noise), (unsigned char)(68 + noise), 255};
					}
				}
			}
		}
	}
	
	for (int t = 0; t < 6; t++)
		for (int y = 0; y < TS; y++)
			for (int x = 0; x < TS; x++)
				ceilTex[t][y][x] = (Color){20, 20, 25, 255};
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
	Color *pixels = (Color *)renderBuffer.data;
	Texture2D screenTexture = LoadTextureFromImage(renderBuffer);
	
	SetTargetFPS(60);
	DisableCursor();
	
	while (!WindowShouldClose()) {
		float dt = GetFrameTime();
		if (dt > 0.05f) dt = 0.05f; // защита от "прыжков" при лагах
		
		if (volume_ui_timer > 0.0f) volume_ui_timer -= dt;
		
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
		
		Vector2 mouseDelta = GetMouseDelta();
		float mouseSensitivity = 0.0025f;
		pa += mouseDelta.x * mouseSensitivity;
		pitch -= mouseDelta.y * 0.08f;
		
		if (pitch > 150.0f) pitch = 150.0f;
		if (pitch < -150.0f) pitch = -150.0f;
		
		if (pa < 0.0f) pa += 2.0f * PI;
		if (pa > 2.0f * PI) pa -= 2.0f * PI;
		
		float moveSpeed = 6.0f * dt;
		float npx = px;
		float npy = py;
		float cosA = cosf(pa);
		float sinA = sinf(pa);
		
		bool moving = false;
		
		if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
			npx += cosA * moveSpeed;
			npy += sinA * moveSpeed;
			moving = true;
		}
		if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
			npx -= cosA * moveSpeed;
			npy -= sinA * moveSpeed;
			moving = true;
		}
		if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
			npx += sinA * moveSpeed;
			npy -= cosA * moveSpeed;
			moving = true;
		}
		if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
			npx -= sinA * moveSpeed;
			npy += cosA * moveSpeed;
			moving = true;
		}
		
		{
			int cx = (int)npx, cy = (int)py;
			if (cx >= 0 && cx < MW && cy >= 0 && cy < MH && map[cx][cy] == 0) px = npx;
		}
		{
			int cx = (int)px, cy = (int)npy;
			if (cx >= 0 && cx < MW && cy >= 0 && cy < MH && map[cx][cy] == 0) py = npy;
		}
		
		// --- BOB (покачивание) ---
		if (moving) {
			bobPhase += dt * 9.0f;   // частота шага
			if (bobPhase > 2.0f * PI) bobPhase -= 2.0f * PI;
			bobAmount += (1.0f - bobAmount) * dt * 6.0f; // плавно нарастает
		} else {
			bobAmount += (0.0f - bobAmount) * dt * 6.0f; // плавно затухает
		}
		
		// Вертикальное покачивание (в пикселях экрана)
		float bobVertical = sinf(bobPhase * 2.0f) * 7.0f * bobAmount;
		// Горизонтальное покачивание (лёгкое смещение вбок)
		float bobHorizontal = cosf(bobPhase) * 5.0f * bobAmount;
		
		float horizon = SH / 2.0f + pitch + bobVertical;
		if (horizon < 1.0f) horizon = 1.0f;
		if (horizon > SH - 1.0f) horizon = SH - 1.0f;
		
		float lightRadius = depth;
		
		// --- ПРАВИЛЬНАЯ КАМЕРА: dir + plane ---
		float dirX = cosf(pa);
		float dirY = sinf(pa);
		// Плоскость камеры перпендикулярна dir, длина = tan(fov/2)
		float planeLen = tanf(fov * 0.5f);
		float planeX = -dirY * planeLen;
		float planeY =  dirX * planeLen;
		
		// Вертикальный сдвиг пола из-за pitch+bob: posZ смещается
		float posZ = 0.5f * (float)SH;
		
		for (int x = 0; x < SW; x++) {
			float camX = 2.0f * ((float)x / (float)SW) - 1.0f; // -1..+1
			float rayDirX = dirX + planeX * camX;
			float rayDirY = dirY + planeY * camX;
			
			// Горизонтальный bob: сдвигаем camX на bobHorizontal/SW
			// (простой вариант — сдвиг луча)
			// --- DDA ---
			int mapX = (int)px;
			int mapY = (int)py;
			
			float deltaDistX = (rayDirX == 0.0f) ? 1e30f : fabsf(1.0f / rayDirX);
			float deltaDistY = (rayDirY == 0.0f) ? 1e30f : fabsf(1.0f / rayDirY);
			
			float sideDistX, sideDistY;
			int stepX, stepY;
			
			if (rayDirX < 0) { stepX = -1; sideDistX = (px - mapX) * deltaDistX; }
			else             { stepX =  1; sideDistX = (mapX + 1.0f - px) * deltaDistX; }
			if (rayDirY < 0) { stepY = -1; sideDistY = (py - mapY) * deltaDistY; }
			else             { stepY =  1; sideDistY = (mapY + 1.0f - py) * deltaDistY; }
			
			bool hit = false;
			int side = 0;
			int texID = 1;
			float perpDist = 0.0f;
			
			// DDA без sqrt — оптимизация
			while (!hit) {
				if (sideDistX < sideDistY) {
					sideDistX += deltaDistX;
					mapX += stepX;
					side = 0;
				} else {
					sideDistY += deltaDistY;
					mapY += stepY;
					side = 1;
				}
				if (mapX < 0 || mapX >= MW || mapY < 0 || mapY >= MH) {
					hit = true;
					perpDist = depth;
					texID = 1;
					break;
				}
				if (map[mapX][mapY] > 0) {
					hit = true;
					texID = map[mapX][mapY] % 20;
				}
			}
			
			float sampleX;
			if (side == 0) {
				perpDist = (mapX - px + (1 - stepX) / 2.0f) / rayDirX;
				sampleX = py + perpDist * rayDirY;
			} else {
				perpDist = (mapY - py + (1 - stepY) / 2.0f) / rayDirY;
				sampleX = px + perpDist * rayDirX;
			}
			if (perpDist < 0.01f) perpDist = 0.01f;
			sampleX -= floorf(sampleX);
			zbuf[x] = perpDist;
			
			int lineHeight = (int)((float)SH / perpDist);
			int drawStart = (int)(horizon - (float)lineHeight * 0.5f);
			int drawEnd   = (int)(horizon + (float)lineHeight * 0.5f);
			if (drawStart < 0) drawStart = 0;
			if (drawEnd > SH)  drawEnd = SH;
			
			int tx = (int)(sampleX * (float)TS);
			if (tx < 0) tx = 0;
			if (tx >= TS) tx = TS - 1;
			
			float wallShade = 1.0f - (perpDist / lightRadius) * 0.85f;
			if (wallShade < 0.2f) wallShade = 0.2f;
			float sideShade = (side == 1) ? 0.7f : 1.0f;
			float totalShade = wallShade * sideShade;
			
			// === СТЕНА (по вертикали, без ветвлений на пиксель) ===
			int y;
			// рисуем только стену, пол и небо — отдельно
			for (y = drawStart; y < drawEnd; y++) {
				float texY = (float)(y - drawStart) / (float)lineHeight;
				int ty = (int)(texY * (float)TS);
				if (ty < 0) ty = 0;
				if (ty >= TS) ty = TS - 1;
				
				Color c = wallTex[texID][ty][tx];
				c.r = (unsigned char)(c.r * totalShade);
				c.g = (unsigned char)(c.g * totalShade);
				c.b = (unsigned char)(c.b * totalShade);
				pixels[y * SW + x] = c;
			}
			
			// === НЕБО (выше стены) ===
			float normAngle = atan2f(rayDirY, rayDirX);
			if (normAngle < 0) normAngle += 2.0f * PI;
			int skyX = (int)(normAngle / (2.0f * PI) * (float)SKY_W) % SKY_W;
			if (skyX < 0) skyX += SKY_W;
			
			for (y = 0; y < drawStart; y++) {
				int skyY = (int)((float)y - pitch - bobVertical);
				if (skyY < 0) skyY = 0;
				if (skyY >= SKY_H) skyY = SKY_H - 1;
				pixels[y * SW + x] = skyTex[skyY][skyX];
			}
			
			// === ПОЛ (ниже стены) ===
			// Правильный floor casting через camX + plane
			for (y = drawEnd; y < SH; y++) {
				float p = (float)y - horizon;
				if (p < 1.0f) p = 1.0f;
				
				float rowDistance = posZ / p;
				if (rowDistance > depth * 2.0f) rowDistance = depth * 2.0f;
				
				float floorX = px + rayDirX * rowDistance;
				float floorY = py + rayDirY * rowDistance;
				
				int cellX = (int)floorX;
				int cellY = (int)floorY;
				int tIdx = 0;
				if (cellX >= 0 && cellX < MW && cellY >= 0 && cellY < MH)
					tIdx = floorMap[cellX][cellY] % 6;
				
				int ftx = (int)((floorX - cellX) * (float)TS) & (TS - 1);
				int fty = (int)((floorY - cellY) * (float)TS) & (TS - 1);
				if (ftx < 0) ftx += TS;
				if (fty < 0) fty += TS;
				
				Color c = floorTex[tIdx][fty][ftx];
				float shade = 1.0f - (rowDistance / lightRadius) * 0.85f;
				if (shade < 0.2f) shade = 0.2f;
				c.r = (unsigned char)(c.r * shade);
				c.g = (unsigned char)(c.g * shade);
				c.b = (unsigned char)(c.b * shade);
				pixels[y * SW + x] = c;
			}
		}
		
		UpdateTexture(screenTexture, pixels);
		
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
			DrawLine(SW - 120, 120,
					 SW - 120 + (int)(cosf(pa) * 15),
					 120 + (int)(sinf(pa) * 15), RED);
			DrawText("M: Toggle Minimap", SW - 210, 230, 12, GRAY);
		}
		
		if (volume_ui_timer > 0.0f) {
			DrawRectangle(20, SH - 40, 200, 20, Fade(BLACK, 0.5f));
			DrawRectangle(25, SH - 35, (int)(190 * music_volume), 10, GREEN);
			DrawText("Volume [ ]", 25, SH - 55, 12, WHITE);
		}
		
		DrawText("WASD: Move / Mouse: Look", 20, 20, 16, RAYWHITE);
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
