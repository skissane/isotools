#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SECTOR_SIZE 2048

static const char* basename(const char* path) {
    const char* ls = strrchr(path,'/');
    if (ls == NULL || strlen(ls+1)==0) return path;
    return ls+1;
}

bool read_sector(FILE* fh, off_t number, char* secbuf) {
    if (fseeko(fh, number*SECTOR_SIZE, SEEK_SET) != 0) return false;
    return fread(secbuf,1,SECTOR_SIZE,fh) == SECTOR_SIZE;
}

void dump_sector(char* secbuf) {
    int conAllZero = 0;
    for (int i = 0; i < 128; i++) {
        bool allZero = true;
        for (int j = 0; j < 16; j++) {
            uint8_t ch = secbuf[(i*16)+j];
            if (ch != 0) allZero = false;
        }
        if (allZero) conAllZero++; else conAllZero = 0;
        if (i < 127) {
            if (conAllZero == 2) {
                printf("\t...\n");
                continue;
            }
            if (conAllZero > 2) continue;
        }
        printf("\t%03x: ",i*16);
        for (int j = 0; j < 16; j++) {
            uint8_t ch = secbuf[(i*16)+j];
            printf("%02x", ch);
        }
        printf(" ");
        for (int j = 0; j < 16; j++) {
            uint8_t ch = secbuf[(i*16)+j];
            printf("%c", ch >= 0x20 && ch <= 0x7e ? ch : '.');
        }
        printf("\n");
    }
}

#define ELTORITO "EL TORITO SPECIFICATION"

uint32_t from_u32le(uint8_t* data) {
    uint32_t r = data[3];
    for (int i = 2; i >= 0; i--) {
        r >>= 8;
        r |= data[i];
    }
    return r;
}

void print_boot_record(char* secbuf,uint32_t *bootCatSector) {
    printf("\tTYPE 0: BOOT RECORD\n");
    char bootSysId[33] = {0};
    memcpy(bootSysId, secbuf+7, 32);
    printf("\t\tBoot System Id = [%s]\n", bootSysId);
    char bootId[33] = {0};
    memcpy(bootId, secbuf+7, 32);
    printf("\t\tBoot Id        = [%s]\n", bootId);
    if (strcmp(bootSysId,ELTORITO)!=0 || strcmp(bootId,ELTORITO)!=0) {
        printf("\t!!! NOT EL TORITO\n");
        return;
    }
    printf("\t\t=== EL TORITO FOUND\n");
    if (*bootCatSector != 0)
        printf("\t\t??? MULTIPLE EL TORITO BOOT RECORDS ???\n");
    *bootCatSector = from_u32le((uint8_t*)(secbuf + 71));
    printf("\t\tBOOT CATALOG SECTOR = %zu\n", (size_t)*bootCatSector);
}

int main(int argc, char *argv[]) {
    if (argc != 2)
        return fprintf(stderr, "|ERROR| Usage: %s FILENAME\n", basename(argv[0])), 1;
    char *filename = argv[1];
    const char *filebase = basename(filename);
    struct stat file_stat;
    if (stat(filename, &file_stat) == -1)
        return perror("stat failed"), 1;
    if (!S_ISREG(file_stat.st_mode))
        return fprintf(stderr, "|ERROR| %s: not a regular file\n", filebase), 1;
    size_t size = file_stat.st_size;
    if ((size % SECTOR_SIZE) != 0)
        return fprintf(stderr, "|ERROR| %s: extra %zu bytes at end of image\n", filebase, (size % SECTOR_SIZE)), 1;
    size_t sectors = size / SECTOR_SIZE;
    if (sectors < 17)
        return fprintf(stderr, "|ERROR| %s: not a valid image, %zu is too few sectors\n", filebase, sectors), 1;
    printf("%s: %zu sectors\n", filebase, sectors);
    FILE *fh = fopen(filename,"r");
    if (fh == NULL)
        return perror("fopen failed"), 1;
    char secbuf[SECTOR_SIZE];
    off_t sector = 16;
    uint32_t bootCatSector = 0;
    printf("=== Volume Descriptors\n");
    while (true) {
        if (!read_sector(fh,sector,secbuf))
            return fprintf(stderr, "|ERROR| %s: failed reading sector %zu\n", filebase, (size_t)sector), 1;
        uint8_t type = secbuf[0];
        char ident[6] = {0};
        memcpy(ident,secbuf+1,5);
        if (strcmp(ident,"CD001") != 0)
            return fprintf(stderr, "|ERROR| %s: sector %zu missing CD001 descriptor\n", filebase, (size_t)sector), 1;
        uint8_t version = secbuf[6];
        printf("Sector %zu: descriptor type %d version %d\n", (size_t)sector, type, version);
        dump_sector(secbuf);
        if (type == 0)
            print_boot_record(secbuf,&bootCatSector);
        if (type == 255) break;
        sector++;
    }
    printf("TOTAL: %zu volume descriptors (sectors 16-%zu)\n",
           (size_t)((sector-16)+1),
           (size_t)sector);
    if (bootCatSector != 0) {
        sector = bootCatSector;
        if (!read_sector(fh,sector,secbuf))
            return fprintf(stderr, "|ERROR| %s: failed reading boot catalog sector %zu\n", filebase, (size_t)sector), 1;
        printf("=== El Torito\n");
        printf("Sector %zu: EL TORITO BOOT CATALOG\n", (size_t)sector);
        dump_sector(secbuf);
    }
    fclose(fh);
    return 0;
}

// vim: et ts=4 sw=4 cc=121 tw=120 cino=l1(0
