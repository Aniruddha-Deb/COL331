#include "types.h"
#include "stat.h"
#include "user.h"

// int
// atoi(char *s) {
//   int a = 0;
//   int i = 0;
//   int sign = 1;
//   while (s[i] != '\0') {
//     if (i == 0 && s[i] == '-') sign = -1;
//     else a = a*10 + (48-s[i]);
//     i++;
//   }
// 
//   return a*sign;
// }

int
main(int argc, char *argv[])
{
  if(argc != 3){
    printf(2, "Usage: add <num1> <num2>\n");
    exit();
  }
  int n1 = atoi(argv[1]);
  int n2 = atoi(argv[2]);
  printf(2, "%d + %d = %d\n", n1, n2, add(n1, n2));
  exit();
}
