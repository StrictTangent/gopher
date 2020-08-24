#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>

char ** arg_parse (char *line, int *argcptr);
int countArgs(char* line);
void createArgArray(char** argArray, char* line);
void cleanArgs(char** args);
void removeQuotes(char* str);

// Returns an array of strings representing
// command line arguments passed within a single string.
char ** arg_parse (char *line, int *argcptr){

  char   **aryptr;  // The array of strings to return

  // Count arguments. Return NULL if no arguments.
  if ((*argcptr = countArgs(line)) < 1) return NULL;

  // Allocate memory for array. 
  // Print error & return NULL if unsuccesful.
  aryptr = (char**) malloc(sizeof(char*) * (*argcptr + 1));
  if (aryptr == NULL) {
    fprintf(stderr, "ush: unable to allocate memory for argument list\n");
    *argcptr = 0;
    return NULL;
  }

  // Populate array and strip quotes.
  createArgArray(aryptr, line);
  cleanArgs(aryptr); 
  return aryptr;
}

// Returns count of individual arguments in a string.
int countArgs(char* line) {

  int count = 0;  // The number of arguments

  //While not EOS, find arguments.
  while (*line != 0) {
    while(*line== ' '){
      line++; 
    }
    //If argument found, find end of argument.
    if (*line != 0){
      count += 1;
      while(*line != ' ' && *line != 0){
        //Skip to closing quote if quote found.
        if (*line == '\"'){
          line++;
          while(*line != '\"'){
            //If no closing quote encountered, print error & return
            if (*line == 0) {
              fprintf(stderr, "ush: odd number of quotes\n");
              return -1;
            }
            line++;
          }
        }
        line++;
      }
    }
  }
  return count;
}

// Populates a char** array with individual arguments in a string
void createArgArray(char** argArray, char* line){

  //While not EOS, find next argument.
  while (*line != 0) {
    while(*line == ' '){
      line++; 
    }
    //If not EOS: assign pointer, find end of arg, turn space to 0
    if (*line != 0){
      *argArray = line;
      argArray++;
      
      while(*line != ' ' && *line != 0){
        //Skip to closing quote if quote found.
        if (*line == '\"'){
          line++;
          while(*line != '\"'){
            line++;
          }
        }
        line++;
      }
      if ((*line) != 0) {
	*line = 0;
	line++;
      }
    }
  }

  *argArray = NULL;
}

// Strips double-qoute character from all strings in an array.
void cleanArgs(char** args){
  while(*args != NULL){
    removeQuotes(*args);
    args++;
  }
}

// Strips double-quote character from a single string.
void removeQuotes(char* str){
  char* src = str;  // Pointer for source character
  char* dst = str;  // Pointer for destination character

  while (*src != 0){
    if(*src != '\"'){
      *dst = *src;
      dst++;
    }
    src++;
  }
  *dst = 0;
}
