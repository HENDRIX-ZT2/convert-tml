//
//Adjusted and optimized version for JPOG batch use. Below is the original notice.
//

/*************************************************************************\
	ConvertCCT.cpp
	
	Conversion program for a bunch of formats based on DirectX texture
	compression (DXT1,DXT3,DXT5)

	Formats include:
	- .DDS  DirectX texture format, only DXT1, DXT3, (and DXT5 with no alpha)
	- .CCT  X-Isle texture format
	- .CTC  X-Isle terrain texture format
	- .TML  Jurassic Park Operation Genesis texture library


	2003-03-29 Initial coding.
\*************************************************************************/

#include <windows.h>
#include <stdio.h>


/*************************************************************************\
	Misc routines
\*************************************************************************/

// Does 'str' end with 'end', case insensitive
bool endswithi(const char* str, const char* end) {
	int lend = strlen(end);
	if (strlen(str) < strlen(end)) return false;
	return _stricmp(str+strlen(str)-lend, end) == 0;
}

typedef unsigned char byte;
typedef unsigned int uint;

int ReadLong(FILE* file) {
	int temp = 0;
	fread(&temp, 4, 1, file);
	return temp;
}
float ReadFloat(FILE* file) {
	float temp = 0;
	fread(&temp, 4, 1, file);
	return temp;
}
int ReadShort(FILE* file) {
	int temp = 0;
	fread(&temp, 2, 1, file);
	return temp;
}



/*************************************************************************\
	Misc color related functions
\*************************************************************************/

struct RGBA{
	union {
		uint dw;
		struct { unsigned char r,g,b,a; };
	};
	//uint operator unsigned int() { return dw; }
};

inline uint rgbtobgr(uint col) {
	RGBA ans;
	ans.dw = col;
	int temp = ans.r;
	ans.r = ans.b;
	ans.b = temp;
	return ans.dw;
}

// 888 is rgb order
inline uint rgb565to888(uint col) {
	RGBA ans;
	ans.r = byte(255*((col<<3) & 0xF8)/0xF8); // expand color to fill full 0-255 range
	ans.g = byte(255*((col>>3) & 0xFC)/0xFC);
	ans.b = byte(255*((col>>8) & 0xF8)/0xF8); 
	ans.a = 255;
	return ans.dw;
}
inline uint rgba5551to888(uint col) {
	RGBA ans;
	ans.r = byte(255*((col<<3) & 0xF8)/0xF8); // expand color to fill full 0-255 range
	ans.g = byte(255*((col>>2) & 0xF8)/0xF8);
	ans.b = byte(255*((col>>7) & 0xF8)/0xF8); 
	ans.a = byte(255*((col>>8) & 0x80)/0x80); 
	return ans.dw;
}
inline uint rgba4444to888(uint col) {
	RGBA ans;
	ans.r = byte(255*((col<<4) & 0xF0)/0xF0); // expand color to fill full 0-255 range
	ans.g = byte(255*((col>>0) & 0xF0)/0xF0);
	ans.b = byte(255*((col>>4) & 0xF0)/0xF0); 
	ans.a = byte(255*((col>>8) & 0xF0)/0xF0);
	return ans.dw;
}
inline uint a4to8(uint col) {
	return byte(255*((col<<4) & 0xF0)/0xF0);
}




/*************************************************************************\
	DXT texture decompression routines
\*************************************************************************/

// return (a+b)/2
inline uint interp2(uint _a, uint _b) {
	RGBA a; a.dw=_a;
	RGBA b; b.dw=_b;
	RGBA r; r.a = 255;
	r.r = (int(a.r)+int(b.r))/2;
	r.g = (int(a.g)+int(b.g))/2;
	r.b = (int(a.b)+int(b.b))/2;
	return r.dw;
}
// return (2a+b)/3 
inline uint interp3(uint _a, uint _b) {
	RGBA a; a.dw=_a;
	RGBA b; b.dw=_b;
	RGBA r; r.a = 255;
	r.r = (2*int(a.r)+int(b.r)+1)/3;
	r.g = (2*int(a.g)+int(b.g)+1)/3;
	r.b = (2*int(a.b)+int(b.b)+1)/3;
	return r.dw;
}

// From MSDN search for "Compressed Texture Formats" and "DDS"
// fourcc is the DXT compression code (DXT1,DXT3,DXT5. DXT5 alpha not supported)
void ConvertDXTtoRGB(int width, int height, uint* dxt, uint *rgb, const char*fourcc) {
	bool dxt1 = strnicmp(fourcc, "DXT1", 4) == 0;
	bool interpAlpha = strnicmp(fourcc, "DXT5", 4) == 0;
	for (int y=0; y<height; y+=4) {
		for (int x=0; x<width; x+=4) {
			int blockDwords = (dxt1) ? 2 : 4;
			int dxtPos = ((x/4)+(y/4)*(width/4))*blockDwords;

			// Extract color bits
			int colorOffs = (dxt1) ? 0 : 2;
			uint data = dxt[dxtPos + colorOffs + 0];
			uint sel  = dxt[dxtPos + colorOffs + 1];
			// Set up interpolated color table
			uint color_0 = data & 0xFFFF;
			uint color_1 = (data>>16) & 0xFFFF;
			uint ctable[4];
			ctable[0] = rgb565to888(color_0);
			ctable[1] = rgb565to888(color_1);
			if (color_0 > color_1 || !dxt1) {
				ctable[2] = interp3(ctable[0], ctable[1]);
				ctable[3] = interp3(ctable[1], ctable[0]);
			} else {
				ctable[2] = interp2(ctable[0], ctable[1]);
				ctable[3] = 0x00000000;
			}

			// Extract alpha bits
			uint alphaBlock0 = dxt[dxtPos + 0]; // not used if dxt1, but no matter.
			uint alphaBlock1 = dxt[dxtPos + 1];
			uint alphaIndex0 = ((alphaBlock0 & 0xFFFF0000) >> 16) + ((alphaBlock1 & 0xFF) << 16);
			uint alphaIndex1 = ((alphaBlock1 & 0xFFFFFF00) >> 8);
			uint atable[8];
			if (interpAlpha) {
				// Set up interpolated alpha table
				atable[0] = (alphaBlock0 & 0x00FF);
				atable[1] = (alphaBlock0 & 0xFF00) >> 8;
				if (atable[0] > atable[1]) {    
					// 8-alpha block:  derive the other six alphas.
					// Bit code 000 = atable[0], 001 = atable[1], others are interpolated.
					atable[2] = (6 * atable[0] + 1 * atable[1] + 3) / 7;    // bit code 010
					atable[3] = (5 * atable[0] + 2 * atable[1] + 3) / 7;    // bit code 011
					atable[4] = (4 * atable[0] + 3 * atable[1] + 3) / 7;    // bit code 100
					atable[5] = (3 * atable[0] + 4 * atable[1] + 3) / 7;    // bit code 101
					atable[6] = (2 * atable[0] + 5 * atable[1] + 3) / 7;    // bit code 110
					atable[7] = (1 * atable[0] + 6 * atable[1] + 3) / 7;    // bit code 111  
				}    
				else {  
					// 6-alpha block.
					// Bit code 000 = atable[0], 001 = atable[1], others are interpolated.
					atable[2] = (4 * atable[0] + 1 * atable[1] + 2) / 5;    // Bit code 010
					atable[3] = (3 * atable[0] + 2 * atable[1] + 2) / 5;    // Bit code 011
					atable[4] = (2 * atable[0] + 3 * atable[1] + 2) / 5;    // Bit code 100
					atable[5] = (1 * atable[0] + 4 * atable[1] + 2) / 5;    // Bit code 101
					atable[6] = 0;                                      // Bit code 110
					atable[7] = 255;                                    // Bit code 111
				}
			}

			// Decompress...
			for (int yy=0; yy<4; yy++) {
				for (int xx=0; xx<4; xx++) {
					int pos = (xx+yy*4); // linear position in 16 pixel square ( row (x-coord) major)

					// Look up color
					int bits = (sel >> (pos*2)) & 0x0003;
					uint pixel = ctable[bits];

					// If we have a seperate block for alpha values, look up alpha
					if (!dxt1) { // 
						int alpha;
						if (interpAlpha) { // Interpolated alpha
							uint abits;
							if (pos < 8) abits = (alphaIndex0 >> ((pos-0)*3)) & 0x07;
							else         abits = (alphaIndex1 >> ((pos-8)*3)) & 0x07;
							alpha = atable[abits];
						} else { // Explicit alpha
							if (pos < 8) alpha = (alphaBlock0 >> ((pos-0)*4)) & 0x0F;
							else         alpha = (alphaBlock1 >> ((pos-8)*4)) & 0x0F;
							alpha = a4to8(alpha);
						}
						pixel = (pixel & 0xFFFFFF) + ((alpha & 0xFF) << 24);
					}

					// Save the result!
					rgb[x+xx+(y+yy)*width] = pixel;
				}
			}
		}
	}
}


void WriteTGAImage(const char* filename, short width, short height, uint *rgb, bool flipY = false) {
	// can this be done further up?
	unsigned char* rgba = (unsigned char*)rgb;

	FILE *file = fopen(filename, "wb");
	if (!file)
		return;

	char cbuffer[] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,};
	fwrite(cbuffer, sizeof(char), sizeof(cbuffer), file);
	short sbuffer[] = { width, height };
	fwrite(sbuffer, sizeof(short), 2, file);
	char cbuffer2[] = {32,0 };
	fwrite(cbuffer2, sizeof(char), sizeof(cbuffer2), file);


	//Write the pixel data
	for (int y = 0; y<height; y++)
		for (int x = 0; x<width; x++) {
			if (flipY) {
				//write four bytes from here to file
				fwrite(&rgba[(x + (height - 1 - y)*width) * 4], 4, 1, file);
			}
			else {
				fwrite(&rgba[(x + y*width) * 4], 4, 1, file);
			}
		}
	fclose(file);

}


#include <conio.h>

void ExtractTML(FILE *file, const char* fileName) {
	int type = ReadLong(file);
	if (strnicmp((char*)&type, "tml1", 4)) {
		printf("Unrecognised file format.\n");
		printf("\nPress any key to exit\n");
		//_getch();
		return;
	}
	
	char P[_MAX_DRIVE], D[_MAX_DIR], Fname[_MAX_FNAME];
	char baseName[_MAX_PATH];
	_splitpath(fileName, P, D, Fname, 0);
	strcpy(baseName, P);
	strcat(baseName, D);
	//strcat(baseName, F);

	int i;
	ReadLong(file);
	int numBlocks = ReadLong(file);
	char (*blockName)[32+8] = new char[numBlocks][32+8];
	for (i=0; i<numBlocks; i++)
		memset(blockName[i], 0, 32+8);

	// Skip past all blocks so we can read the names
	fseek(file, 0x0C, SEEK_SET);
	for (i=0; i<numBlocks; i++) {
		int nextBlockPos = ftell(file);
		int blockNum = ReadLong(file);
		int dataSize = ReadLong(file);
		nextBlockPos += dataSize + 0x1C;
		fseek(file, nextBlockPos, SEEK_SET);
	}

	// Read names
	bool doNames = true;
	if (doNames) {
		int numStrings = ReadLong(file);
		char (*stringName)[32] = new char[numStrings][32];
		for (i=0; i<numStrings; i++) {
			fread(stringName[i], 32, 1, file);
		}
		for (i=0; i<numStrings; i++) {
			int strNum = ReadLong(file);
			int temp = ReadShort(file);
			int count = ReadShort(file);
			for (int j=0; j<count; j++) {
				int blk = ReadLong(file);
				strcpy(blockName[blk], stringName[strNum]);
			}
		}
		// Get rid of duplicate names, append "-2", "-3", for 2nd, 3rd copy of name etc.
		for (i=numBlocks-1; i>=0; i--) {
			int count = 1;
			for (int j=0; j<i; j++) {
				if (!strcmp(blockName[i], blockName[j]))
					count++;
			}
			if (count > 1) {
				char buf[255];
				sprintf(buf, "%s-%d", blockName[i], count);
				strcpy(blockName[i], buf);
			}
		}
	}


	// Read and output textures
	fseek(file, 0x0C, SEEK_SET);
	for (i=0; i<numBlocks; i++) {
		int nextBlockPos = ftell(file);
		int blockNum = ReadLong(file);
		int dataSize = ReadLong(file);
		int f1 = ReadLong(file);
		int f2 = ReadLong(file);
		int format = ReadShort(file);
		int width = ReadShort(file);
		int height = ReadShort(file);
		int f3 = ReadShort(file);
		int f4 = ReadLong(file);
		nextBlockPos += dataSize + 0x1C;

		uint *rgb = new uint[width*height];

		// Create name for this texture
		char texName[256];
		strcpy(texName, blockName[blockNum]);
		if (texName[0] == 0)
			sprintf(texName, "%s_%02d", Fname, blockNum);

		if (format == 0) { // RGBA
			int dataSize = width*height*4;
			unsigned long *dxt = (unsigned long*)new char[dataSize];
			fread(dxt, dataSize, 1, file);
			for (int y=0; y<height; y++) {
				for (int x=0; x<width; x++) {
					uint col = dxt[x+y*width];
					rgb[x+y*width] = rgbtobgr(col);
				}
			}
			delete [] dxt;
		}
		else
		if (format == 2 || format == 7) { // 16bit
			int dataSize = width*height*2;
			unsigned short *dxt = (unsigned short*)new char[dataSize];
			fread(dxt, dataSize, 1, file);
			for (int y=0; y<height; y++) {
				for (int x=0; x<width; x++) {
					int col = dxt[x+y*width];
					if (format == 2)
						rgb[x+y*width] = rgba5551to888(col);
					else
					if (format == 7)
						rgb[x+y*width] = rgba4444to888(col);
					rgb[x+y*width] = rgbtobgr(rgb[x+y*width]);
				}
			}
			delete [] dxt;
		}
		else
		if (format == 6) {// DXT compressed texture
			int width2, height2, type3;
			int dataOffs = ftell(file);

			// Check magic number
			int type2 = ReadLong(file);
			if (strnicmp((char*)&type2, "dds ", 4)) {
				printf("Expected 'dds ' format.\n");
				goto errNextLoop;
			}

			// Verify sizes
			fseek(file, dataOffs+0x0C, SEEK_SET);
			height2 = ReadLong(file);
			width2 = ReadLong(file);
			if (height != height2 || width != width2) {
				printf("DDS width and\\or height don't match.\n");
				goto errNextLoop;
			}
			
			// Check supported compression formats
			fseek(file, dataOffs+0x54, SEEK_SET);
			type3 = ReadLong(file);
			if (strnicmp((char*)&type3, "DXT3", 4) && strnicmp((char*)&type3, "DXT5", 4)) {
				printf("Expected 'DXT3' or 'DXT5' format.\n");
				goto errNextLoop;
			}

			{
				fseek(file, dataOffs+0x7C+4, SEEK_SET); // start of data

				int dataSize = width*height;
				uint *dxt = (uint*)new char[dataSize];
				fread(dxt, dataSize, 1, file);
				ConvertDXTtoRGB(width, height, dxt, rgb, (char*)&type3);
				delete [] dxt;
			}
		} else {
			printf("Sub-format %d not supported for texture block %d\n", format, blockNum);
			printf(" Press any key to skip texture.\n");
			//_getch();
			continue;
		}

		if (true) {
			char texPath[MAX_PATH];
			sprintf(texPath,     "%s%s.tga", baseName, texName);
			printf("Writing file '%s'...\n", texPath);
			//could the rgb array be set to use char here already?
			WriteTGAImage(texPath, width, height, rgb, true);

		}
		goto nextLoop;
errNextLoop:
		printf(" Skipping Texture.\n");
		//_getch();
nextLoop:
		delete [] rgb;
		fseek(file, nextBlockPos, SEEK_SET);
	}

}


void main(int argc, char** argv) {

	if (argc != 2) {
		printf("ConvertTML.exe\n");
		printf("\nExtracts textures from the following file types:\n");
		printf("  .tml  Jurassic Park Operation Genesis texture library\n");
		printf("Textures are extracted as .tga, put in the same directory as the original file.\n");
		printf("\nUsage: ConvertTML <fileName>\n");
		printf("       or drag and drop file onto this application.\n");
		printf("\nPress any key to exit\n");
		_getch();
		return;
	}
	const char* fileName = argv[1];

	FILE *file = fopen(fileName, "rb");
	if (!file) {
		printf("File not found\n");
		printf("\nPress any key to exit\n");
		//_getch();
		return;
	}

	if (endswithi(fileName, "tml")) { // JPOG material library
		ExtractTML(file, fileName);
		fclose(file);
		return;
	}
}