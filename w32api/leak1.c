#include <stdio.h>
#include <assert.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>

int main(void)
{
	HANDLE h;
	FILE *f;
	h = CreateFile("NUL", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	assert(h != INVALID_HANDLE_VALUE);
	f = _fdopen(_open_osfhandle(h, _O_RDONLY), "r");
	assert(f != 0);
	assert(fgetc(f) == EOF);
    fclose(f);
#if 0
	{
		char buf[20];
		DWORD len;
		fprintf(stderr, "read: %d\n", ReadFile(h, buf, sizeof buf, &len, 0));
		fprintf(stderr, "error: %lu\n", GetLastError());
		fprintf(stderr, "len: %lu\n", len);
	}
#endif
	fprintf(stderr, "%d\n", CloseHandle(h));
	fprintf(stderr, "error: %lu\n", GetLastError());
    return 0;
}
