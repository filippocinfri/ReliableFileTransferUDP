#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

void charToBinary(char c[], char* binary, int numOfChar);
void binaryToChar(char* binary, char* text);
void readRequest(char* binary);
void list(char* binary);

//Converte una stringa in un ulteriore stringa, rappresentativa della stessa in binario. Potendo specificare il numero di char da convertire.
void charToBinary(char buff[], char* binary, int numOfChar) { 
	//ATTENZIONE BINARY DEVE ESSERE VUOTO, Binary passata come argomento per poter fare la free()
	
    int bin; //Variabile momentania, o 0 o 1 per il calcolo del numero binario completo
	char ch[2];
	
	for (int j = 0; j < numOfChar ; j++) {
		for (int i = 0; i < 8; i++) {
		
			bin = ((buff[j] << i) & 0x80) ? 1 : 0;
			sprintf(ch, "%d", bin); // Conversione da intero (bin) in stringa (ch)
			strcat(binary,ch); // Creazione della stringa completa con il numero binario, concatena 
		}
	}
}

//Converte una stringa binaria nella sua rappresentazione testuale
void binaryToChar(char *binary, char *text) {
    char subbuff[16];
    unsigned char c;
    int index = 0;
    int binaryLength = strlen(binary);

    for(int k = 0; k < binaryLength; k += 8) {    
        memcpy(subbuff, &binary[k], 8);                   // <--- copy 8 butes only
        //subbuff[8] = '\0';  
        c = (unsigned char)strtol(subbuff, NULL, 2);
        text[index++] = c;
        //text[index] = '\0';
    }
    printf("Result = %s\n", text);
}

//Legge il comando scritto da tastiera dal client e lo elabora chiamando la relativa funzione
void readRequest(char* binary){
    if (strncmp(binary,"00",2) == 0);
    //else if (strncmp(binary,"01",2) == 0) get();
    //else if (strncmp(binary,"10",2) == 0) put();
}

//Svolge il lavoro del comado list, legge tutti i file in una cartella e per ora li stampa a schermo
void list(char* buff){
    DIR *folder;
    struct dirent *file;
    folder = opendir("./Files"); //open all or present directory
    if (folder) {
        while ((file = readdir(folder)) != NULL) {
            //Condition to check regular file.
            if(file->d_type == DT_REG ) {
                strcat(buff,file->d_name);
                printf("%s\n",file->d_name); //Da inviare invece di printare sul server
            }
        }
        closedir(folder);
    }
}


/* COSE CHE NON SERVONO MA POTREBBERO TORNARE UTILI IN CASO DI PIU CONTROLLI 
void binaryToText(char *binary, int binaryLength, char *text, int symbolCount);
void formatBinary(char *input, int length, char *output);
unsigned long binaryToDecimal(char *binary, int length);
int validate(char *binary); 
int main(){
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
int validate(char *binary){ 
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
*/
