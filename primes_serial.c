/*
 * primes_serial.c
 *  Circular queue tester
 * @author Jiri Kaspar, CVUT FIT
 *
 * verze 0.9.7  9.12.2017
 *
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

unsigned int *primes;
unsigned int max_prime;
unsigned int max_prime_sqrt;
unsigned int num=1;
unsigned int p=3;
FILE *f;

void appl_help () {
  printf("prime MAX \n");
}
//void appl_run (int thread, int stage, int index, CQhandle *input, CQhandle *output) {
//}
//void appl_stat () {
//}

void appl_init(int argc, char **argv) {
int a=1;

  if ( (argc > 1) && (sscanf (argv[1], "%u", &max_prime) != 1) ) argc = 1;
  if (argc < 2) {
    appl_help();
    exit(1);
  }
  primes = malloc( max_prime * sizeof(unsigned int) );
//  max_prime_sqrt = (int) sqrt( (double) max_prime ) + 1;
//  primes = malloc( max_prime_sqrt * sizeof(unsigned int) );
  primes[0]=2;
  f = fopen("primes.txt","w");
  fprintf (f,"%d\n", 2);
  while (p < max_prime) {
    for (int i=0; i<num; i++) {
      if (p % primes[i] == 0) break;
      if (p < primes[i]*primes[i]) {
	primes[num++]=p;
        fprintf (f,"%d\n", p);
	break;
      }
    }
  p+=2;
  }
  printf("prime: %d\n", primes[num-1]);
  fclose(f);
  free(primes);
}

int main(int argc, char **argv) {
  appl_init(argc, argv);
}
