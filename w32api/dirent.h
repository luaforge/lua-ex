#include <windows.h>

typedef struct DIR_tag DIR;
DIR *opendir(const char *name);
const WIN32_FIND_DATA *readdir(DIR *pi);
void closedir(DIR *pi);
