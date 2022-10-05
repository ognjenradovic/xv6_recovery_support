#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"
//u lsdel bi trebalo da zovemo sys_lsdel(char*path,char *result)
//u result bi se ispisala pretraga i to bi printovali
//ovde moramo da proveravamo da li je putanja validna
//msm da moze samo da se uzme od lsa

void
lsdeleted(char *path)
{
	char buf[512], *p;
	int fd;
	struct dirent de;
	struct stat st;

	if((fd = open(path, 0)) < 0){
		fprintf(2, "lsdeleted: cannot open %s\n", path);
		return;
	}

	if(fstat(fd, &st) < 0){
		fprintf(2, "lsdeleted: cannot stat %s\n", path);
		close(fd);
		return;
	}

	switch(st.type){
	case T_FILE:
	if(st.type==7){
		printf("%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
		}
		break;

	case T_DIR:
		if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
			printf("lsdeleted: path too long\n");
			break;
		}
		strcpy(buf, path);
		p = buf+strlen(buf);
		*p++ = '/';
		while(read(fd, &de, sizeof(de)) == sizeof(de)){
			if(de.inum == 0)
				continue;
			memmove(p, de.name, DIRSIZ);
			p[DIRSIZ] = 0;
			if(stat(buf, &st) < 0){
				printf("lsdeleted: cannot stat %s\n", buf);
				continue;
			}
			if(st.type==7){
	printf("%s %d %d %d %d\n", fmtname(buf), st.type, st.ino, st.size,de.del);
			}
		}
		break;
	}
	close(fd);
}

int
main(int argc, char *argv[])
{

}
