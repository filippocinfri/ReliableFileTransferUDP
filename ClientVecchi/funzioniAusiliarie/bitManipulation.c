#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to set the kth bit of n
char setBit(char n, int k)
{
    if(k<8){    //per evitare di spostare l'1 di troppe posizioni
        return (n | (1 << k));
    }
    else{
        return n;
    }
}

// Function to clear the kth bit of n
int clearBit(int n, int k)
{
    return (n & (~(1 << (k - 1))));
}
// non ancora finita

// Function to set the kth bit of n
int readBit(char n, int k)
{
    if(k<8){    //per evitare di spostare l'1 di troppe posizioni
        if (n & (1 << k)){
            return 1;
        }
        return 0; 
    }
    else{
        return -1;
    }
}

/*
int main(){
    
    char mychars[10];
    int * intlocation = (int*)(&mychars[5]);
    *intlocation = 3632; // stores 3632
    printf("%d \n ", *intlocation);
    
    char * c = malloc(1);
    strcpy(c, "\0");
    printf("%d 0\n", c[0]);
    
    printf("\n\n");
    printf("%d 1\n", setBit(c[0], 0));
    printf("%d 1\n", readBit(setBit(c[0], 0), 0));
    printf("%d 0\n", readBit(setBit(c[0], 0), 1));
    printf("%d -1\n", readBit(setBit(c[0], 0), 8));
    printf("\n\n");
    
    printf("%d 2\n", setBit(c[0], 1));
    printf("%d 4\n", setBit(c[0], 2));
    printf("%d 8\n", setBit(c[0], 3));
    printf("%d 16\n", setBit(c[0], 4));
    printf("%d 32\n", setBit(c[0], 5));
    printf("%d 64\n", setBit(c[0], 6));
    printf("%d 128\n", setBit(c[0], 7));
    printf("%d 0\n", setBit(c[0], 8));
    printf("%d 0\n", setBit(c[0], 9));
    
    
    char car = setBit(c[0], 2);
    strncpy(c, &car, 1);
    printf("%c \n", c[0]);
    
    
    exit(0);
}
*/
