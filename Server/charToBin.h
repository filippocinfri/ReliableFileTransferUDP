#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

void binaryToText(char *binary, int binaryLength, char *text, int symbolCount);
void formatBinary(char *input, int length, char *output);
unsigned long binaryToDecimal(char *binary, int length);
int validate(char *binary);
char* charToBinary(char c[], char* binary);
void readRequest(char* binary);
void list(void);

/* int main(){
	char* buff = "a";
    char* binary = (char*)malloc (sizeof (char) * 100);
	binary = charToBinary(buff,binary);
    readRequest(binary);
    list();
    
    int binaryLength = strlen(binary);
    int symbolCount = binaryLength / 8 + 1;
    char *text = malloc(symbolCount + 1);
    char *formattedBinary = malloc(binaryLength + 1);
    if(text == NULL || formattedBinary == NULL)
        exit(1);
    if(binaryLength % 8 == 0)
        --symbolCount;
            
    formatBinary(binary, binaryLength, formattedBinary);
    binaryToText(formattedBinary, strlen(formattedBinary), text, symbolCount);
    printf("%s in binary is the following text: %s\n", binary, text);
        
    free(text);
    free(formattedBinary);
    free(binary);
    
	return 0;
}*/

void readRequest(char* binary){
    char commandBin[2];
    strncpy(commandBin,binary,2);
    printf("%s\n",binary);

    switch (*commandBin){
        case '00' : list();}
        /*case '01' : get();
        case '10' : put();
    }*/
}

void list(void){
    DIR *d;
    struct dirent *dir;
    d = opendir("./File"); //open all or present directory
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            //Condition to check regular file.
            if(dir->d_type == DT_REG ) {
                printf("%s\n",dir->d_name); //Da inviare invece di printare sul server
            }
        }
        closedir(d);
    }
}

char* charToBinary(char c[], char* binary) { //Binary passata come argomento per poter fare la free()
	
	int bin; //Variabile momentania, o 0 o 1 per il calcolo del numero binario completo
	char ch[2];
	
	for (int j = 0; j < strlen(c) ; j++) {
		for (int i = 0; i < 8; i++) {
		
			bin = ((c[j] << i) & 0x80) ? 1 : 0;
			sprintf(ch, "%d", bin); // Conversione da intero (bin) in stringa (ch)
			strcat(binary,ch); // Creazione della stringa completa con il numero binario, concatena 
		}
	}
	return binary;
}

void binaryToText(char *binary, int binaryLength, char *text, int symbolCount){
    int i;
    for(i = 0; i < binaryLength; i+=8, binary += 8){
        char *byte = binary;
        byte[8] = '\0';
        *text++ = binaryToDecimal(byte, 8);
    }
    text -= symbolCount;
}

void formatBinary(char *input, int length, char *output){
    while(*input){
        if(*input == '0' || *input == '1'){
            *output++ = *input++;
        }
        else{
            ++input;
            --length;
        }
    }
    output -= length;
}

int validate(char *binary){ //Non serve
	while(*binary){
		if((*binary != '0') && (*binary != '1') && (*binary != ' '))
			return 0;
        ++binary;
	}
	return 1;
}

unsigned long binaryToDecimal(char *binary, int length){
	int i;
	unsigned long decimal = 0;
	unsigned long weight = 1;
	binary += length - 1;
	weight = 1;
	for(i = 0; i < length; ++i, --binary)
	{
		if(*binary == '1')
			decimal += weight;
		weight *= 2;
	}
	
	return decimal;
}
