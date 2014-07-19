/* pj64tosrm
 * Combine Project64's save files (*.eep, *.mpk, *.fla, *.sra) into a
 * libretro-mupen64 save file (*.srm).
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
 
/* from libretro's mupen64+ source (save_memory_data) */
static struct
{
    uint8_t eeprom[0x200];
    /*uint8_t mempack[4][0x8000];*/
	uint8_t mempack[0x20000];
    uint8_t sram[0x8000];
    uint8_t flashram[0x20000];
} __attribute__((packed)) srm;
 
static void die(const char *msg)
{
	puts(msg);
	exit(-1);
}
 
static void read_mem(const char *filename, void *dest, size_t size)
{
	FILE *fp = fopen(filename, "rb");
 
	if (!fp) {
		perror(filename);
		exit(EXIT_FAILURE);
	}
 
	fread(dest, size, 1, fp);
 
	if (ferror(fp)) {
		perror(filename);
		fclose(fp);
		exit(EXIT_FAILURE);
	}
 
	fclose(fp);
}
 
int main(int argc, char *argv[])
{
	FILE *fp = NULL;
 
	if (argc < 4) {
Usage:
		printf("usage: %s output.srm [-e file.eep] [-m file.mpk] [-s file.sra] [-f file.fla]\n", argv[0]);
		exit(EXIT_FAILURE);
	}
 
	int i;
	int inputs = 0;
 
	for (i = 2; i < argc; ++i) {
		if (!strcmp(argv[i], "-h"))
			goto Usage;
 
		if (!strcmp(argv[i], "-e")) {
			if (i + 1 == argc)
				die("missing filename after -e argument");
			i++;
			read_mem(argv[i], srm.eeprom, sizeof(srm.eeprom));
			inputs++;
		}
		if (!strcmp(argv[i], "-m")) {
			if (i + 1 == argc)
				die("missing filename after -m argument");
			i++;
			read_mem(argv[i], srm.mempack, sizeof(srm.mempack));
			inputs++;
		}
		if (!strcmp(argv[i], "-s")) {
			if (i + 1 == argc)
				die("missing filename after -s argument");
			i++;
			read_mem(argv[i], srm.sram, sizeof(srm.sram));
			inputs++;
		}
		if (!strcmp(argv[i], "-f")) {
			if (i + 1 == argc)
				die("missing filename after -f argument");
			i++;
			read_mem(argv[i], srm.flashram, sizeof(srm.flashram));
			inputs++;
		}
	}
 
	if (!inputs) {
		die("no input file.");
	}
 
	fp = fopen(argv[1], "wb");
 
	if (!fp) {
		perror(argv[1]);
		exit(EXIT_FAILURE);
	}
 
	fwrite(&srm, sizeof(srm), 1, fp);
	fclose(fp);
 
	return 0;
}
