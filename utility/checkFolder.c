/**
 * @file checkFolder.c
 *
 * @author Mario Nicola
 *
 * @date 24/08/2020
 *
 * @version 1.0.0
 *
 * @brief Function used to che the existence of a folder
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ngeneConstants.h"
#include "checkFolder.h"
const char *GetExecutableDirectory(void)
{
#ifdef NGENE_NATIVE_WINDOWS
    return NgNativeHome();
#else
    static char directory[MAX_ROW_LENGTH];
    ssize_t length;
    char *last;
    if (directory[0])
        return directory;
    length = readlink("/proc/self/exe", directory, sizeof(directory) - 1);
    if (length <= 0 || length >= (ssize_t)sizeof(directory) - 1) {
        directory[0] = '\0';
        return NULL;
    }
    directory[length] = '\0';
    last = strrchr(directory, '/');
    if (!last) {
        directory[0] = '\0';
        return NULL;
    }
    if (last == directory)
        last[1] = '\0';
    else
        *last = '\0';
    return directory;
#endif
}
int IsFolderExistent(char *folder)
{
    struct stat sb;
    if (stat(folder, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        // it exists
        return TRUE_FLAG;
    } else {
        // it does not exist
        return FALSE_FLAG;
    }
}
int GetFilenameFromFilePointer(FILE *fp, char *filename)
{
    char tmpPath[MAX_ROW_LENGTH]; // temp variable used to get the filename
    char outPath[MAX_ROW_LENGTH]; // complete path of the target file
    int len;
    int idx;
    // terminate the output string
    filename[0] = '\0';
    // if the file pointer is not valid, terminate!
    if (fp == NULL) {
        return FALSE_FLAG;
    }
#ifdef NGENE_NATIVE_WINDOWS
    {
        HANDLE handle = (HANDLE)_get_osfhandle(_fileno(fp));
        DWORD count =
            GetFinalPathNameByHandleA(handle, outPath, sizeof(outPath), FILE_NAME_NORMALIZED);
        char *last;
        if (!count || count >= sizeof(outPath))
            return FALSE_FLAG;
        last = strrchr(outPath, '\\');
        snprintf(filename, MAX_ROW_LENGTH, "%s", last ? last + 1 : outPath);
        return TRUE_FLAG;
    }
#else
    // read file descriptor and use to address the file in proc
    sprintf(tmpPath, "/proc/self/fd/%d", fileno(fp));
    // get the complete path of the target file
    len = readlink(tmpPath, outPath, MAX_ROW_LENGTH - 1);
    if ((len < 1) || (len >= MAX_ROW_LENGTH)) {
        // something went wrong
        return FALSE_FLAG;
    }
    // the path string is not terminated
    outPath[len] = '\0';
    // extract the filename (find the last '/')
    idx = len - 1;
    while ((idx >= 0) && (outPath[idx] != '/'))
        idx--;
    if (idx < 0)
        idx = 0;
    // output
    sprintf(filename, "%s", &(outPath[idx]));
    return TRUE_FLAG;
#endif
}
