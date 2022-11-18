#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to set the kth bit of n
char setBit(char n, int k){
    if(k<8){    //per evitare di spostare l'1 di troppe posizioni
        return (n | (1 << k));
    }
    else{
        return n;
    }
}

// Function to clear the kth bit of n
int clearBit(int n, int k){
    return (n & (~(1 << (k - 1))));
}
// non ancora finita

// Function to read the kth bit of n
int readBit(char n, int k){
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