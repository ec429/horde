#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "bits.h"

/*
	WARNINGS
	* This version of persist doesn't support fine-grained locking; it flock()s the entire file for the entire duration of its run.
		Finer-grained locking might require fdatasync() usage, but I think O_SYNC covers us
	* Since flock() is only an advisory lock, it's possible for another process to futz with the file while persist has it locked.  Don't do that!  It could lead to severe data corruption.
*/

#define TYPEMASK	0xF0
#define T_CONS	0x00
#define T_U8	0x80
#define T_U16	0x90
#define T_U32	0xA0
#define T_BYTES	0xB0
#define T_CSTR	0xC0
#define T_FLOAT	0xF0

#define SZ_CONS		9
#define SZ_U8		2
#define SZ_U16		3
#define SZ_U32		5
#define SZ_FLOAT	5

int write8(int fd, off_t addr, uint8_t val);
int write32(int fd, off_t addr, uint32_t val);
int writecons(int fd, off_t addr, uint32_t car, uint32_t cdr);
int read8(int fd, off_t addr, uint8_t *val);
int read32(int fd, off_t addr, uint32_t *val);
int readcons(int fd, off_t addr, uint32_t *car, uint32_t *cdr);
char *readcstr(int dbfd, uint32_t addr);
int db_init(int dbfd);
uint32_t db_alloc(int dbfd, size_t sz); // returns allocated address
uint32_t db_store(int dbfd, const char *name, uint8_t type, uint8_t *data); // returns address of stored value
uint32_t name_lookup(int dbfd, const char *name); // returns address of namecar (whose car is object, cdr is name)

int main(void)
{
	int dbfd=open("test.per", O_RDWR | O_SYNC, S_IRWXU | S_IRWXG);
	if(dbfd<0)
	{
		if(errno==ENOENT)
		{
			dbfd=open("test.per", O_RDWR | O_SYNC | O_CREAT, S_IRWXU | S_IRWXG);
			if(dbfd<0)
			{
				perror("persist: open");
				return(1);
			}
			else
			{
				if(db_init(dbfd))
				{
					fprintf(stderr, "persist: failed to initialise db\n");
					return(1);
				}
			}
		}
		else
		{
			perror("persist: open");
			return(1);
		}
	}
	if(flock(dbfd, LOCK_EX))
	{
		perror("persist: flock");
		close(dbfd);
		return(1);
	}
	uint8_t ans=42;
	if(!db_store(dbfd, "Answer", T_U8, &ans))
	{
		fprintf(stderr, "persist: db_store failed\n");
		return(1);
	}
	flock(dbfd, LOCK_UN);
	close(dbfd);
	return(0);
}

int db_init(int dbfd)
{
	if(flock(dbfd, LOCK_EX))
	{
		perror("persist: db_init: flock");
		close(dbfd);
		return(1);
	}
	if(write32(dbfd, 0, 8))
	{
		close(dbfd);
		flock(dbfd, LOCK_UN);
		return(1);
	}
	if(write32(dbfd, 4, 0))
	{
		close(dbfd);
		flock(dbfd, LOCK_UN);
		return(1);
	}
	if(writecons(dbfd, 8, 17, 0))
	{
		close(dbfd);
		flock(dbfd, LOCK_UN);
		return(1);
	}
	if(writecons(dbfd, 17, 26, -26))
	{
		close(dbfd);
		flock(dbfd, LOCK_UN);
		return(1);
	}
	if(lseek(dbfd, 0, SEEK_CUR)!=26)
	{
		fprintf(stderr, "persist: db_init: offset not 26 after writing, something bad happened\n");
		flock(dbfd, LOCK_UN);
		return(1);
	}
	flock(dbfd, LOCK_UN);
	return(0);
}

int write8(int fd, off_t addr, uint8_t val)
{
	if(lseek(fd, addr, SEEK_SET)!=addr)
	{
		perror("persist: write8: lseek");
		return(1);
	}
	ssize_t b=write(fd, &val, 1);
	if(b<0)
	{
		perror("persist: write8: write");
		return(1);
	}
	if(b<1)
	{
		fprintf(stderr, "persist: write8: short count\n");
		return(1);
	}
	return(0);
}

int write32(int fd, off_t addr, uint32_t val)
{
	if(lseek(fd, addr, SEEK_SET)!=addr)
	{
		perror("persist: write32: lseek");
		return(1);
	}
	ssize_t b=write(fd, (const uint8_t [4]){val>>24, val>>16, val>>8, val}, 4);
	if(b<0)
	{
		perror("persist: write32: write");
		return(1);
	}
	if(b<4)
	{
		fprintf(stderr, "persist: write32: short count\n");
		return(1);
	}
	return(0);
}

int writecons(int fd, off_t addr, uint32_t car, uint32_t cdr)
{
	if(write8(fd, addr, T_CONS))
		return(1);
	if(write32(fd, addr+1, car))
		return(1);
	if(write32(fd, addr+5, cdr))
		return(1);
	return(0);
}

int read8(int fd, off_t addr, uint8_t *val)
{
	if(lseek(fd, addr, SEEK_SET)!=addr)
	{
		perror("persist: read8: lseek");
		return(1);
	}
	uint8_t buf;
	if(read(fd, &buf, 1)<1)
	{
		fprintf(stderr, "persist: read8: short read\n");
		return(1);
	}
	if(val)
		*val=buf;
	return(0);
}

int read32(int fd, off_t addr, uint32_t *val)
{
	if(lseek(fd, addr, SEEK_SET)!=addr)
	{
		perror("persist: read32: lseek");
		return(1);
	}
	uint8_t buf[4];
	if(read(fd, buf, 4)<4)
	{
		fprintf(stderr, "persist: read32: short read\n");
		return(1);
	}
	if(val)
		*val=(buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
	return(0);
}

int readcons(int fd, off_t addr, uint32_t *car, uint32_t *cdr)
{
	uint8_t type;
	if(read8(fd, addr, &type))
	{
		fprintf(stderr, "persist: readcons: failed to read type\n");
		return(1);
	}
	if((type&TYPEMASK)!=T_CONS)
	{
		fprintf(stderr, "persist: readcons: type!=T_CONS\n");
		return(2);
	}
	if(read32(fd, addr+1, car))
	{
		fprintf(stderr, "persist: readcons: failed to read car\n");
		return(1);
	}
	if(read32(fd, addr+5, cdr))
	{
		fprintf(stderr, "persist: readcons: failed to read cdr\n");
		return(1);
	}
	return(0);
}

char *readcstr(int dbfd, uint32_t addr)
{
	char *buf; size_t l,i;
	init_char(&buf, &l, &i);
	uint8_t b;
	do
	{
		if(read8(dbfd, addr, &b))
		{
			fprintf(stderr, "persist: readcstr: failed to read byte at 0x%08x\n", addr);
			free(buf);
			return(NULL);
		}
		append_char(&buf, &l, &i, (char)b);
		addr++;
	}
	while(b);
	return(buf);
}

uint32_t db_alloc(int dbfd, size_t sz)
{
	uint32_t freeent, freecar, freecdr;
	if(read32(dbfd, 0, &freeent))
	{
		fprintf(stderr, "persist: db_alloc: failed to find freeent\n");
		return(0);
	}
	while(freeent)
	{
		if(readcons(dbfd, freeent, &freecar, &freecdr))
		{
			fprintf(stderr, "persist: db_alloc: failed to read freeent\n");
			return(0);
		}
		if(!freecar)
		{
			fprintf(stderr, "persist: db_alloc: freecar==nil\n");
			return(0);
		}
		uint32_t freeaddr, freelen;
		if(readcons(dbfd, freecar, &freeaddr, &freelen))
		{
			fprintf(stderr, "persist: db_alloc: failed to read freecar\n");
			return(0);
		}
		if(sz<=freelen)
		{
			writecons(dbfd, freecar, freeaddr+sz, freelen-sz);
			return(freeaddr);
		}
		freeent=freecdr;
	}
	fprintf(stderr, "persist: db_alloc: freeent==nil\n");
	return(0);
}

uint32_t db_store(int dbfd, const char *name, uint8_t type, uint8_t *data)
{
	if(!name)
	{
		fprintf(stderr, "persist: db_store: name==NULL\n");
		return(0);
	}
	size_t sz=0;
	switch(type&TYPEMASK)
	{
		case T_CONS:
			sz=SZ_CONS;
		break;
		case T_U8:
			sz=SZ_U8;
		break;
		case T_U16:
			sz=SZ_U16;
		break;
		case T_U32:
			sz=SZ_U32;
		break;
		case T_BYTES:
			sz=0;
		break;
		case T_CSTR:
			sz=data?strlen((const char *)data)+2:2;
		break;
		case T_FLOAT:
			sz=SZ_FLOAT;
		break;
	}
	if(!sz)
	{
		fprintf(stderr, "persist: db_store: unknown size\n");
		return(0);
	}
	uint32_t namehead;
	if(read32(dbfd, 4, &namehead))
	{
		fprintf(stderr, "persist: db_store: failed to read namehead at 0x4\n");
		return(0);
	}
	uint32_t nameptr=namehead, namecar, namecdr, oldptr=0;
	while(nameptr)
	{
		if(readcons(dbfd, nameptr, &namecar, &namecdr))
		{
			fprintf(stderr, "persist: db_store: failed to read namelist cons at 0x%08x\n", nameptr);
			return(0);
		}
		uint32_t namecaar, namecdar;
		if(readcons(dbfd, namecar, &namecaar, &namecdar))
		{
			fprintf(stderr, "persist: db_store: failed to read namelist entry at 0x%08x\n", namecar);
			return(0);
		}
		char *buf=readcstr(dbfd, namecaar);
		if(strcmp(name, buf)==0)
		{
			free(buf);
			break;
		}
		free(buf);
		oldptr=nameptr;
		nameptr=namecdr;
	}
	if(namecar)
	{
		
	}
	else
	{
		uint32_t nameent=db_alloc(dbfd, SZ_CONS);
		if(!nameent)
		{
			fprintf(stderr, "persist: db_store: failed to allocate space for new nameent\n");
			return(0);
		}
		if(!namehead)
		{
			if(write32(dbfd, 4, namehead=nameent))
			{
				fprintf(stderr, "persist: db_store: failed to write namehead at 0x4\n");
				//db_free(dbfd, nameent);
				return(0);
			}
		}
		else if(!oldptr)
		{
			fprintf(stderr, "persist: db_store: internal error (oldptr==NULL)\n");
			//db_free(dbfd, nameent);
			return(0);
		}
		else
		{
			if(writecons(dbfd, oldptr, namecar, nameent))
			{
				fprintf(stderr, "persist: db_store: failed to write to oldptr\n");
				//db_free(dbfd, nameent);
				return(0);
			}
		}
		if(!(namecar=db_alloc(dbfd, SZ_CONS)))
		{
			fprintf(stderr, "persist: db_store: failed to allocate space for new namecar\n");
			//db_free(dbfd, nameent);
			return(0);
		}
		if(writecons(dbfd, nameent, namecar, 0))
		{
			fprintf(stderr, "persist: db_store: failed to write to nameent\n");
			//db_free(dbfd, namecar);
			//db_free(dbfd, nameent);
			return(0);
		}
		uint32_t nameaddr=db_alloc(dbfd, strlen(name)+1);
		if(!nameaddr)
		{
			fprintf(stderr, "persist: db_store: failed to allocate space for name\n");
			//db_free(dbfd, namecar);
			//db_free(dbfd, nameent);
			return(0);
		}
		size_t i=0;
		do
			if(write8(dbfd, nameaddr+i, name[i]))
			{
				fprintf(stderr, "persist: db_store: failed to write out name\n");
				//db_free(dbfd, addr);
				//db_free(dbfd, nameaddr);
				//db_free(dbfd, namecar);
				//db_free(dbfd, nameent);
				return(0);
			}
		while(name[i++]);
	}
	uint32_t addr=db_alloc(dbfd, sz);
	if(!addr)
	{
		fprintf(stderr, "persist: db_store: failed to allocate space for data\n");
		//db_free(dbfd, nameaddr);
		//db_free(dbfd, namecar);
		//db_free(dbfd, nameent);
		return(0);
	}
	if(write8(dbfd, addr, type))
	{
		fprintf(stderr, "persist: db_store: failed to write out type\n");
		//db_free(dbfd, addr);
		//db_free(dbfd, nameaddr);
		//db_free(dbfd, namecar);
		//db_free(dbfd, nameent);
		return(0);
	}
	for(size_t i=1;i<sz;i++)
		if(write8(dbfd, addr+i, data[i-1]))
		{
			fprintf(stderr, "persist: db_store: failed to write out data\n");
			//db_free(dbfd, addr);
			//db_free(dbfd, nameaddr);
			//db_free(dbfd, namecar);
			//db_free(dbfd, nameent);
			return(0);
		}
	if(writecons(dbfd, namecar, namecar, 0)) // XXX
	{
		fprintf(stderr, "persist: db_store: failed to write to nameent\n");
		//db_free(dbfd, namecar);
		//db_free(dbfd, nameent);
		return(0);
	}
	return(addr);
}

uint32_t name_lookup(int dbfd, const char *name)
{
	uint32_t namehead;
	if(read32(dbfd, 4, &namehead))
	{
		fprintf(stderr, "persist: name_lookup: failed to read namehead at 0x4\n");
		return(0);
	}
	uint32_t nameptr=namehead, namecar, namecdr;
	while(nameptr)
	{
		if(readcons(dbfd, nameptr, &namecar, &namecdr))
		{
			fprintf(stderr, "persist: name_lookup: failed to read namelist cons at 0x%08x\n", nameptr);
			return(0);
		}
		uint32_t namecaar, namecdar;
		if(readcons(dbfd, namecar, &namecaar, &namecdar))
		{
			fprintf(stderr, "persist: name_lookup: failed to read namelist entry at 0x%08x\n", namecar);
			return(0);
		}
		char *buf=readcstr(dbfd, namecaar);
		if(strcmp(name, buf)==0)
		{
			free(buf);
			return(namecar);
		}
		free(buf);
		nameptr=namecdr;
	}
	// name not found
	return(0);
}
