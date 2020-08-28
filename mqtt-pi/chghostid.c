
#include <stdio.h>
#include <unistd.h>
 
int main() {
long id,res;
 
id = gethostid();
printf("current hostid is: %x\n",id);
res = sethostid(0xa030d01);                    
if (res == 0) printf("change hostid, success! (%d) \n",res);
id = gethostid();
printf("hostid change to: %x \n",id);
}

