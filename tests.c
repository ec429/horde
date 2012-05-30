#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include "bits.h"

#define CWD_BUF_SIZE	4096

typedef struct
{
	bool out;
	char *text;
	unsigned int sl; // source line
}
line;

typedef struct
{
	char *name;
	char *path;
	char *prog;
	unsigned int nlines;
	line *lines;
}
test;

int rcreaddir(const char *dn);
int rcread(const char *fn, const char *pn);
int do_fork(const char *prog, int pipes[2]);

bool debug=false;

unsigned int ntests;
test *tests;

int main(void)
{
	if(rcreaddir("."))
	{
		fprintf(stderr, "tests: bad test, giving up\n");
		return(EXIT_FAILURE);
	}
	bool haverr=false;
	for(unsigned int i=0;i<ntests;i++)
	{
		test *t=tests+i;
		if(debug) fprintf(stderr, "tests: %s: %s\n", t->prog, t->name);
		int pipes[2];
		if(do_fork(t->prog, pipes))
		{
			fprintf(stderr, "tests: failed to fork '%s', giving up\n", t->prog);
			return(EXIT_FAILURE);
		}
		unsigned int errs=0;
		for(unsigned int j=0;j<t->nlines;j++)
		{
			line *l=t->lines+j;
			if(l->out)
			{
				fd_set readfds;
				FD_SET(pipes[0], &readfds);
				if(select(pipes[0]+1, &readfds, NULL, NULL, &(struct timeval){.tv_sec=4, .tv_usec=0})<0)
				{
					perror("tests: select");
					return(EXIT_FAILURE);
				}
				if(!FD_ISSET(pipes[0], &readfds))
				{
					fprintf(stderr, "tests: timeout in test '%s' at line %u\nexpected %s\n", t->name, l->sl, l->text);
					haverr=true;
					errs++;
				}
				else
				{
					char *o=getl(pipes[0]);
					if(!o)
					{
						fprintf(stderr, "tests: o==NULL\n");
						return(EXIT_FAILURE);
					}
					if(strcmp(o, l->text)!=0)
					{
						fprintf(stderr, "tests: error in test '%s' at line %u\nexpected %s\nreceived %s\n", t->name, l->sl, l->text, o);
						haverr=true;
						errs++;
					}
					free(o);
				}
			}
			else
			{
				write(pipes[1], l->text, strlen(l->text));
				write(pipes[1], "\n", 1);
			}
		}
		write(pipes[1], "(kill)\n", 7);
		close(pipes[0]);
		close(pipes[1]);
		wait(NULL);
		if(errs)
		{
			fprintf(stderr, "tests: %u errors in test '%s'\n\ttest path is '%s'\n\tprogram is '%s'\n", errs, t->name, t->path, t->prog);
		}
	}
	fprintf(stderr, "tests: ran %u tests: %s\n", ntests, haverr?"errors were found":"all passed OK");
	return(haverr?1:0);
}

int rcreaddir(const char *dn)
{
	char *dh=NULL;
	if(strcmp(dn+strlen(dn)-6, ".tests")==0)
	{
		if(!(dh=strndup(dn, strlen(dn)-6)))
		{
			perror("tests: strndup");
			return(1);
		}
	}
	if(debug) fprintf(stderr, "tests: searching %s for tests\n", dn);
	DIR *rcdir=opendir(dn);
	if(!rcdir)
	{
		fprintf(stderr, "tests: failed to opendir %s: %s\n", dn, strerror(errno));
		closedir(rcdir);
		free(dh);
		return(1);
	}
	struct dirent *entry;
	while((entry=readdir(rcdir)))
	{
		if(entry->d_name[0]=='.') continue;
		char *nn=malloc(strlen(dn)+strlen(entry->d_name)+2);
		if(!nn)
		{
			perror("tests: malloc");
			closedir(rcdir);
			free(dh);
			return(1);
		}
		sprintf(nn, "%s/%s", dn, entry->d_name);
		struct stat st;
		if(stat(nn, &st))
		{
			if(debug) fprintf(stderr, "tests: failed to stat %s: %s\n", nn, strerror(errno));
			closedir(rcdir);
			free(dh);
			free(nn);
			return(1);
		}
		if(st.st_mode&S_IFDIR)
		{
			if(rcreaddir(nn))
			{
				free(dh);
				free(nn);
				return(1);
			}
		}
		else if(strcmp(entry->d_name+strlen(entry->d_name)-5, ".test")==0)
		{
			if(dh)
			{
				if(rcread(nn, dh))
				{
					closedir(rcdir);
					free(dh);
					free(nn);
					return(1);
				}
			}
			else
			{
				fprintf(stderr, "tests: found '%s', but directory '%s' is not of the form <prog>.tests\n", entry->d_name, dn);
				closedir(rcdir);
				free(nn);
				return(1);
			}
		}
		free(nn);
	}
	if(debug) fprintf(stderr, "tests: done searching %s\n", dn);
	closedir(rcdir);
	free(dh);
	return(0);
}

int rcread(const char *fn, const char *pn)
{
	if(!pn)
	{
		fprintf(stderr, "tests: rcread: pn==NULL\n");
		return(1);
	}
	FILE *rc=fopen(fn, "r");
	if(rc)
	{
		test t={.name=NULL, .prog=strdup(pn), .path=NULL, .nlines=0, .lines=NULL};
		if(!t.prog)
		{
			perror("tests: strdup");
			fclose(rc);
			return(1);
		}
		t.path=strdup(fn);
		if(!t.path)
		{
			perror("tests: strdup");
			free(t.prog);
			fclose(rc);
			return(1);
		}
		if(debug) fprintf(stderr, "tests: reading test file '%s'\n", fn);
		char *fline;
		unsigned int sl=0;
		while((fline=fgetl(rc)))
		{
			sl++;
			if(!*fline)
			{
				free(fline);
				if(feof(rc)) break;
				continue;
			}
			size_t end;
			while(fline[(end=strlen(fline))-1]=='\\')
			{
				fline[end-1]=0;
				char *cont=fgetl(rc);
				if(!cont) break;
				char *newl=realloc(fline, strlen(fline)+strlen(cont)+1);
				if(!newl)
				{
					free(cont);
					break;
				}
				strcat(newl, cont);
				free(cont);
				fline=newl;
				sl++;
			}
			if((!fline[0])||(fline[1]!=' '))
			{
				fprintf(stderr, "tests: choked on test file '%s'\n", fn);
				free(fline);
				free(t.prog);
				free(t.path);
				fclose(rc);
				return(1);
			}
			switch(*fline)
			{
				case '$':
					t.name=strdup(fline+2);
				break;
				case '<':
				case '>':;
					line l={.out=(*fline=='>'), .text=strdup(fline+2), .sl=sl};
					unsigned int n=t.nlines++;
					line *nl=realloc(t.lines, t.nlines*sizeof(line));
					if(!nl)
					{
						perror("tests: realloc");
						return(1);
					}
					(t.lines=nl)[n]=l;
				break;
				case '#':
					// discard it
				break;
				default:
					fprintf(stderr, "tests: choked on test file '%s'\n", fn);
					free(fline);
					fclose(rc);
					free(t.prog);
					free(t.path);
					return(1);
				break;
			}
			free(fline);
		}
		fclose(rc);
		if(debug) fprintf(stderr, "tests: finished reading test file\n");
		unsigned int n=ntests++;
		test *nt=realloc(tests, ntests*sizeof(test));
		if(!nt)
		{
			perror("tests: realloc");
			free(t.prog);
			free(t.path);
			return(1);
		}
		(tests=nt)[n]=t;
		return(0);
	}
	fprintf(stderr, "tests: failed to open test file '%s': fopen: %s\n", fn, strerror(errno));
	return(1);
}

int do_fork(const char *prog, int pipes[2])
{
	struct stat stbuf;
	if(stat(prog, &stbuf)) return(-1); // not there (or can't stat for some other reason)
	if(access(prog, X_OK)) return(-1); // can't execute
	int s[2][2];
	if(pipe(s[0])==-1)
	{
		perror("tests: pipe");
		return(-1);
	}
	if(pipe(s[1])==-1)
	{
		perror("tests: pipe");
		close(s[0][0]);
		close(s[0][1]);
		return(-1);
	}
	pid_t pid;
	switch((pid=fork()))
	{
		case -1: // error
			perror("tests: fork");
			close(s[0][0]);
			close(s[0][1]);
			close(s[1][0]);
			close(s[1][1]);
			return(-1);
		break;
		case 0:; // chld
			if(dup2(s[0][0], STDIN_FILENO)==-1)
				perror("tests: chld: dup2(0)");
			if(dup2(s[1][1], STDOUT_FILENO)==-1)
				perror("tests: chld: dup2(1)");
			close(s[0][1]);
			close(s[1][0]);
			execl(prog, prog, NULL);
			// still here?  then it failed
			perror("tests: execl");
			exit(EXIT_FAILURE);
		break;
		default: // parent
			pipes[0]=s[1][0];
			pipes[1]=s[0][1];
			close(s[0][0]);
			close(s[1][1]);
		break;
	}
	return(0);
}
