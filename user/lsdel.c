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
	char result[896];
	char *ptr=result;
	int r;
	if((r=lsdel(path,result))){
			printf("Vratio je %d rezultata \n",r);

		for(int i=0;i<r;i++){
			for (int j=0;j<14;j++)
				printf("%c", result[i*14+j]);
			printf("\n");
		}
	}
	else{
		printf("Nema obrisanih fajlova \n");
	}
}

int
main(int argc, char *argv[])
{
	int i;

	if(argc < 2){
		lsdeleted(".");
		exit();
	}
	for(i=1; i<argc; i++){
		printf("Argv %s\n",argv[i] );
		lsdeleted(argv[i]);}

	exit();
}
