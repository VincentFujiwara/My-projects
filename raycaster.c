#include "raylib.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <omp.h> // Для многопоточности

int cpu_threads = 4;

// ------------------------------------------------------------
//  ОСНОВНЫЕ КОНСТАНТЫ
// ------------------------------------------------------------
#define SW 1280         // Ширина рендера
#define SH 720          // Высота рендера
#define MW 160          // Ширина карты
#define MH 160          // Высота карты
#define TS 32           // Размер текстуры
#define SKY_W 1024
#define SKY_H 720

// Высоты (Потолки подняты, чтобы не застревать на ящиках)
#define Z_FLOOR      0.0f
#define Z_STEP       0.125f
#define Z_CRATE      0.75f  // Высота ящика (чтобы запрыгивать)
#define Z_CEIL       2.0f
#define Z_LOWCEIL    1.5f
#define Z_HIGHCEIL   2.5f   // Высокий потолок

// Игрок
#define PLAYER_RADIUS 0.25f
#define EYE_HEIGHT    0.5f
#define PLAYER_HEIGHT 0.9f

// Физика
#define GRAVITY       -12.0f
#define JUMP_SPEED    5.0f
#define STEP_UP       0.15f

// Аудио (только для старого синтеза, если понадобится)
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BPM 150
#define AUDIO_TICK_RATE (AUDIO_SAMPLE_RATE * 60 / (AUDIO_BPM * 4))

// ------------------------------------------------------------
//  ТИПЫ КЛЕТОК И СТРУКТУРЫ
// ------------------------------------------------------------
typedef enum {
	CELL_EMPTY = 0,     // Пусто (пол/потолок)
	CELL_WALL,          // Обычная стена
	CELL_DOOR,          // Дверь
	CELL_WINDOW,        // Окно
	CELL_CRATE,         // Ящик
	CELL_LOW_WALL       // Низкая стена
} CellType;

typedef struct {
	uint8_t  type;      // CellType
	uint8_t  wallTex;   // Индекс текстуры стены
	uint8_t  floorTex;  // Индекс текстуры пола
	uint8_t  ceilTex;   // Индекс текстуры потолка
	float    floorZ;    // Высота пола
	float    ceilZ;     // Высота потолка
} Cell;

typedef struct {
	float x, y, z;
	float vx, vy, vz;
	float pa, pitch;
	bool  onGround;
	float bobPhase, bobAmount;
	float stamina;      // <--- ДОБАВИТЬ
	float maxStamina;   // <--- ДОБАВИТЬ
} Player;

// ------------------------------------------------------------
//  ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ------------------------------------------------------------
Cell map[MW][MH];
Player player;

float fov       = 66.0f * (PI / 180.0f);
float depth     = 64.0f;
bool showMap = true;

float zbuf[SW];

// Буферы для двухпроходного рендера
int   drawStartBuf[SW];
int   drawEndBuf[SW];
int   texIDBuf[SW];
int   txBuf[SW];
int   sideBuf[SW];
float perpBuf[SW];
int   skyXBuf[SW];
int   topZBuf[SW];    
int   botZBuf[SW];    
int   shadeFloorBuf[SH];

// Текстуры
#define N_WALL_TEX  8
#define N_FLOOR_TEX 8
#define N_CEIL_TEX  4

Color wallTex[N_WALL_TEX][TS][TS];
Color floorTex[N_FLOOR_TEX][TS][TS];
Color ceilTex[N_CEIL_TEX][TS][TS];
Color skyTex[SKY_H][SKY_W];

// ------------------------------------------------------------
//  СЕКТОР 2: МАТЕМАТИКА ДЛЯ ТЕКСТУР (Из Dust2)
// ------------------------------------------------------------

// Хеш-функция для псевдослучайных чисел без внешнего состояния
static inline unsigned int hash2(unsigned int x, unsigned int y) {
	unsigned int h = x * 374761393u + y * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

// Плавный шум для деталей
static inline float noise2(float x, float y) {
	int xi = (int)floorf(x);
	int yi = (int)floorf(y);
	float xf = x - xi;
	float yf = y - yi;
	float u = xf * xf * (3.0f - 2.0f * xf);
	float v = yf * yf * (3.0f - 2.0f * yf);
	
	float a = (float)(hash2(xi,     yi    ) & 0xFFFF) / 65535.0f;
	float b = (float)(hash2(xi + 1, yi    ) & 0xFFFF) / 65535.0f;
	float c = (float)(hash2(xi,     yi + 1) & 0xFFFF) / 65535.0f;
	float d = (float)(hash2(xi + 1, yi + 1) & 0xFFFF) / 65535.0f;
	
	return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
}

// Фрактальный шум (несколько октав) для органичных текстур
static float fbm(float x, float y, int octaves) {
	float sum = 0.0f, amp = 0.5f, freq = 1.0f;
	for (int i = 0; i < octaves; i++) {
		sum += noise2(x * freq, y * freq) * amp;
		freq *= 2.0f;
		amp  *= 0.5f;
	}
	return sum;
}

// Смешать два цвета (для плавных переходов)
static inline Color MixColor(Color a, Color b, float t) {
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return (Color){
		(unsigned char)(a.r + (b.r - a.r) * t),
		(unsigned char)(a.g + (b.g - a.g) * t),
		(unsigned char)(a.b + (b.b - a.b) * t),
		255
	};
}

// ------------------------------------------------------------
//  СЕКТОР 3: ТЕКСТУРЫ СТЕН (Из Dust2) - Часть 1
// ------------------------------------------------------------

// Текстура 0: Песочный кирпич
static void GenerateWall_SandBrick(Color tex[TS][TS]) {
	Color mortar   = (Color){168, 142, 102, 255};
	Color brickA   = (Color){205, 175, 130, 255};
	Color brickB   = (Color){190, 158, 112, 255};
	
	int brickH = 8;
	int brickW = 16;
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			int row = y / brickH;
			int offset = (row % 2) * (brickW / 2);
			int bx = (x + offset) % brickW;
			int by = y % brickH;
			
			bool isMortar = (bx == 0 || by == 0);
			
			if (isMortar) {
				float n = fbm(x * 0.3f, y * 0.3f, 2);
				unsigned char v = (unsigned char)(150 + n * 30);
				tex[y][x] = (Color){v, (unsigned char)(v * 0.82f), (unsigned char)(v * 0.58f), 255};
			} else {
				int variant = ((bx * 7 + by * 13 + row * 17) % 5);
				Color base = (variant < 3) ? brickA : brickB;
				float n = fbm(x * 0.4f + row * 5.0f, y * 0.4f, 2);
				float shade = 0.85f + n * 0.3f;
				
				if (by == 1) shade *= 0.92f;
				if (bx == 1) shade *= 0.95f;
				if (by == brickH - 1) shade *= 1.05f;
				
				tex[y][x] = (Color){
					(unsigned char)(base.r * shade),
					(unsigned char)(base.g * shade),
					(unsigned char)(base.b * shade),
					255
				};
			}
		}
	}
}

// Текстура 1: Облупленная штукатурка
static void GenerateWall_Stucco(Color tex[TS][TS]) {
	Color base = (Color){196, 168, 122, 255};
	Color dark = (Color){140, 115, 78, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.25f, y * 0.25f, 3);
			float cracks = fbm(x * 0.8f, y * 0.8f, 2);
			
			Color c = MixColor(dark, base, n);
			
			if (cracks > 0.72f) {
				float t = (cracks - 0.72f) / 0.28f;
				c = MixColor(c, (Color){60, 45, 30, 255}, t * 0.9f);
			}
			
			if (n > 0.75f) {
				c = MixColor(c, (Color){220, 200, 160, 255}, (n - 0.75f) * 1.5f);
			}
			
			tex[y][x] = c;
		}
	}
}

// Текстура 2: Каменная кладка (туннели)
static void GenerateWall_Stone(Color tex[TS][TS]) {
	Color mortar = (Color){88, 78, 62, 255};
	Color stoneA = (Color){130, 118, 96, 255};
	Color stoneB = (Color){110, 98, 80, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			int row = y / 8;
			int offset = (row % 2) * 8;
			int bx = (x + offset) % 16;
			int by = y % 8;
			
			bool isMortar = (bx < 1 || by < 1);
			
			if (isMortar) {
				tex[y][x] = mortar;
			} else {
				int variant = (hash2(x, y + row * 17) & 3);
				Color base = (variant < 2) ? stoneA : stoneB;
				float n = fbm(x * 0.5f + row * 3.0f, y * 0.5f, 2);
				float shade = 0.85f + n * 0.3f;
				
				tex[y][x] = (Color){
					(unsigned char)(base.r * shade),
					(unsigned char)(base.g * shade),
					(unsigned char)(base.b * shade),
					255
				};
			}
		}
	}
}

// ------------------------------------------------------------
//  СЕКТОР 4: ТЕКСТУРЫ СТЕН (Из Dust2) - Часть 2
// ------------------------------------------------------------

// Текстура 3: Деревянный ящик
static void GenerateWall_WoodCrate(Color tex[TS][TS]) {
	Color plankLight = (Color){168, 122, 72, 255};
	Color plankDark  = (Color){120, 82, 45, 255};
	Color edgeDark   = (Color){70, 45, 22, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			bool isEdge = (x < 2 || x >= TS - 2 || y < 2 || y >= TS - 2);
			bool isBrace = (abs((x - TS / 2) - (y - TS / 2)) < 2) ||
			(abs((x - TS / 2) + (y - TS / 2) - TS / 2) < 2);
			
			if (isEdge) {
				tex[y][x] = edgeDark;
			} else if (isBrace) {
				float n = fbm(x * 0.4f, y * 0.4f, 2);
				Color c = MixColor(plankDark, edgeDark, 0.4f);
				c.r = (unsigned char)(c.r * (0.85f + n * 0.3f));
				c.g = (unsigned char)(c.g * (0.85f + n * 0.3f));
				c.b = (unsigned char)(c.b * (0.85f + n * 0.3f));
				tex[y][x] = c;
			} else {
				int plank = y / 8;
				Color base = (plank % 2 == 0) ? plankLight : plankDark;
				float grain = sinf((float)x * 0.6f + fbm(x * 0.3f, y * 0.5f, 2) * 6.0f) * 0.5f + 0.5f;
				float shade = 0.8f + grain * 0.4f;
				
				if (x >= 4 && x <= 6 && y >= 4 && y <= 6) shade *= 0.5f;
				if (x >= TS-7 && x <= TS-5 && y >= 4 && y <= 6) shade *= 0.5f;
				if (x >= 4 && x <= 6 && y >= TS-7 && y <= TS-5) shade *= 0.5f;
				if (x >= TS-7 && x <= TS-5 && y >= TS-7 && y <= TS-5) shade *= 0.5f;
				
				tex[y][x] = (Color){
					(unsigned char)(base.r * shade),
					(unsigned char)(base.g * shade),
					(unsigned char)(base.b * shade),
					255
				};
			}
		}
	}
}

// Текстура 4: Металлическая дверь
static void GenerateWall_MetalDoor(Color tex[TS][TS]) {
	Color metalLight = (Color){130, 130, 135, 255};
	Color metalDark  = (Color){75, 75, 80, 255};
	Color rust       = (Color){130, 75, 40, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			bool isFrame = (x < 2 || x >= TS - 2 || y < 2 || y >= TS - 2);
			
			if (isFrame) {
				tex[y][x] = metalDark;
			} else {
				float band = ((x / 4) % 2 == 0) ? 1.05f : 0.9f;
				float n = fbm(x * 0.6f, y * 0.6f, 2);
				float shade = band * (0.8f + n * 0.3f);
				Color c = MixColor(metalDark, metalLight, n);
				
				float rustNoise = fbm(x * 0.2f + 10.0f, y * 0.2f + 10.0f, 3);
				if (rustNoise > 0.65f) {
					c = MixColor(c, rust, (rustNoise - 0.65f) * 1.8f);
				}
				
				if (x >= TS/2 - 3 && x <= TS/2 + 3 && y >= TS/2 - 2 && y <= TS/2 + 2) {
					c = (Color){50, 50, 55, 255};
				}
				
				tex[y][x] = (Color){
					(unsigned char)(c.r * shade),
					(unsigned char)(c.g * shade),
					(unsigned char)(c.b * shade),
					255
				};
			}
		}
	}
}

// Текстура 5: Металлическая бочка
static void GenerateWall_Barrel(Color tex[TS][TS]) {
	Color base = (Color){90, 110, 130, 255};
	Color highlight = (Color){150, 170, 190, 255};
	Color rust = (Color){140, 80, 35, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float dx = (float)x - TS / 2.0f;
			float dv = 1.0f - fabsf(dx) / (TS / 2.0f);
			dv = dv * dv;
			
			Color c = MixColor(base, highlight, dv * 0.7f);
			
			if (y % 8 == 0 || y % 8 == 1) {
				c = MixColor(c, (Color){40, 40, 45, 255}, 0.7f);
			}
			
			float n = fbm(x * 0.35f + 20.0f, y * 0.35f + 20.0f, 3);
			if (n > 0.6f) {
				c = MixColor(c, rust, (n - 0.6f) * 1.5f);
			}
			
			float metalNoise = (hash2(x, y) & 0xFF) / 255.0f;
			c.r = (unsigned char)(c.r * (0.92f + metalNoise * 0.15f));
			c.g = (unsigned char)(c.g * (0.92f + metalNoise * 0.15f));
			c.b = (unsigned char)(c.b * (0.92f + metalNoise * 0.15f));
			
			tex[y][x] = c;
		}
	}
}

// Текстура 6: Тёмная штукатурка (туннели)
static void GenerateWall_DarkStucco(Color tex[TS][TS]) {
	Color base = (Color){120, 100, 72, 255};
	Color dark = (Color){75, 60, 42, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.3f + 5.0f, y * 0.3f + 5.0f, 3);
			Color c = MixColor(dark, base, n);
			
			float dirt = fbm(x * 0.15f, y * 0.15f, 2);
			if (dirt > 0.7f) {
				c = MixColor(c, (Color){50, 40, 28, 255}, (dirt - 0.7f) * 2.0f);
			}
			
			tex[y][x] = c;
		}
	}
}

// Текстура 7: Кирпич с меткой (акценты)
static void GenerateWall_BrickMarked(Color tex[TS][TS]) {
	GenerateWall_SandBrick(tex);
	
	for (int y = 10; y < 22; y++) {
		for (int x = 8; x < 24; x++) {
			if ((x + y) % 3 == 0 && x > 10 && x < 22 && y > 12 && y < 20) {
				tex[y][x] = (Color){140, 45, 35, 255};
			}
		}
	}
}

// ------------------------------------------------------------
//  СЕКТОР 5: ТЕКСТУРЫ ПОЛОВ (Из Dust2) - Часть 1
// ------------------------------------------------------------

// Пол 0: Песок (улицы)
static void GenerateFloor_Sand(Color tex[TS][TS]) {
	Color sandLight = (Color){200, 175, 130, 255};
	Color sandDark  = (Color){160, 135, 95, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.4f, y * 0.4f, 3);
			Color c = MixColor(sandDark, sandLight, n);
			
			if ((hash2(x, y) & 0x7F) < 3) {
				c = MixColor(c, (Color){120, 100, 70, 255}, 0.6f);
			}
			
			tex[y][x] = c;
		}
	}
}

// Пол 1: Песчаная плитка (A site, B site)
static void GenerateFloor_SandTile(Color tex[TS][TS]) {
	Color tileLight = (Color){210, 185, 140, 255};
	Color tileDark  = (Color){170, 145, 105, 255};
	Color grout     = (Color){110, 90, 65, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			bool isGrout = (x % 16 == 0 || y % 16 == 0);
			
			if (isGrout) {
				tex[y][x] = grout;
			} else {
				int tileX = x / 16;
				int tileY = y / 16;
				float variance = ((hash2(tileX, tileY) & 0xFF) / 255.0f - 0.5f) * 0.2f;
				
				float n = fbm(x * 0.5f, y * 0.5f, 2);
				Color c = MixColor(tileDark, tileLight, n + variance);
				
				tex[y][x] = c;
			}
		}
	}
}

// Пол 2: Каменная плитка (туннели)
static void GenerateFloor_StoneTile(Color tex[TS][TS]) {
	Color stone = (Color){95, 85, 70, 255};
	Color stoneLight = (Color){125, 112, 92, 255};
	Color grout = (Color){50, 45, 38, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			bool isGrout = (x % 8 == 0 || y % 8 == 0);
			
			if (isGrout) {
				tex[y][x] = grout;
			} else {
				int tx = x / 8;
				int ty = y / 8;
				float v = ((hash2(tx + 100, ty + 100) & 0xFF) / 255.0f);
				float n = fbm(x * 0.6f, y * 0.6f, 2);
				Color c = MixColor(stone, stoneLight, n * 0.5f + v * 0.5f);
				
				if (n < 0.3f) c = MixColor(c, (Color){60, 50, 38, 255}, 0.5f);
				
				tex[y][x] = c;
			}
		}
	}
}

// Пол 3: Гравий / щебёнка (дороги)
static void GenerateFloor_Gravel(Color tex[TS][TS]) {
	Color base = (Color){140, 120, 90, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.7f, y * 0.7f, 3);
			Color c = base;
			
			if ((hash2(x, y) & 0x3F) < 6) {
				unsigned int h = hash2(x, y);
				int shade = (h >> 8) & 0x3F;
				c = (Color){
					(unsigned char)(100 + shade),
					(unsigned char)(90 + shade),
					(unsigned char)(75 + shade),
					255
				};
			} else {
				c = MixColor(base, (Color){180, 160, 125, 255}, n);
			}
			
			tex[y][x] = c;
		}
	}
}

// ------------------------------------------------------------
//  СЕКТОР 6: ТЕКСТУРЫ ПОЛОВ (Из Dust2) - Часть 2
// ------------------------------------------------------------

// Пол 4: Деревянный пол (внутри зданий)
static void GenerateFloor_Wood(Color tex[TS][TS]) {
	Color wood = (Color){145, 100, 60, 255};
	Color woodDark = (Color){100, 68, 40, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			int plank = y / 4;
			Color base = (plank % 2 == 0) ? wood : woodDark;
			
			float grain = sinf(x * 0.8f + fbm(x * 0.4f, y * 0.6f, 2) * 5.0f) * 0.5f + 0.5f;
			float shade = 0.85f + grain * 0.3f;
			
			if (y % 4 == 0) shade *= 0.5f;
			
			tex[y][x] = (Color){
				(unsigned char)(base.r * shade),
				(unsigned char)(base.g * shade),
				(unsigned char)(base.b * shade),
				255
			};
		}
	}
}

// Пол 5: Песок с камнями (улицы, мид)
static void GenerateFloor_SandyStones(Color tex[TS][TS]) {
	GenerateFloor_Sand(tex);
	
	for (int i = 0; i < 20; i++) {
		unsigned int h = hash2(i * 17, i * 31);
		int cx = (h >> 0) & 0x1F;
		int cy = (h >> 8) & 0x1F;
		int r  = 1 + ((h >> 16) & 1);
		
		for (int dy = -r; dy <= r; dy++) {
			for (int dx = -r; dx <= r; dx++) {
				int x = cx + dx;
				int y = cy + dy;
				if (x >= 0 && x < TS && y >= 0 && y < TS) {
					if (dx * dx + dy * dy <= r * r) {
						unsigned char v = (unsigned char)(90 + ((h >> 20) & 0x3F));
						tex[y][x] = (Color){v, (unsigned char)(v * 0.9f), (unsigned char)(v * 0.75f), 255};
					}
				}
			}
		}
	}
}

// Пол 6: Земля / грязь
static void GenerateFloor_Dirt(Color tex[TS][TS]) {
	Color dirt = (Color){75, 60, 42, 255};
	Color dirtLight = (Color){110, 88, 62, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.5f, y * 0.5f, 3);
			Color c = MixColor(dirt, dirtLight, n);
			
			float dark = fbm(x * 0.2f + 30.0f, y * 0.2f, 2);
			if (dark > 0.7f) {
				c = MixColor(c, (Color){40, 30, 20, 255}, (dark - 0.7f) * 2.0f);
			}
			
			tex[y][x] = c;
		}
	}
}

// Пол 7: Красная плитка (акценты)
static void GenerateFloor_RedTile(Color tex[TS][TS]) {
	Color tile = (Color){150, 55, 40, 255};
	Color tileDark = (Color){110, 35, 25, 255};
	Color grout = (Color){60, 25, 18, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			bool isGrout = (x % 16 == 0 || y % 16 == 0);
			if (isGrout) {
				tex[y][x] = grout;
			} else {
				float n = fbm(x * 0.4f, y * 0.4f, 2);
				tex[y][x] = MixColor(tileDark, tile, n);
			}
		}
	}
}

// ------------------------------------------------------------
//  СЕКТОР 7: ПОТОЛКИ И НЕБО
// ------------------------------------------------------------

// Потолок 0: Открытое небо (заглушка для внутренних помещений)
static void GenerateCeil_Sky(Color tex[TS][TS]) {
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float t = (float)y / TS;
			tex[y][x] = (Color){
				(unsigned char)(110 + t * 60),
				(unsigned char)(150 + t * 50),
				(unsigned char)(200 + t * 30),
				255
			};
		}
	}
}

// Потолок 1: Деревянные балки (внутри зданий)
static void GenerateCeil_WoodBeams(Color tex[TS][TS]) {
	Color beam = (Color){85, 60, 35, 255};
	Color panel = (Color){60, 42, 28, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			if (x % 8 == 0 || y % 8 == 0) {
				tex[y][x] = beam;
			} else {
				float n = fbm(x * 0.4f, y * 0.4f, 2);
				tex[y][x] = MixColor(panel, (Color){80, 60, 40, 255}, n);
			}
		}
	}
}

// Потолок 2: Каменный (туннели)
static void GenerateCeil_Stone(Color tex[TS][TS]) {
	Color stone = (Color){70, 62, 50, 255};
	Color stoneLight = (Color){95, 85, 68, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.5f, y * 0.5f, 3);
			tex[y][x] = MixColor(stone, stoneLight, n);
		}
	}
}

// Потолок 3: Песчаный (открытые зоны)
static void GenerateCeil_Sand(Color tex[TS][TS]) {
	Color base = (Color){180, 155, 115, 255};
	Color dark = (Color){130, 110, 80, 255};
	
	for (int y = 0; y < TS; y++) {
		for (int x = 0; x < TS; x++) {
			float n = fbm(x * 0.3f, y * 0.3f, 2);
			tex[y][x] = MixColor(dark, base, n);
		}
	}
}

// Небо со звёздами и луной (БЕЗ КОРИЧНЕВОГО ЦВЕТА)
static void GenerateSky(void) {
	for (int y = 0; y < SKY_H; y++) {
		float t = (float)y / (float)SKY_H;
		for (int x = 0; x < SKY_W; x++) {
			Color c;
			if (t < 0.45f) {
				float st = t / 0.45f;
				c = (Color){(unsigned char)(8 + st * 25), (unsigned char)(12 + st * 30), (unsigned char)(30 + st * 65), 255};
			} else {
				// Плавный переход в тёмно-синий вместо коричневого
				float ht = (t - 0.45f) / 0.55f;
				c = (Color){(unsigned char)(33 + ht * 15), (unsigned char)(42 + ht * 15), (unsigned char)(95 - ht * 20), 255};
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

// Собираем все текстуры в кучу
void GenerateAllTextures(void) {
	// Стены (8)
	GenerateWall_SandBrick  (wallTex[0]);
	GenerateWall_Stucco     (wallTex[1]);
	GenerateWall_Stone      (wallTex[2]);
	GenerateWall_WoodCrate  (wallTex[3]);
	GenerateWall_MetalDoor  (wallTex[4]);
	GenerateWall_Barrel     (wallTex[5]);
	GenerateWall_DarkStucco (wallTex[6]);
	GenerateWall_BrickMarked(wallTex[7]);
	
	// Полы (8)
	GenerateFloor_Sand        (floorTex[0]);
	GenerateFloor_SandTile    (floorTex[1]);
	GenerateFloor_StoneTile   (floorTex[2]);
	GenerateFloor_Gravel      (floorTex[3]);
	GenerateFloor_Wood        (floorTex[4]);
	GenerateFloor_SandyStones (floorTex[5]);
	GenerateFloor_Dirt        (floorTex[6]);
	GenerateFloor_RedTile     (floorTex[7]);
	
	// Потолки (4)
	GenerateCeil_Sky      (ceilTex[0]);
	GenerateCeil_WoodBeams(ceilTex[1]);
	GenerateCeil_Stone    (ceilTex[2]);
	GenerateCeil_Sand     (ceilTex[3]);
	
	// Небо
	GenerateSky();
}

// ------------------------------------------------------------
//  СЕКТОР 8: АУДИО (Глобальные настройки)
// -----------------------------------------------------------
static float music_volume = 0.5f; // Громкость (0.0 - 1.0)
static float volume_ui_timer = 0.0f;
static float stamina_ui_timer = 0.0f;

// ------------------------------------------------------------
//  СЕКТОР 9: КАРТА ИЗ ПРОЕКТА 1 (С текстурами Dust2)
// ------------------------------------------------------------

static void SetCell(int x, int y, CellType type, int wtex, int ftex, int ctex, float fz, float cz) {
	if (x < 0 || x >= MW || y < 0 || y >= MH) return;
	map[x][y].type     = (uint8_t)type;
	map[x][y].wallTex  = (uint8_t)wtex;
	map[x][y].floorTex = (uint8_t)ftex;
	map[x][y].ceilTex  = (uint8_t)ctex;
	map[x][y].floorZ   = fz;
	map[x][y].ceilZ    = cz;
}

static void Carve(int x1, int y1, int x2, int y2, int ftex, int ctex, float fz, float cz) {
	if (x1 > x2) { int t=x1; x1=x2; x2=t; }
	if (y1 > y2) { int t=y1; y1=y2; y2=t; }
	for (int x = x1; x <= x2; x++)
		for (int y = y1; y <= y2; y++)
			SetCell(x, y, CELL_EMPTY, 0, ftex, ctex, fz, cz);
}

static void SetWalls(int x1, int y1, int x2, int y2, int wtex, float fz, float cz) {
	if (x1 > x2) { int t=x1; x1=x2; x2=t; }
	if (y1 > y2) { int t=y1; y1=y2; y2=t; }
	for (int x = x1; x <= x2; x++) {
		if (map[x][y1].type != CELL_EMPTY) SetCell(x, y1, CELL_WALL, wtex, 0, 0, fz, cz);
		if (map[x][y2].type != CELL_EMPTY) SetCell(x, y2, CELL_WALL, wtex, 0, 0, fz, cz);
	}
	for (int y = y1; y <= y2; y++) {
		if (map[x1][y].type != CELL_EMPTY) SetCell(x1, y, CELL_WALL, wtex, 0, 0, fz, cz);
		if (map[x2][y].type != CELL_EMPTY) SetCell(x2, y, CELL_WALL, wtex, 0, 0, fz, cz);
	}
}

void GenMap(void) {
	// Всё — каменная скала
	for (int x = 0; x < MW; x++)
		for (int y = 0; y < MH; y++)
			SetCell(x, y, CELL_WALL, 6, 0, 0, Z_FLOOR, Z_HIGHCEIL);
	
	// 1. Центральный зал (Песок, открытое небо)
	Carve(80, 80, 120, 120, 0, 0, Z_FLOOR, Z_HIGHCEIL);
	SetWalls(80, 80, 120, 120, 0, Z_FLOOR, Z_HIGHCEIL); 
	// Ящик по центру
	SetCell(100, 100, CELL_CRATE, 3, 1, 0, Z_FLOOR, Z_CRATE);
	
	// 2. Коридоры (Гравий)
	Carve(20, 95, 80, 105, 3, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(120, 95, 180, 105, 3, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(95, 20, 105, 80, 3, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(95, 120, 105, 180, 3, 3, Z_FLOOR, Z_HIGHCEIL);
	
	// 3. Левая комната (Песчаная плитка, штукатурка)
	Carve(25, 85, 75, 115, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	SetWalls(25, 85, 75, 115, 1, Z_FLOOR, Z_HIGHCEIL);
	Carve(30, 90, 70, 92, 1, 3, Z_FLOOR, Z_HIGHCEIL); 
	Carve(30, 108, 70, 110, 1, 3, Z_FLOOR, Z_HIGHCEIL); 
	Carve(28, 97, 32, 103, 1, 3, Z_FLOOR, Z_HIGHCEIL); 
	SetCell(40, 100, CELL_CRATE, 3, 1, 0, Z_FLOOR, Z_CRATE);
	
	// 4. Правая комната (Песчаная плитка, кирпич)
	Carve(125, 85, 175, 115, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	SetWalls(125, 85, 175, 115, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(147, 97, 153, 103, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(170, 97, 174, 103, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(135, 120, 165, 145, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	Carve(145, 115, 155, 120, 1, 3, Z_FLOOR, Z_HIGHCEIL);
	
	// 5. Верхняя комната (Дерево, балки)
	Carve(85, 25, 115, 55, 4, 1, Z_FLOOR, Z_CEIL);
	SetWalls(85, 25, 115, 55, 5, Z_FLOOR, Z_CEIL);
	Carve(80, 55, 120, 75, 4, 1, Z_FLOOR, Z_CEIL);
	Carve(95, 20, 105, 25, 4, 1, Z_FLOOR, Z_CEIL);
	SetCell(100, 55, CELL_DOOR, 4, 4, 1, Z_FLOOR, Z_CEIL);
	
	// 6. Нижняя комната (Туннели, низкий потолок)
	Carve(85, 125, 115, 155, 2, 2, Z_FLOOR, Z_LOWCEIL);
	SetWalls(85, 125, 115, 155, 2, Z_FLOOR, Z_LOWCEIL);
	Carve(120, 130, 150, 150, 2, 2, Z_FLOOR, Z_LOWCEIL);
	Carve(50, 130, 80, 150, 2, 2, Z_FLOOR, Z_LOWCEIL);
	Carve(115, 135, 120, 140, 2, 2, Z_FLOOR, Z_LOWCEIL);
	Carve(80, 135, 85, 140, 2, 2, Z_FLOOR, Z_LOWCEIL);
	
	// 7. Внешние комнаты (Разные)
	Carve(30, 30, 70, 50, 5, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(30, 50, 35, 80, 5, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(130, 30, 170, 50, 5, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(165, 50, 170, 80, 5, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(30, 150, 70, 170, 6, 0, Z_FLOOR, Z_HIGHCEIL);
	Carve(130, 150, 170, 170, 6, 0, Z_FLOOR, Z_HIGHCEIL);
	
	// 8. Обводка краёв
	for (int x = 0; x < MW; x++) {
		SetCell(x, 0, CELL_WALL, 6, 0, 0, Z_FLOOR, Z_HIGHCEIL);
		SetCell(x, MH-1, CELL_WALL, 6, 0, 0, Z_FLOOR, Z_HIGHCEIL);
	}
	for (int y = 0; y < MH; y++) {
		SetCell(0, y, CELL_WALL, 6, 0, 0, Z_FLOOR, Z_HIGHCEIL);
		SetCell(MW-1, y, CELL_WALL, 6, 0, 0, Z_FLOOR, Z_HIGHCEIL);
	}
	
		// 9. Старт игрока
	player.x = 100.5f;
	player.y = 110.5f;
	player.z = Z_FLOOR;
	player.pa = -1.57f;
	player.pitch = 0.0f;
	player.vx = 0; player.vy = 0; player.vz = 0;
	player.onGround = true;
	player.stamina = 100.0f;      // <--- ДОБАВИТЬ
	player.maxStamina = 100.0f;   // <--- ДОБАВИТЬ
	}

// ------------------------------------------------------------
//  СЕКТОР 10: ФИЗИКА И УПРАВЛЕНИЕ
// ------------------------------------------------------------

static bool GetFloorCeilAt(float wx, float wy, float *outFloorZ, float *outCeilZ, bool allowStepUp, float currentZ) {
	int cx = (int)floorf(wx);
	int cy = (int)floorf(wy);
	if (cx < 0 || cx >= MW || cy < 0 || cy >= MH) return false;
	
	Cell *c = &map[cx][cy];
	if (c->type == CELL_WALL) return false;
	
	if (c->type == CELL_CRATE) {
		if (currentZ >= c->ceilZ - STEP_UP) {
			*outFloorZ = c->ceilZ;                        
			*outCeilZ  = Z_HIGHCEIL; // Потолок высокий, чтобы не биться головой
			return true;
		}
		return false; 
	}
	
	float fz = c->floorZ;
	float cz = c->ceilZ;
	
	if (cz - fz < PLAYER_HEIGHT) return false;
	if (currentZ + PLAYER_HEIGHT > cz + 0.05f) return false;
	
	*outFloorZ = fz;
	*outCeilZ  = cz;
	return true;
}

static bool CanStandAt(float wx, float wy, float z, bool allowStepUp) {
	float r = PLAYER_RADIUS;
	float corners[4][2] = {
		{wx - r, wy - r}, {wx + r, wy - r},
		{wx - r, wy + r}, {wx + r, wy + r}
	};
	float fz, cz;
	for (int i = 0; i < 4; i++) {
		if (!GetFloorCeilAt(corners[i][0], corners[i][1], &fz, &cz, allowStepUp, z))
			return false;
	}
	return true;
}

void UpdatePlayer(float dt) {
	Vector2 md = GetMouseDelta();
	player.pa += md.x * 0.0025f;
	player.pitch -= md.y * 1.5f;
	
	if (player.pitch >  350.0f) player.pitch =  350.0f;
	if (player.pitch < -350.0f) player.pitch = -350.0f;
	if (player.pa < 0.0f)      player.pa += 2.0f * PI;
	if (player.pa > 2.0f * PI) player.pa -= 2.0f * PI;
	
	float cosA = cosf(player.pa);
	float sinA = sinf(player.pa);
	float wx = 0.0f, wy = 0.0f;
	
	if (IsKeyDown(KEY_W)) { wx += cosA; wy += sinA; }
	if (IsKeyDown(KEY_S)) { wx -= cosA; wy -= sinA; }
	if (IsKeyDown(KEY_A)) { wx += sinA; wy -= cosA; }
	if (IsKeyDown(KEY_D)) { wx -= sinA; wy += cosA; }
	
	float wl = sqrtf(wx * wx + wy * wy);
	if (wl > 0.001f) { wx /= wl; wy /= wl; }
	
	// --- СИСТЕМА СТАМИНЫ ---
	bool isMoving = (wl > 0.001f);
	bool wantsToSprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	bool isSprinting = false;
	
	float maxSpeed = 2.5f; 
	
	if (wantsToSprint && isMoving && player.stamina > 0.0f) {
		isSprinting = true;
		maxSpeed *= 1.6f; 
		player.stamina -= 25.0f * dt; 
		if (player.stamina < 0.0f) player.stamina = 0.0f;
	} else {
		player.stamina += 15.0f * dt;
		if (player.stamina > player.maxStamina) player.stamina = player.maxStamina;
	}
	
	// Показываем стамину, если бежим или она не восстановилась
	if (isSprinting || player.stamina < player.maxStamina) {
		stamina_ui_timer = 2.0f;
	}
	
	// --- ФИЗИКА (ИСПРАВЛЕНИЕ ДРЕЙФА) ---
	// Если не двигаемся на земле — жестко обнуляем скорость (никакого скольжения)
	if (!isMoving && player.onGround) {
		player.vx = 0;
		player.vy = 0;
	} else if (player.onGround) {
		float speed = sqrtf(player.vx * player.vx + player.vy * player.vy);
		if (speed > 0.1f) { 
			float drop = ((speed < 1.5f) ? 1.5f : speed) * 4.0f * dt; 
			float ns = speed - drop; 
			if (ns < 0) ns = 0; 
			ns /= speed; 
			player.vx *= ns; player.vy *= ns; 
		}
		if (wl > 0.001f) { 
			float add = 10.0f * dt * maxSpeed; 
			player.vx += add * wx; player.vy += add * wy; 
		}
	} else {
		if (wl > 0.001f) { 
			float ws = maxSpeed; 
			if (ws > 0.42f) ws = 0.42f; 
			float add = 12.0f * dt * ws; 
			player.vx += add * wx; player.vy += add * wy; 
		}
	}
	
	if (IsKeyDown(KEY_SPACE) && player.onGround) { 
		player.vz = JUMP_SPEED * 1.15f; 
		player.onGround = false; 
	}
	
	player.vz += GRAVITY * dt; 
	player.z += player.vz * dt;
	
	float fz = Z_FLOOR, cz = Z_HIGHCEIL;
	if (GetFloorCeilAt(player.x, player.y, &fz, &cz, false, player.z)) {
		if (player.z <= fz && player.vz <= 0.0f) {
			player.z = fz; player.vz = 0.0f; player.onGround = true;
		} else {
			player.onGround = false;
		}
	} else {
		if (player.z < Z_FLOOR) player.z = Z_FLOOR;
		player.onGround = false;
	}
	
	float nx = player.x + player.vx * dt;
	float ny = player.y + player.vy * dt;
	
	if (CanStandAt(nx, player.y, player.z, true)) player.x = nx; else player.vx = 0;
	if (CanStandAt(player.x, ny, player.z, true)) player.y = ny; else player.vy = 0;
	
	float hs = sqrtf(player.vx * player.vx + player.vy * player.vy);
	if (hs > 0.5f && player.onGround) { 
		player.bobPhase += dt * 9.0f * (hs / maxSpeed); 
		if (player.bobPhase > 2.0f * PI) player.bobPhase -= 2.0f * PI; 
		player.bobAmount += (1.0f - player.bobAmount) * dt * 6.0f; 
	} else {
		player.bobAmount += (0.0f - player.bobAmount) * dt * 6.0f;
	}
	
	if (IsKeyPressed(KEY_M)) showMap = !showMap;
}

// ------------------------------------------------------------
//  СЕКТОР 11: РЕНДЕР (С Z-буфером для пола)
// ------------------------------------------------------------
void RenderFrame(Color *pixels) {
	float dirX = cosf(player.pa), dirY = sinf(player.pa);
	float planeLen = tanf(fov * 0.5f);
	float planeX = -dirY * planeLen, planeY = dirX * planeLen;
	float camX = player.x, camY = player.y, camZ = player.z + EYE_HEIGHT;
	
	float bobV = sinf(player.bobPhase * 2.0f) * 6.0f * player.bobAmount;
	float bobH = cosf(player.bobPhase) * 4.0f * player.bobAmount;
	float horizon = SH / 2.0f + player.pitch + bobV;
	float projPlane = (float)SH * 0.5f / tanf(fov * 0.5f);
	
	// ===== ПРОХОД 1: DDA =====
#pragma omp parallel for num_threads(cpu_threads)
	for (int x = 0; x < SW; x++) {
		float camXc = 2.0f * ((float)x / (float)SW) - 1.0f;
		camXc += bobH / (float)SW;
		float rayDirX = dirX + planeX * camXc, rayDirY = dirY + planeY * camXc;
		int mapX = (int)floorf(camX), mapY = (int)floorf(camY);
		
		float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1/rayDirX);
		float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1/rayDirY);
		float sideDistX, sideDistY; int stepX, stepY;
		
		if (rayDirX < 0) { stepX = -1; sideDistX = (camX - mapX) * deltaDistX; } else { stepX = 1; sideDistX = (mapX + 1.0f - camX) * deltaDistX; }
		if (rayDirY < 0) { stepY = -1; sideDistY = (camY - mapY) * deltaDistY; } else { stepY = 1; sideDistY = (mapY + 1.0f - camY) * deltaDistY; }
		
		bool hit = false; int side = 0;
		while (!hit) {
			if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; } 
			else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
			if (mapX < 0 || mapX >= MW || mapY < 0 || mapY >= MH) { hit = true; break; }
			Cell *c = &map[mapX][mapY];
			if (c->type == CELL_WALL || c->type == CELL_CRATE) {
				if (camZ < c->ceilZ) hit = true;
			}
		}
		
		float perpDist = (side == 0) ? (mapX - camX + (1 - stepX)/2.0f) / rayDirX : (mapY - camY + (1 - stepY)/2.0f) / rayDirY;
		if (perpDist < 0.01f) perpDist = 0.01f;
		zbuf[x] = perpDist;
		
		float wallFloorZ = Z_FLOOR, wallCeilZ = Z_CEIL; int wallTexID = 0;
		if (mapX >= 0 && mapX < MW && mapY >= 0 && mapY < MH) {
			Cell *c = &map[mapX][mapY]; wallFloorZ = c->floorZ; wallCeilZ = c->ceilZ; wallTexID = c->wallTex;
		}
		
		float yTop = horizon - (wallCeilZ - camZ) / perpDist * projPlane;
		float yBottom = horizon - (wallFloorZ - camZ) / perpDist * projPlane;
		int drawStart = (int)yTop, drawEnd = (int)yBottom;
		if (drawStart < 0) drawStart = 0; if (drawEnd > SH) drawEnd = SH; if (drawEnd < 0) drawEnd = 0;
		
		float sampleX = (side == 0) ? camY + perpDist * rayDirY : camX + perpDist * rayDirX;
		sampleX -= floorf(sampleX);
		int tx = (int)(sampleX * TS) & (TS - 1); if (tx < 0) tx += TS;
		
		drawStartBuf[x] = drawStart; drawEndBuf[x] = drawEnd; texIDBuf[x] = wallTexID;
		txBuf[x] = tx; sideBuf[x] = side; perpBuf[x] = perpDist;
	}
	
	// ===== ПРОХОД 2: РАСТЕРИЗАЦИЯ (С Z-ТЕСТОМ) =====
#pragma omp parallel for num_threads(cpu_threads)
	for (int y = 0; y < SH; y++) {
		Color *row = pixels + y * SW;
		
		for (int x = 0; x < SW; x++) {
			int ds = drawStartBuf[x], de = drawEndBuf[x];
			float wallDist = perpBuf[x];
			float p = (float)y - horizon;
			
			float floorDist = 9999.0f;
			Color floorColor = BLACK;
			
			if (p > 1.0f) {
				float camXc = 2.0f * ((float)x / (float)SW) - 1.0f;
				camXc += bobH / (float)SW;
				float rayDirX = dirX + planeX * camXc, rayDirY = dirY + planeY * camXc;
				
				float rd = (camZ - Z_FLOOR) * projPlane / p;
				float floorX = camX + rayDirX * rd, floorY = camY + rayDirY * rd;
				int cellX = (int)floorf(floorX), cellY = (int)floorf(floorY);
				
				float actualFloorZ = Z_FLOOR;
				if (cellX >= 0 && cellX < MW && cellY >= 0 && cellY < MH) {
					if (map[cellX][cellY].type == CELL_CRATE) actualFloorZ = map[cellX][cellY].ceilZ;
				}
				
				rd = (camZ - actualFloorZ) * projPlane / p;
				if (rd > 0.01f && rd < depth) {
					floorX = camX + rayDirX * rd; floorY = camY + rayDirY * rd;
					cellX = (int)floorf(floorX); cellY = (int)floorf(floorY);
					
					int ftex = 0;
					bool isCrate = false;
					if (cellX >= 0 && cellX < MW && cellY >= 0 && cellY < MH) {
						ftex = map[cellX][cellY].floorTex;
						if (map[cellX][cellY].type == CELL_CRATE) isCrate = true;
					}
					
					int ftx = (int)((floorX - (float)cellX) * (float)TS); if (ftx < 0) ftx = 0; if (ftx >= TS) ftx = TS - 1;
					int fty = (int)((floorY - (float)cellY) * (float)TS); if (fty < 0) fty = 0; if (fty >= TS) fty = TS - 1;
					
					Color c;
					if (isCrate) c = wallTex[3][fty][ftx]; // Дерево для крышки
					else c = floorTex[ftex][fty][ftx];    // Песок для пола
					
					float fshade = 1.0f - (rd / depth) * 0.85f;
					if (fshade < 0.2f) fshade = 0.2f;
					c.r = (unsigned char)(c.r * fshade); c.g = (unsigned char)(c.g * fshade); c.b = (unsigned char)(c.b * fshade);
					
					floorDist = rd;
					floorColor = c;
				}
			}
			
			if (y < ds) {
				int skyY = (int)((float)y - player.pitch - bobV);
				if (skyY < 0) skyY = 0; if (skyY >= SKY_H) skyY = SKY_H - 1;
				row[x] = skyTex[skyY][x % SKY_W];
			} else if (y >= ds && y < de && wallDist < floorDist) {
				int lineHeight = de - ds; if (lineHeight < 1) lineHeight = 1;
				float texY = (float)(y - ds) / (float)lineHeight;
				int ty = (int)(texY * TS); if (ty < 0) ty = 0; if (ty >= TS) ty = TS - 1;
				
				Color c = wallTex[texIDBuf[x]][ty][txBuf[x]];
				float shade = 1.0f - (wallDist / depth) * 0.85f;
				if (shade < 0.2f) shade = 0.2f;
				if (sideBuf[x] == 1) shade *= 0.75f;
				
				c.r = (unsigned char)(c.r * shade); c.g = (unsigned char)(c.g * shade); c.b = (unsigned char)(c.b * shade);
				row[x] = c;
			} else if (floorDist < 9999.0f) {
				row[x] = floorColor;
			} else {
				row[x] = BLACK;
			}
		}
	}
}

// ------------------------------------------------------------
//  СЕКТОР 12: МИНИ-КАРТА И MAIN
// ------------------------------------------------------------

void DrawMiniMap(RenderTexture2D minimapRT) {
	if (!showMap) return;
	
	BeginTextureMode(minimapRT);
	ClearBackground(BLACK);
	
	int mmSize = 4;
	int curCellX = (int)player.x;
	int curCellY = (int)player.y;
	
	for (int mx = -25; mx < 25; mx++) {
		for (int my = -25; my < 25; my++) {
			int cx = curCellX + mx;
			int cy = curCellY + my;
			if (cx < 0 || cx >= MW || cy < 0 || cy >= MH) continue;
			
			Cell *c = &map[cx][cy];
			Color col = BLANK;
			if (c->type == CELL_WALL)         col = (Color){120, 100, 70, 255};
			else if (c->type == CELL_CRATE)   col = (Color){150, 100, 50, 255};
			else                              col = (Color){200, 180, 140, 80};
			
			DrawRectangle(100 + mx * mmSize, 100 + my * mmSize, mmSize, mmSize, col);
		}
	}
	
	DrawCircle(100, 100, 4, RED);
	DrawLine(100, 100,
			 100 + (int)(cosf(player.pa) * 15),
			 100 + (int)(sinf(player.pa) * 15), RED);
	
	EndTextureMode();
}

int main(void) {
	InitWindow(SW, SH, "MEGA RAYCASTER - Sunlight Edition");
	InitAudioDevice();
	GenerateAllTextures();
	GenMap();
	
	Music music = LoadMusicStream("jager_FyEc20wy.mp3");
	bool hasMusic = (music.frameCount > 0);
	if (hasMusic) { PlayMusicStream(music); music.looping = true; SetMusicVolume(music, music_volume); }
	
	Image renderBuffer = GenImageColor(SW, SH, BLACK);
	Color *pixels = (Color *)renderBuffer.data;
	Texture2D screenTexture = LoadTextureFromImage(renderBuffer);
	
	RenderTexture2D minimapRT = LoadRenderTexture(200, 200);
	int minimapTick = 0;
	
	int detected_procs = 4;
#ifdef _OPENMP
	detected_procs = omp_get_num_procs();
#endif
	cpu_threads = detected_procs;
	if (cpu_threads < 4) cpu_threads = 4; 
	if (cpu_threads > 16) cpu_threads = 16;
#ifdef _OPENMP
	omp_set_num_threads(cpu_threads);
#endif
	
	SetTargetFPS(0); 
	DisableCursor();
	
	while (!WindowShouldClose()) {
		float dt = GetFrameTime(); 
		if (dt > 0.05f) dt = 0.05f;
		
		if (hasMusic) UpdateMusicStream(music);
		UpdatePlayer(dt);
		RenderFrame(pixels);
		UpdateTexture(screenTexture, pixels);
		
		// --- ОБНОВЛЕНИЕ ТАЙМЕРОВ ИНТЕРФЕЙСА ---
		if (volume_ui_timer > 0.0f) volume_ui_timer -= dt;
		if (stamina_ui_timer > 0.0f) stamina_ui_timer -= dt;
		
		// --- УПРАВЛЕНИЕ ГРОМКОСТЬЮ ---
		if (IsKeyDown(KEY_LEFT_BRACKET)) { music_volume -= 0.5f * dt; if (music_volume < 0.0f) music_volume = 0.0f; volume_ui_timer = 2.0f; }
		if (IsKeyDown(KEY_RIGHT_BRACKET)) { music_volume += 0.5f * dt; if (music_volume > 1.0f) music_volume = 1.0f; volume_ui_timer = 2.0f; }
		if (hasMusic) SetMusicVolume(music, music_volume);
		
		// --- МИНИ-КАРТА ---
		if (showMap && (++minimapTick >= 10)) { 
			minimapTick = 0; 
			DrawMiniMap(minimapRT); 
		}
		
		BeginDrawing();
		ClearBackground(BLACK);
		DrawTexture(screenTexture, 0, 0, WHITE);
		
		if (showMap) {
			DrawTextureRec(minimapRT.texture, (Rectangle){0, 0, 200, -200}, (Vector2){SW - 220, 20}, WHITE);
			DrawText("M: Toggle Minimap", SW - 210, 230, 12, GRAY);
		}
		
		// --- HUD (ВЕРХНЯЯ ЧАСТЬ) ---
		DrawText("WASD: Move / SPACE: Jump / SHIFT: Sprint", 20, 20, 16, RAYWHITE);
		DrawText("[ / ]: Volume  M: Map", 20, 40, 16, GRAY);
		DrawText(TextFormat("Pos: X=%.1f Y=%.1f Z=%.1f", player.x, player.y, player.z), 20, 85, 16, YELLOW);
		DrawText(TextFormat("Speed: %.1f u/s", sqrtf(player.vx * player.vx + player.vy * player.vy)), 20, 105, 16, GREEN);
		DrawFPS(20, 65);
		
		// --- HUD (НИЖНЯЯ ЧАСТЬ) ---
		
				// 1. Шкала Стамины (по центру, ниже прицела)
		if (stamina_ui_timer > 0.0f) {
			// Ширина 300, центр по X это (SW/2 - 150) = 490
			int stX = SW / 2 - 150;
			int stY = SH / 2 + 180; // 180 пикселей вниз от прицела (чуть выше низа экрана)
			
			DrawRectangle(stX, stY, 300, 20, Fade(BLACK, 0.5f));
			Color stamCol = (player.stamina > 30.0f) ? GREEN : RED;
			DrawRectangle(stX + 5, stY + 5, (int)(290.0f * (player.stamina / player.maxStamina)), 10, stamCol);
			DrawText(TextFormat("Stamina: %d%%", (int)player.stamina), stX + 5, stY - 15, 12, WHITE);
		}
		
		// 2. Шкала Громкости (появляется при нажатии [ или ])
		if (volume_ui_timer > 0.0f) {
			DrawRectangle(20, SH - 40, 200, 20, Fade(BLACK, 0.5f));
			DrawRectangle(25, SH - 35, (int)(190 * music_volume), 10, BLUE);
			DrawText(TextFormat("Vol: %d%% ([ / ])", (int)(music_volume * 100)), 25, SH - 55, 12, WHITE);
		}
		
		EndDrawing();
	}
	
	if (hasMusic) UnloadMusicStream(music);
	UnloadRenderTexture(minimapRT);
	UnloadTexture(screenTexture); 
	UnloadImage(renderBuffer);
	CloseAudioDevice(); 
	CloseWindow();
	return 0;
}
