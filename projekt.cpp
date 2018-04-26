#include <iostream>
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

//stworzSciezke tworzy pelna sciezke do podanego pliku/folderu 
void stworzSciezke(char * sciezka, char * a , char * b){
	memset(sciezka,0,sizeof sciezka);
	strcat(sciezka,a);
	strcat(sciezka,"/");
	strcat(sciezka,b);
}

/*szkieletDemon - tworzy nowy process ktory jest demonem*/
static void szkieletDemon(){
        pid_t pid, sid;
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }
        umask(0);
        openlog (NULL, LOG_PID, LOG_DAEMON);
        sid = setsid();
        if (sid < 0) {
                exit(EXIT_FAILURE);
        }
        if ((chdir("/")) < 0) {
                exit(EXIT_FAILURE);
        }
}

void usunFolder(char * sciezka){
	char sciezka1[PATH_MAX];
	DIR * folder = opendir(sciezka);
	struct stat buf;
	struct dirent *dir;
	while(dir = readdir(folder)) {
		if(dir->d_name[0] == '.'){
			continue;
		}
		stworzSciezke(sciezka1, sciezka ,dir->d_name);
		stat(sciezka1,&buf) ;
		if(S_ISDIR(buf.st_mode)){
			usunFolder(sciezka1);
		}else remove(sciezka1);	
	}
	closedir(folder);
	remove(sciezka);
}

void obudzDemona(int signum){
	if(signum == SIGUSR1){
		syslog (LOG_NOTICE, "Obudzono demona" );
	}
}

/*uruchomSynchronizacje - przeprowadza synchronizacje dwoch folderow, w zaleznosci od trybu rekurencyjnego(-R) badz tez zwyklego*/
void uruchomSynchronizacje(char * folder1 , char * folder2, char tryb){
	char sciezka1[PATH_MAX];
	char sciezka2[PATH_MAX];

	DIR * zrodloF= opendir(folder1);
	if( zrodloF == NULL || folder1[0] != '/')
   	{
   	   	syslog (LOG_NOTICE,"BĹÄdny pierwszy argument");
      	exit(EXIT_FAILURE);
   	}
	DIR * celF= opendir(folder2);
	if(celF == NULL){
		mkdir(folder2, 0777);
		celF = opendir(folder2);
		string message = "Stworzono folder " + string(folder2) ;
		syslog (LOG_NOTICE, message.c_str() );
	}
    if( celF == NULL || folder2[0] != '/')
   	{
   	   	syslog (LOG_NOTICE,"BĹÄdny drugi argument");
      	exit(EXIT_FAILURE);
   	}

	struct dirent *zrodloFolder;
	struct dirent *celFolder;
	struct stat buf1;
	struct stat buf2;
	//petla odpowiedzialna za usuwanie plikow (i folderow w trybie rekurencyjnym) ktore istnieja w  folderze potomnym , a zostaly usuniete w folderze maciezystym
	while(celFolder = readdir(celF)) {
		if(celFolder->d_name[0] == '.'){
			continue;
		}
		stworzSciezke(sciezka1, folder1 ,celFolder->d_name);
		stworzSciezke(sciezka2, folder2 ,celFolder->d_name);
		stat(sciezka1,&buf1) ;
		stat(sciezka2,&buf2) ;
		if(S_ISDIR(buf2.st_mode)){
			DIR * dir1= opendir(sciezka1);
			DIR * dir2= opendir(sciezka2);
			if( dir2 == NULL )
			{
				syslog (LOG_NOTICE,"Problem z folderem(u)");
				exit(EXIT_FAILURE);
			}
			if( dir1 == NULL ){
				usunFolder(sciezka2);
			}
			string message = "Usunieto folder " +  string(sciezka2) ;
			syslog (LOG_NOTICE, message.c_str() );
			closedir(dir1);
			closedir(dir2);
		}else
		if(S_ISREG(buf2.st_mode)){
			
			int cel= open(sciezka2,O_RDONLY);
			if( cel == -1 )
			{
				syslog (LOG_NOTICE,"Problem z plikiem(u)");
				exit(EXIT_FAILURE);
			}
			int zrodlo = open(sciezka1, O_RDONLY);
			if( zrodlo == -1 )
			{
				remove(sciezka2);
			}
			string message = "Usunieto plik " +  string(sciezka2) ;
			syslog (LOG_NOTICE, message.c_str() );
			close(cel);
			close(zrodlo);
		}
	}
	//petla odpowiedzialna za kopiowanie plikow (i folderow w trybie rekurencyjnym) , ktore zostaly modyfikowane lub stworzone w folderze macierzystym do foldery potomnego
	while(zrodloFolder = readdir(zrodloF)) {
		if(zrodloFolder->d_name[0] == '.'){
			continue;
		}
		stworzSciezke(sciezka1, folder1 ,zrodloFolder->d_name);
		stat(sciezka1,&buf1);
		stworzSciezke(sciezka2, folder2 ,zrodloFolder->d_name);
		stat(sciezka2,&buf2);
		if(S_ISDIR(buf1.st_mode)){
			if(tryb == 'R'){
				uruchomSynchronizacje(sciezka1, sciezka2, tryb);
			}	
		}else{
			if(!(S_ISREG(buf2.st_mode)) || buf1.st_mtime > buf2.st_mtime){
				int zrodlo = open(sciezka1, O_RDONLY);
				if( zrodlo == -1 )
				{
					syslog (LOG_NOTICE,"Problem z plikiem(r)");
					exit(EXIT_FAILURE);
				}
				int cel= creat(sciezka2, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
				if( cel == -1 )
				{
					syslog (LOG_NOTICE,"Problem z plikiem(w)");
					exit(EXIT_FAILURE);
				}
				if(buf1.st_size < 1048576){//granica uzywania read/mmap
					char * bufor=new char[1024];
					int ch=1;
					while( ch!=0 ){
						ch=read(zrodlo, bufor , 1024);
						write(cel, bufor, ch);
					}
				}else{
					void* mmapped = mmap(NULL , buf1.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, cel, 0);
					if(mmapped == MAP_FAILED){
						syslog (LOG_NOTICE,"Problem z tworzeniem mapy");
						exit(EXIT_FAILURE);
					}
					write(1, mmapped, buf1.st_size);
					int rc = munmap(mmapped, buf1.st_size);
					if(rc != 0){
						syslog (LOG_NOTICE,"Problem z czyszczeniem mapy");
						exit(EXIT_FAILURE);
					}
				}
				string message = "Stworzona kopia pliku " +  string(sciezka2) ;
				syslog (LOG_NOTICE, message.c_str() );
				close(cel);
				close(zrodlo);
			}
		}
	}
	closedir(zrodloF);
	closedir(celF);
}

int main(int argc, char ** argv){
    if( argc > 6 || argc < 3 )
   	{
     	syslog (LOG_NOTICE,"ZĹa iloĹÄ argumentĂłw");
      	exit(EXIT_FAILURE);
   	}
	int r=0 ,t=0;
	int sleepTime = 300;

    szkieletDemon();
	signal(SIGUSR1, obudzDemona);
    while (1)
    {
		syslog (LOG_NOTICE, "Obudzenie demona" );
    
		r=0, t=0;
		for(int i=3; i<argc ; ++i){
			if(  argv[i][0]=='-' || argv[i][1]=='R'){
				r=i;
			}else
			if(argv[i][0]=='-' || argv[i][1]=='t'){
				if(argc == i){
					syslog (LOG_NOTICE,"ZĹa iloĹÄ argumentĂłw");
      				exit(EXIT_FAILURE);
				}else
					t = atoi(argv[i+1]);
			}else{
				
			}
		}
		if(r>0){
			uruchomSynchronizacje(argv[1],argv[2], 'R');
		}else{
			uruchomSynchronizacje(argv[1],argv[2], '-');
		}
		if(t>0){
			sleepTime = t;
		}

		syslog (LOG_NOTICE, "Uspienie demona" );
        sleep (sleepTime);
    }

    syslog (LOG_NOTICE, "Zniszczenie demona");
    closelog();

    return EXIT_SUCCESS;
}