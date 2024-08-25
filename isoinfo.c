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

void dump_binary(char* secbuf, int paras) {
    int conAllZero = 0;
    for (int i = 0; i < paras; i++) {
        bool allZero = true;
        for (int j = 0; j < 16; j++) {
            uint8_t ch = secbuf[(i*16)+j];
            if (ch != 0) allZero = false;
        }
        if (allZero) conAllZero++; else conAllZero = 0;
        if (i < (paras-1)) {
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

void dump_sector(char* secbuf) {
    dump_binary(secbuf,128);
}

#define ELTORITO "EL TORITO SPECIFICATION"

uint16_t from_u16le(uint8_t* data) {
    return (data[0]) | (data[1] << 8);
}

uint16_t from_u16be(uint8_t* data) {
    return (data[1]) | (data[0] << 8);
}

typedef struct {
    uint16_t le, be;
} u16_biendian_t;

typedef struct {
    uint32_t le, be;
} u32_biendian_t;

u16_biendian_t from_u16bi(uint8_t* data) {
    u16_biendian_t v;
    v.le = from_u16le(data);
    v.be = from_u16be(data+2);
    return v;
}

uint32_t from_u32le(uint8_t* data) {
    uint32_t r = data[3];
    for (int i = 2; i >= 0; i--) {
        r <<= 8;
        r |= data[i];
    }
    return r;
}

uint32_t from_u32be(uint8_t* data) {
    uint32_t r = data[0];
    for (int i = 1; i < 4; i++) {
        r <<= 8;
        r |= data[i];
    }
    return r;
}

u32_biendian_t from_u32bi(uint8_t* data) {
    u32_biendian_t v;
    v.le = from_u32le(data);
    v.be = from_u32be(data+4);
    return v;
}

bool u16bi_validate(const char* field, u16_biendian_t v) {
    if (v.le == v.be) return true;
    printf("\t\t??? %s: LE and BE mismatch: LE %u, BE %u\n",
           field, v.le, v.be);
    return false;
}

bool u32bi_validate(const char* field, u32_biendian_t v) {
    if (v.le == v.be) return true;
    printf("\t\t??? %s: LE and BE mismatch: LE %u, BE %u\n",
           field, v.le, v.be);
    return false;
}

void read_string(char* secbuf, char* strbuf, int offset, int maxlen) {
    memset(strbuf,0,maxlen+1);
    memcpy(strbuf,secbuf+offset,maxlen);
}

bool datetime_empty(uint8_t* dt) {
    for (int i = 0; i < 17; i++)
        if (dt[i] != 0) return false;
    return true;
}

bool is_digit(char ch) { return (ch >= '0') && (ch <= '9'); }

bool is_digits(char* f, int count) {
    for (int i = 0; i < count; i++)
        if (!is_digit(f[i])) return false;
    return true;
}

bool ensure_digits(char* f, int count, char* fname) {
    if (is_digits(f,count)) return true;
    printf("[invalid %s]\n",fname);
    return false;
}

void print_datetime(uint8_t* dt) {
    if (datetime_empty(dt)) return (void)printf("(all zeros)\n");
    char year[5], month[3], day[3], hour[3], minute[3], second[3], hundredths[3];
    read_string((char*)dt,year,0,4);
    read_string((char*)dt,month,4,2);
    read_string((char*)dt,day,6,2);
    read_string((char*)dt,hour,8,2);
    read_string((char*)dt,minute,10,2);
    read_string((char*)dt,second,12,2);
    read_string((char*)dt,hundredths,14,2);
    if (!ensure_digits(year,4,"year")) return;
    if (!ensure_digits(month,2,"month")) return;
    if (!ensure_digits(day,2,"day")) return;
    if (!ensure_digits(hour,2,"hour")) return;
    if (!ensure_digits(minute,2,"minute")) return;
    if (!ensure_digits(second,2,"second")) return;
    if (!ensure_digits(hundredths,2,"hundredths")) return;
    int tzmin = (int8_t)dt[16] * 15;
    bool tzneg = (tzmin < 0);
    if (tzneg) tzmin *= -1;
    int tzhr = tzmin / 60;
    tzmin = tzmin % 60;
    printf("%s-%s-%s %s:%s:%s.%s %c%02d:%02d\n", year,month,day,hour,minute,second,hundredths,
           tzneg?'-':'+',tzhr,tzmin);
}

void print_pvd(uint8_t* secbuf) {
    printf("\tTYPE 1: PRIMARY VOLUME DESCRIPTOR\n");
    char sysId[33];
    char volId[33];
    char volSetId[129];
    char publisherId[129];
    char dataPrepId[129];
    char appId[129];
    char copyrightFileId[38];
    char abstractFileId[38];
    char biblioFileId[38];
    read_string((char*)secbuf,sysId,8,32);
    read_string((char*)secbuf,volId,40,32);
    read_string((char*)secbuf,volSetId,190,128);
    read_string((char*)secbuf,publisherId,318,128);
    read_string((char*)secbuf,dataPrepId,446,128);
    read_string((char*)secbuf,appId,574,128);
    read_string((char*)secbuf,copyrightFileId,702,37);
    read_string((char*)secbuf,abstractFileId,739,37);
    read_string((char*)secbuf,biblioFileId,776,37);
    u32_biendian_t volsize  = from_u32bi(secbuf+80);
    u16_biendian_t volcount = from_u16bi(secbuf+120);
    u16_biendian_t volseq   = from_u16bi(secbuf+124);
    u16_biendian_t blksize  = from_u16bi(secbuf+128);
    u32_biendian_t ptabsize = from_u32bi(secbuf+132);
    if (!u32bi_validate("Volume Space Size",volsize)) return;
    if (!u16bi_validate("Volume Set Size",volcount)) return;
    if (!u16bi_validate("Volume Sequence",volseq)) return;
    if (!u16bi_validate("Block Size",blksize)) return;
    if (!u32bi_validate("Path Table Size",ptabsize)) return;
    printf("\t\tSystem Id         = [%s]\n", sysId);
    printf("\t\tVolume Id         = [%s]\n", volId);
    printf("\t\tVolume Space Size = %u\n", volsize.le);
    printf("\t\tVolume Set Size   = %u\n", volcount.le);
    printf("\t\tVolume Sequence   = %u\n", volseq.le);
    printf("\t\tBlock Size        = %u\n", blksize.le);
    printf("\t\tPath Table Size   = %u\n", ptabsize.le);
    uint32_t ptab_le = from_u32le(secbuf+140);
    uint32_t ptabopt_le = from_u32le(secbuf+144);
    uint32_t ptab_be = from_u32be(secbuf+148);
    uint32_t ptabopt_be = from_u32be(secbuf+152);
    printf("\t\tPath Table     LE = %u\n", ptab_le);
    printf("\t\tPath Table Opt LE = %u\n", ptabopt_le);
    printf("\t\tPath Table     BE = %u\n", ptab_be);
    printf("\t\tPath Table Opt BE = %u\n", ptabopt_be);
    printf("\t\tVolume Set Id     = [%s]\n", volSetId);
    printf("\t\tPublisher Id      = [%s]\n", publisherId);
    printf("\t\tData Prep Id      = [%s]\n", dataPrepId);
    printf("\t\tApplication Id    = [%s]\n", appId);
    printf("\t\tCopyright File    = [%s]\n", copyrightFileId);
    printf("\t\tAbstract File     = [%s]\n", abstractFileId);
    printf("\t\tBiblio File       = [%s]\n", biblioFileId);
    printf("\t\tVolume Created    = "), print_datetime(secbuf+813);
    printf("\t\tVolume Modified   = "), print_datetime(secbuf+830);
    printf("\t\tVolume Expires    = "), print_datetime(secbuf+847);
    printf("\t\tVolume Effective  = "), print_datetime(secbuf+864);
    printf("\t\tFile Struct Ver   = 0x%02X\n", secbuf[881]);
    printf("\t\t=== APPLICATION USE AREA\n");
    dump_binary((char*)(secbuf+883),32);
}

void print_boot_record(char* secbuf,uint32_t *bootCatSector) {
    char bootSysId[33];
    char bootId[33];
    read_string(secbuf,bootSysId,7,32);
    read_string(secbuf,bootId,39,32);
    printf("\tTYPE 0: BOOT RECORD\n");
    printf("\t\tBoot System Id = [%s]\n", bootSysId);
    printf("\t\tBoot Id        = [%s]\n", bootId);
    if (strcmp(bootSysId,ELTORITO)!=0 || (strlen(bootId) > 0)) {
        printf("\t!!! NOT EL TORITO\n");
        return;
    }
    printf("\t\t=== EL TORITO FOUND\n");
    if (*bootCatSector != 0)
        printf("\t\t??? MULTIPLE EL TORITO BOOT RECORDS ???\n");
    *bootCatSector = from_u32le((uint8_t*)(secbuf + 71));
    printf("\t\tBOOT CATALOG SECTOR = %zu\n", (size_t)*bootCatSector);
}

const char* platform_id_decode(uint8_t platid) {
    switch (platid) {
        case 0: return "X86";
        case 1: return "PPC";
        case 2: return "Mac";
        case 0xEF: return "EFI";
        default: return NULL;
    }
}

const char* media_type_decode(uint8_t mtype) {
    switch (mtype) {
        case 0: return "NOEMU";
        case 1: return "FDD12";
        case 2: return "FDD144";
        case 3: return "FDD288";
        case 4: return "HDD";
        default: return NULL;
    }
}

void print_boot_initial(uint8_t* recbuf) {
    printf("\t\t--- Initial Entry\n");
    uint8_t bootinc = recbuf[0];
    if (bootinc != 0x88 && bootinc != 0x00) {
        printf("\t\t??? Unexpected boot indicator %02X\n",bootinc);
        return;
    }
    bool bootable = (bootinc == 0x88);
    uint8_t media_type = recbuf[1];
    const char* media_type_name = media_type_decode(media_type);
    printf("\t\tBOOTABLE   = %s\n",bootable ? "YES" : "NO");
    printf("\t\tMEDIA TYPE = 0x%02X (%s)\n",
           media_type,media_type_name != NULL ? media_type_name : "??? UNKNOWN");
    if (media_type_name == NULL) {
        printf("\t\t??? Unexpected media type ID %02X\n",media_type);
        return;
    }
    uint16_t loadseg = (recbuf[2]) | (recbuf[3] << 8);
    printf("\t\tLOAD SEG  = 0x%04X\n",loadseg);
    if (loadseg == 0)
        printf("\t\t[Load segment 0, assume default 0x7C00]\n");
    uint8_t systype = recbuf[4];
    printf("\t\tSYS TYPE  = 0x%02X\n",systype);
    if (recbuf[5] != 0)
        return (void)printf("\t\t??? Byte 0x05 unexpectedly non-zero\n");
    uint16_t sectors = (recbuf[6]) | (recbuf[7] << 8);
    uint32_t lba = from_u32le(recbuf + 8);
    printf("\t\tBOOT SEC  = %u [2048 byte sector]\n",lba);
    printf("\t\tSECTORS   = %u [512 byte sectors]\n",sectors);
}

void print_boot_catalog(uint8_t* secbuf) {
    // NOTE: In theory, a boot catalog can be longer than just one sector.
    // In practice, never seen it, doubt anyone supports it.
    printf("\t\t--- Validation Entry\n");
    if (secbuf[0] != 0x01) {
        printf("\t\t??? Validation entry missing\n");
        return;
    }
    if (secbuf[2] != 0 || secbuf[3] != 0) {
        printf("\t\t??? Reserved bytes 2-3 not zero\n");
        return;
    }
    if (secbuf[0x1E] != 0x55 || secbuf[0x1F] != 0xAA) {
        printf("\t\t??? Key missing or incorrect\n");
        return;
    }
    uint16_t checksum = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t lo = secbuf[2*i];
        uint8_t hi = secbuf[(2*i)+1];
        uint16_t word = lo | (hi << 8);
        checksum += word;
    }
    if (checksum != 0) {
        printf("\t\t??? Checksum %zu invalid!\n", (size_t)checksum);
        return;
    }
    uint8_t platid = secbuf[1];
    const char* platname = platform_id_decode(platid);
    printf("\t\tPlatform ID = 0x%02X (%s)\n",
           platid, platname != NULL ? platname : "??? UNKNOWN");
    char manufacturer[25];
    read_string((char*)secbuf,manufacturer,4,24);
    printf("\t\tManufacturer = [%s]\n",manufacturer);
    // Initial entry at offset 0x20
    print_boot_initial(secbuf + 0x20);
    // After initial entry can come "section entries".
    // I've never seen it in the wild so just ignore it.
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
        if (type == 1)
            print_pvd((uint8_t*)secbuf);
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
        print_boot_catalog((uint8_t*)secbuf);
    }
    fclose(fh);
    return 0;
}

// vim: et ts=4 sw=4 cc=121 tw=120 cino=l1(0
