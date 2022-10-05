#include "kernel/types.h"
#include "user.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"

void
recover(char *path)
{
	if(rec(path)){

	}
}

int
main(int argc, char *argv[])
{
	int i;

	if(argc < 2){
		recover(".");
		exit();
	}
	for(i=1; i<argc; i++){
		//printf("Argv %s\n",argv[i] );
		recover(argv[i]);
  }

	exit();
}
