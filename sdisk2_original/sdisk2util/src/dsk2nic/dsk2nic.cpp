#include <stdio.h>
#include <windows.h>
#include <shlwapi.h>
#include <tchar.h>
#pragma comment(lib, "shlwapi.lib")

char *StrToUpper(char *s)
{
    char *p;  
 
    for (p = s; *p; p++) *p = toupper(*p);
	return s;
}

void fputc_(int ch, HANDLE fp)
{
	unsigned char buff[1];
	DWORD written;
	buff[0] = ch;
	if (!WriteFile(fp, buff, 1, &written, NULL)) throw TEXT("File error.");
	if (written != 1) throw TEXT("File error.");
}

void fread_(void *buff, int w, unsigned long len, HANDLE fp)
{
	DWORD read;
	if (!ReadFile(fp, buff, len*w, &read, NULL)) throw TEXT("File error.");
	if (read != len) throw TEXT("File error.");
}

void writeAAVal(unsigned char val, HANDLE fp)
{
	fputc_(0xaa | (val >> 1), fp);
	fputc_(0xaa | val, fp);
}

void conv(LPCTSTR name)
{
	static const unsigned char encTable[] = {
		0x96,0x97,0x9A,0x9B,0x9D,0x9E,0x9F,0xA6,
		0xA7,0xAB,0xAC,0xAD,0xAE,0xAF,0xB2,0xB3,
		0xB4,0xB5,0xB6,0xB7,0xB9,0xBA,0xBB,0xBC,
		0xBD,0xBE,0xBF,0xCB,0xCD,0xCE,0xCF,0xD3,
		0xD6,0xD7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,
		0xDF,0xE5,0xE6,0xE7,0xE9,0xEA,0xEB,0xEC,
		0xED,0xEE,0xEF,0xF2,0xF3,0xF4,0xF5,0xF6,
		0xF7,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF
	};
	static const unsigned char scramble[] = {
		0,7,14,6,13,5,12,4,11,3,10,2,9,1,8,15
	};
	static const unsigned char FlipBit1[4] = { 0, 2,  1,  3  };
	static const unsigned char FlipBit2[4] = { 0, 8,  4,  12 };
	static const unsigned char FlipBit3[4] = { 0, 32, 16, 48 };

	unsigned char src[256+2];
	int volume = 0xfe;
	int track, sector;

	if (!name || !name[0]) return;

	HANDLE fp, fpw;
	TCHAR path[MAX_PATH];

	if (StrCmp(StrToUpper(PathFindExtension(name)), TEXT(".DSK")) != 0) {
		MessageBox(NULL, TEXT("Drop .DSK file."), TEXT("dsk2nic"), MB_OK);
		return;
	}

	fp = CreateFile(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fp == INVALID_HANDLE_VALUE) {
		MessageBox(NULL, TEXT("Can't open the DSK file."), TEXT("dsk2nic"), MB_OK);
		return;
	}

	lstrcpy(path, name);
	PathRemoveExtension(path);
	PathAddExtension(path, TEXT(".NIC"));

	fpw = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);		
	if (fpw == INVALID_HANDLE_VALUE) {
		if (MessageBox(NULL, TEXT("Overwrite the NIC file?"), TEXT("dsk2nic"), MB_OKCANCEL)
			!= IDOK) {
			CloseHandle(fp);
			return;
		}
		fpw = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fpw == INVALID_HANDLE_VALUE) {
			CloseHandle(fp);
			return;
		}
	}

	try {
		for (track = 0; track < 35; track++) {
			for (sector = 0; sector < 16; sector++) {
				int i;
				unsigned char x, ox = 0;

				if (SetFilePointer(fp, track*(256*16)+scramble[sector]*256, NULL, FILE_BEGIN)
					== INVALID_SET_FILE_POINTER) throw TEXT("File error.");
				for (i=0; i<22; i++) fputc_(0xff, fpw);

				fputc_(0x03, fpw);
				fputc_(0xfc, fpw);
				fputc_(0xff, fpw);
				fputc_(0x3f, fpw);
				fputc_(0xcf, fpw);				
				fputc_(0xf3, fpw);
				fputc_(0xfc, fpw);
				fputc_(0xff, fpw);
				fputc_(0x3f, fpw);
				fputc_(0xcf, fpw);
				fputc_(0xf3, fpw);
				fputc_(0xfc, fpw);

				fputc_(0xd5, fpw);
				fputc_(0xaa, fpw);
				fputc_(0x96, fpw);
				writeAAVal(volume, fpw);
				writeAAVal(track, fpw);
				writeAAVal(sector, fpw);
				writeAAVal(volume ^ track ^ sector, fpw);
				fputc_(0xde, fpw);
				fputc_(0xaa, fpw);
				fputc_(0xeb, fpw);
				for (i=0; i<5; i++) fputc_(0xff, fpw);
				fputc_(0xd5, fpw);
				fputc_(0xaa, fpw);
				fputc_(0xad, fpw);
				fread_(src, 1, 256, fp);
				src[256] = src[257] = 0;
				for (i = 0; i < 86; i++) {
					x = (FlipBit1[src[i]&3] | FlipBit2[src[i+86]&3] | FlipBit3[src[i+172]&3]);
					fputc_(encTable[(x^ox)&0x3f], fpw);
					ox = x;
				}
				for (i = 0; i < 256; i++) {
					x = (src[i] >> 2);
					fputc_(encTable[(x^ox)&0x3f], fpw);
					ox = x;
				}
				fputc_(encTable[ox&0x3f], fpw);
				fputc_(0xde, fpw);
				fputc_(0xaa, fpw);
				fputc_(0xeb, fpw);
				for (i=0; i<14; i++) fputc_(0xff, fpw);
				for (i=0; i<(512-416); i++) fputc_(0x00, fpw);
			}
		}
	}
	catch (LPTSTR msg) {
		MessageBox(NULL, msg, TEXT("dsk2nic"), MB_OK);
		CloseHandle(fpw);
		CloseHandle(fp);
		return; 
	}
	// MessageBox(NULL, TEXT("the NIC image created."), TEXT("dsk2nic"), MB_OK);
	CloseHandle(fpw);
	CloseHandle(fp);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
	if (__argc>1) {
		int i;
		for (i=1; i<__argc; i++) conv(__targv[i]);
	}
	return 0;
}