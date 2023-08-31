#include <pyreleases.h>

int main(void) {

	ActivateVirtualTerminalEscapes();

	LPWSTR SERVER = L"www.python.org";
	LPWSTR ACCESS_POINT = L"/downloads/windows/";

	SCRHANDLES hScrStructs = HttpGet(SERVER, ACCESS_POINT);

	LPSTR pszHtml = ReadHttpResponse(hScrStructs);

	if (!pszHtml) {
		return 1;
	}

	DWORD dwStableReleasesSize = 0;
	LPSTR pszStable = GetStableReleases(pszHtml, RESP_BUFF_SIZE, &dwStableReleasesSize);
	
	if (!pszStable) {
		return 1;
	}

	ParsedPyStructs ppsResult = DeserializeStableReleases(pszStable, dwStableReleasesSize);

	if (!ppsResult.pyStart) {
		fprintf_s(stderr, "Error in DeserializeStableReleases, a NULL buffer returned.\n");
		return 1;
	}

	CHAR lpszVersion[BUFF_SIZE] = { 0 };
	GetPythonVersion(lpszVersion, BUFF_SIZE);
#ifdef _DEBUG
	printf_s("%u Python releases have been parsed.\n", ppsResult.dwStructCount);
	printf_s("Installed Python version is %s", lpszVersion);
#endif // _DEBUG
	PrintPythonReleases(ppsResult, lpszVersion);

	free(pszHtml);
	free(pszStable);
	free(ppsResult.pyStart);

	return 0;
}