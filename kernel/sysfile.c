//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
//#include "user/user.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "buf.h"

//#include <string.h>
struct superblock sb;
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
	int fd;
	struct file *f;

	if(argint(n, &fd) < 0)
		return -1;
	if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
		return -1;
	if(pfd)
		*pfd = fd;
	if(pf)
		*pf = f;
	return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
	int fd;
	struct proc *curproc = myproc();

	for(fd = 0; fd < NOFILE; fd++){
		if(curproc->ofile[fd] == 0){
			curproc->ofile[fd] = f;
			return fd;
		}
	}
	return -1;
}

int
sys_dup(void)
{
	struct file *f;
	int fd;

	if(argfd(0, 0, &f) < 0)
		return -1;
	if((fd=fdalloc(f)) < 0)
		return -1;
	filedup(f);
	return fd;
}

int
sys_read(void)
{
	struct file *f;
	int n;
	char *p;

	if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
		return -1;
	return fileread(f, p, n);
}

int
sys_write(void)
{
	struct file *f;
	int n;
	char *p;

	if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
		return -1;
	return filewrite(f, p, n);
}

int
sys_close(void)
{
	int fd;
	struct file *f;

	if(argfd(0, &fd, &f) < 0)
		return -1;
	myproc()->ofile[fd] = 0;
	fileclose(f);
	return 0;
}

int
sys_fstat(void)
{
	struct file *f;
	struct stat *st;

	if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
		return -1;
	return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
	char name[DIRSIZ], *new, *old;
	struct inode *dp, *ip;
	//cprintf("LINKOVANJE");
	if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
		return -1;

	begin_op();
	if((ip = namei(old)) == 0){
		end_op();
		return -1;
	}

	ilock(ip);
	if(ip->type == T_DIR){
		iunlockput(ip);
		end_op();
		return -1;
	}

	ip->nlink++;
	iupdate(ip);
	iunlock(ip);

	if((dp = nameiparent(new, name)) == 0)
		goto bad;
	ilock(dp);
	if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
		iunlockput(dp);
		goto bad;
	}
	iunlockput(dp);
	iput(ip);

	end_op();

	return 0;

bad:
	ilock(ip);
	ip->nlink--;
	iupdate(ip);
	iunlockput(ip);
	end_op();
	return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
	int off;
	struct dirent de;

	for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
		if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
			panic("isdirempty: readi");
		if(de.inum != 0)
			return 0;
	}
	return 1;
}

int
sys_unlink(void)
{
	struct inode *ip, *dp;//dp -parent
	struct dirent de;
	char name[DIRSIZ], *path;
	uint off;
	if(argstr(0, &path) < 0)
		return -1;

	begin_op();
	if((dp = nameiparent(path, name)) == 0){
		end_op();
		return -1;
	}
	ilock(dp);
	// Cannot unlink "." or "..".
	if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
		goto bad;

	if((ip = dirlookup(dp, name, &off)) == 0)
		goto bad;
	ilock(ip);

	if(ip->nlink < 1)
		panic("unlink: nlink < 1");
	if(ip->type == T_DIR && !isdirempty(ip)){
		iunlockput(ip);
		goto bad;
	}
	if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
	 	panic("unlink: readi");
	de.del=1;
	//memset(&de, 0, sunlockizeof(de));//ispunjava nulama
	if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
	 	panic("unlink: writei");//ovo isto brise

	if(ip->type == T_DIR){
		dp->nlink--;
		iupdate(dp);
	}
	iunlockput(dp);
	ip->type=0;
	ip->nlink--;
	iupdate(ip);
	iunlockput(ip);
	end_op();
	return 0;

bad:
	iunlockput(dp);
	end_op();
	return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
	struct inode *ip, *dp;
	char name[DIRSIZ];

	if((dp = nameiparent(path, name)) == 0)
		return 0;
	ilock(dp);

	if((ip = dirlookup(dp, name, 0)) != 0){
		iunlockput(dp);
		ilock(ip);
		if(type == T_FILE && ip->type == T_FILE)
			return ip;
		iunlockput(ip);
		return 0;
	}

	if((ip = ialloc(dp->dev, type)) == 0)
		panic("create: ialloc");

	ilock(ip);
	ip->major = major;
	ip->minor = minor;
	ip->nlink = 1;
	iupdate(ip);

	if(type == T_DIR){  // Create . and .. entries.
		dp->nlink++;  // for ".."
		iupdate(dp);
		// No ip->nlink++ for ".": avoid cyclic ref count.
		if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
			panic("create dots");
	}

	if(dirlink(dp, name, ip->inum) < 0)
		panic("create: dirlink");

	iunlockput(dp);

	return ip;
}

static int
bcheck(int dev, uint b)
{
	struct buf *bp;
	int bi, m;

	bp = bread(dev, BBLOCK(b, sb));
	bi = b % BPB;
	m = 1 << (bi % 8);
	if((bp->data[bi/8] & m) != 0 )
		return -1;
	//bp->data[bi/8] &= m;
	//log_write(bp);
	return 1;
	brelse(bp);
}
static void
bnotfree(int dev, uint b)
{
	struct buf *bp;
	int bi, m;

	bp = bread(dev, BBLOCK(b, sb));
	bi = b % BPB;
	m = 1 << (bi % 8);
	bp->data[bi/8] |= m;
	log_write(bp);
	brelse(bp);
}


int
sys_open(void)
{
	char *path;
	int fd, omode;
	struct file *f;
	struct inode *ip;

	if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
		return -1;
	//cprintf("%s\n",path );
	begin_op();

	if(omode & O_CREATE){
		ip = create(path, T_FILE, 0, 0);
		if(ip == 0){
			end_op();
			return -1;
		}
	} else {
		if((ip = namei(path)) == 0){
			end_op();
			return -1;
		}
		ilock(ip);
		if(ip->type == T_DIR && omode != O_RDONLY){
			iunlockput(ip);
			end_op();
			return -1;
		}
	}

	if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
		if(f)
			fileclose(f);
		iunlockput(ip);
		end_op();
		return -1;
	}
	iunlock(ip);
	end_op();

	f->type = FD_INODE;
	f->ip = ip;
	f->off = 0;
	f->readable = !(omode & O_WRONLY);
	f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
	return fd;
}

int
sys_mkdir(void)
{
	char *path;
	struct inode *ip;

	begin_op();
	if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
		end_op();
		return -1;
	}
	iunlockput(ip);
	end_op();
	return 0;
}

int
sys_lsdel(void)
{
	struct inode *ip;
	struct dirent de;
	char *path;
	char *result;
	int off;

	begin_op();
	if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
		cprintf("GRESKA\n");
		end_op();
		return -1;
	}
	if(argstr(1, &result)< 0){
		cprintf("GRESKA\n");
	}
	int rcount=0;
	for(off = 0; off < ip->size; off += sizeof(de)){
		if(readi(ip, (char*)&de, off, sizeof(de)) != sizeof(de))
			panic("dirlookup read");
		if(de.inum == 0)
			continue;
		if(de.del==1){
			//cprintf("%d\n",ip->type);
			memcpy(result+rcount*14,de.name,strlen(de.name));
			rcount++;
		}

	}
	end_op();

	return rcount;
}

int
sys_rec(void)
{
	char *path;
	struct inode *dp, *ip;
	struct dirent de;
	struct dirent recovered;
	int off;

	begin_op();
	if(argstr(0, &path) < 0 || (dp = nameiparent(path,path)) == 0){
		cprintf("GRESKA\n");
		end_op();
		return -1;
	}


	int recOff;
	for(off = 0; off < dp->size; off += sizeof(de)){
		if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
			panic("dirlookup read");
		if(de.inum == 0)
			continue;
			if(de.del==1){//&& de.name ime da se uporedi
				de.del=0;
				//cprintf("Ime povracenog: %s\n",de.name);
				recovered=de;
				recOff=off;
			}
			if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
				panic("dirlookup read");
		}
		//de.inum();
		if((ip=dirlookup(dp,de.name,recOff))<0){
			cprintf("GRESKA");
			end_op();
			return -1;
		};
/*
			int i, j;
			struct buf *bp;
			uint *a;

			for(i = 0; i < NDIRECT; i++){
				if(ip->addrs[i]){
					if(bcheck(ip->dev, ip->addrs[i])>0){
						bnotfree(ip->dev, ip->addrs[i]);
					}
					else{
						cprintf("Ne moze da se povrati");
						brelse(bp);
						end_op();
					}
				}
			}

			if(ip->addrs[NDIRECT]){
				bp = bread(ip->dev, ip->addrs[NDIRECT]);
				a = (uint*)bp->data;
				for(j = 0; j < NINDIRECT; j++){
					if(a[j]){
						if(bcheck(ip->dev,  a[j])>0){
							bnotfree(ip->dev,  a[j]);
						}
						else{
							cprintf("Ne moze da se povrati");
							brelse(bp);
							end_op();
						}
					}
					//	bfree(ip->dev, a[j]);
				}
				brelse(bp);
			//	bfree(ip->dev, ip->addrs[NDIRECT]);
			if(bcheck(ip->dev, ip->addrs[NDIRECT])>0){
				bnotfree(ip->dev, ip->addrs[NDIRECT]);
				end_op();
			}
			else{
					cprintf("Ne moze da se povrati");
			}
			}
*/
			//ip->size = 0;
			iupdate(ip);


	end_op();

	return 0;
}


int
sys_mknod(void)
{
	struct inode *ip;
	char *path;
	int major, minor;

	begin_op();
	if((argstr(0, &path)) < 0 ||
			argint(1, &major) < 0 ||
			argint(2, &minor) < 0 ||
			(ip = create(path, T_DEV, major, minor)) == 0){
		end_op();
		return -1;
	}
	iunlockput(ip);
	end_op();
	return 0;
}

int
sys_chdir(void)
{
	char *path;
	struct inode *ip;
	struct proc *curproc = myproc();

	begin_op();
	if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
		end_op();
		return -1;
	}
	ilock(ip);
	if(ip->type != T_DIR){
		iunlockput(ip);
		end_op();
		return -1;
	}
	iunlock(ip);
	iput(curproc->cwd);
	end_op();
	curproc->cwd = ip;
	return 0;
}

int
sys_exec(void)
{
	char *path, *argv[MAXARG];
	int i;
	uint uargv, uarg;

	if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
		return -1;
	}
	memset(argv, 0, sizeof(argv));
	for(i=0;; i++){
		if(i >= NELEM(argv))
			return -1;
		if(fetchint(uargv+4*i, (int*)&uarg) < 0)
			return -1;
		if(uarg == 0){
			argv[i] = 0;
			break;
		}
		if(fetchstr(uarg, &argv[i]) < 0)
			return -1;
	}
	return exec(path, argv);
}

int
sys_pipe(void)
{
	int *fd;
	struct file *rf, *wf;
	int fd0, fd1;

	if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
		return -1;
	if(pipealloc(&rf, &wf) < 0)
		return -1;
	fd0 = -1;
	if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
		if(fd0 >= 0)
			myproc()->ofile[fd0] = 0;
		fileclose(rf);
		fileclose(wf);
		return -1;
	}
	fd[0] = fd0;
	fd[1] = fd1;
	return 0;
}
