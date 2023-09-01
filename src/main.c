#include <pyreleases.h>

int wmain(void) {

	activate_vtes();

	const wchar_t* const SERVER = L"www.python.org";
	const wchar_t* const ACCESS_POINT = L"/downloads/windows/";

	hscr_t scr_struct = http_get(SERVER, ACCESS_POINT);

	char* html_content = read_http_response(scr_struct);

	if (!html_content) return 1;

	uint64_t stable_release_chunksize = 0;
	char* stable_releases_start = get_stable_releases(html_content, RESP_BUFF_SIZE, &stable_release_chunksize);
	
	if (!stable_releases_start) return 2;

	parsedstructs_t parse_results = deserialize_stable_releases(stable_releases_start, stable_release_chunksize);

	if (!parse_results.py_start) {
		fprintf_s(stderr, "Error in DeserializeStableReleases, a NULL buffer returned.\n");
		return 1;
	}

	char py_version[BUFF_SIZE] = { 0 };
	get_installed_python_version(py_version, BUFF_SIZE);

#ifdef _DEBUG
	printf_s("%u python_t releases have been parsed.\n", parse_results.struct_count);
	printf_s("Installed python_t version is %s", py_version);
#endif // _DEBUG

	print_python_releases(parse_results, py_version);

	free(html_content);
	free(stable_releases_start);
	free(parse_results.py_start);

	return 0;
}