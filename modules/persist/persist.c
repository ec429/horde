#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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

int write8(int fd, off_t addr, uint8_t val);
int write32(int fd, off_t addr, uint32_t val);
int writecons(int fd, off_t addr, uint32_t car, uint32_t cdr);
int read8(int fd, off_t addr, uint8_t *val);
int read32(int fd, off_t addr, uint32_t *val);
int readcons(int fd, off_t addr, uint32_t *car, uint32_t *cdr);
int dbinit(int dbfd);
uint32_t db_alloc(int dbfd, size_t sz);
uint32_t db_store(int dbfd, const char *name, uint8_t type, uint8_t *data); // returns address of stored value

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
				if(dbinit(dbfd))
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

int dbinit(int dbfd)
{
	if(flock(dbfd, LOCK_EX))
	{
		perror("persist: dbinit: flock");
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
		fprintf(stderr, "persist: dbinit: offset not 17 after writing, something bad happened\n");
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
			sz=9;
		break;
		case T_U8:
			sz=1;
		break;
		case T_U16:
			sz=2;
		break;
		case T_U32:
			sz=4;
		break;
		case T_BYTES:
			sz=0;
		break;
		case T_CSTR:
			sz=data?strlen((const char *)data)+1:1;
		break;
		case T_FLOAT:
			sz=4;
		break;
	}
	if(!sz)
	{
		fprintf(stderr, "persist: db_store: unknown size\n");
		return(0);
	}
	uint32_t nameent, namecar, namecdr;
	if(read32(dbfd, 4, &nameent))
		return(0);
	if(!nameent)
	{
		if(!(nameent=db_alloc(dbfd, 9)))
		{
			fprintf(stderr, "persist: db_store: failed to create namelist\n");
			return(0);
		}
		if(write32(dbfd, 4, nameent))
		{
			fprintf(stderr, "persist: db_store: failed to write namelist addr\n");
			// TODO db_free nameent
			return(0);
		}
		if(!(namecar=db_alloc(dbfd, 9)))
		{
			fprintf(stderr, "persist: db_store: failed to allocate namecar\n");
			// TODO db_free nameent
			return(0);
		}
		namecdr=0;
		if(writecons(dbfd, nameent, namecar, namecdr))
		{
			fprintf(stderr, "persist: db_store: failed to write namelist\n");
			// TODO db_free nameent
			return(0);
		}
	}
	else
	{
		fprintf(stderr, "persist: db_store: namelist iter not done yet\n");
		return(0);
	}
	size_t namelen=1+strlen(name);
	uint32_t namecaar, namecdar;
	if(!(namecaar=db_alloc(dbfd, namelen)))
	{
		fprintf(stderr, "persist: db_store: failed to allocate space for name\n");
		return(0);
	}
	if(!(namecdar=db_alloc(dbfd, sz)))
	{
		fprintf(stderr, "persist: db_store: failed to allocate space for value\n");
		return(0);
	}
	if(writecons(dbfd, namecar, namecaar, namecdar))
	{
		fprintf(stderr, "persist: db_store: failed to write nameent\n");
		return(0);
	}
	if(write8(dbfd, namecaar, T_CSTR))
	{
		fprintf(stderr, "persist: db_store: failed to write name\n");
		return(0);
	}
	ssize_t b=write(dbfd, name, namelen);
	if(b<(ssize_t)namelen)
	{
		fprintf(stderr, "persist: db_store: failed to write name\n");
		if(b<0)
			perror("\twrite");
		return(0);
	}
	if(write8(dbfd, namecdar, type))
	{
		fprintf(stderr, "persist: db_store: failed to write value\n");
		return(0);
	}
	b=write(dbfd, data, sz);
	if(b<(ssize_t)sz)
	{
		fprintf(stderr, "persist: db_store: failed to write value\n");
		if(b<0)
			perror("\twrite");
		return(0);
	}
	return(namecdar);
}
