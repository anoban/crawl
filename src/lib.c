#include <pyreleases.h>


// May be unnecessary, since Windows consoles seem to be sensitive to VTEs without manually customizing the 
// Win32 console mode API. At least in these days. MS examples often include this step though! :(
bool activate_vtes(void) {
	HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (console_handle == INVALID_HANDLE_VALUE) {
		fprintf_s(stderr, "Error %lu in GetStdHandle.\n", GetLastError());
		return false;
	}
	uint32_t console_mode = 0;
	if (!GetConsoleMode(console_handle, &console_mode)) {
		fprintf_s(stderr, "Error %lu in GetConsoleMode.\n", GetLastError());
		return false;
	}
	console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(console_handle, console_mode)) {
		fprintf_s(stderr, "Error %lu in SetConsoleMode.\n", GetLastError());
		return false;
	}
	return true;
}



hscr_t http_get(_In_ const wchar_t* restrict server_name, _In_ const wchar_t* restrict access_point) {

	/*
	* A convenient wrapper around WinHttp functions.
	* Allows to send a GET request and receive the response in one function call
	* without having to deal with the cascade of WinHttp callbacks.
	*/

	HINTERNET session_handle = NULL, connection_handle = NULL, request_handle = NULL;
	bool http_send_reqest_status = false;
	hscr_t scr_handles = { .session_handle = NULL, .connection_handle = NULL, .request_handle = NULL };

	// Returns a valid session handle if successful, or NULL otherwise.
	// first of the WinHTTP functions called by an application. 
	// It initializes internal WinHTTP data structures and prepares for future calls from the application.
	session_handle = WinHttpOpen(
		// impersonating Firefox to avoid request denials.
		L"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/116.0",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);

	if (session_handle) {
		// Specifies the initial target server of an HTTP request and returns an HINTERNET connection handle
		// to an HTTP session for that initial target.
		// Returns a valid connection handle to the HTTP session if the connection is successful, or NULL otherwise.
		connection_handle = WinHttpConnect(
			session_handle,
			server_name,
			INTERNET_DEFAULT_HTTP_PORT,		// Uses port 80 for HTTP and port 443 for HTTPS.
			0);

	}
	else {
		fprintf_s(stderr, "Error %lu in WinHttpOpen.\n", GetLastError());
		goto premature_return;
	}

	if (connection_handle) {
		// Creates an HTTP request handle.
		// An HTTP request handle holds a request to send to an HTTP server and contains all 
		// RFC822/MIME/HTTP headers to be sent as part of the request.
		request_handle = WinHttpOpenRequest(
			connection_handle,
			L"GET",
			access_point,
			NULL,	// Pointer to a string that contains the HTTP version. If this parameter is NULL, the function uses HTTP/1.1
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, // Pointer to a null-terminated array of string pointers that 
			// specifies media types accepted by the client.
			// WINHTTP_DEFAULT_ACCEPT_TYPES, no types are accepted by the client. 
			// Typically, servers handle a lack of accepted types as indication that the client accepts 
			// only documents of type "text/*"; that is, only text documents & no pictures or other binary files
			0);
	}
	else {
		fprintf_s(stderr, "Error %lu in WinHttpConnect.\n", GetLastError());
		WinHttpCloseHandle(session_handle);
		goto premature_return_sh;
	}

	if (request_handle) {
		// Sends the specified request to the HTTP server.
		// Returns true if successful, or false otherwise.
		http_send_reqest_status = WinHttpSendRequest(
			request_handle,
			WINHTTP_NO_ADDITIONAL_HEADERS,	// A pointer to a string that contains the additional headers to append to the request.
			0,	// An unsigned long integer value that contains the length, in characters, of the additional headers.
			WINHTTP_NO_REQUEST_DATA,	// A pointer to a buffer that contains any optional data to send immediately after the request headers
			0,	// An unsigned long integer value that contains the length, in bytes, of the optional data.
			0,	// An unsigned long integer value that contains the length, in bytes, of the total data sent.
			0);	// A pointer to a pointer-sized variable that contains an application-defined value that is passed, with the request handle, to any callback functions.
	}
	else {
		fprintf_s(stderr, "Error %lu in the WinHttpOpenRequest.\n", GetLastError());
		goto premature_return_ch;
	}

	if (!http_send_reqest_status) {
		fprintf_s(stderr, "Error %lu in the WinHttpSendRequest.\n", GetLastError());
		goto premature_return_rh;
	}

	// these 3 handles need to be closed by the caller.
	scr_handles.session_handle = session_handle;
	scr_handles.connection_handle = connection_handle;
	scr_handles.request_handle = request_handle;

	return scr_handles;

// cleanup
premature_return_rh:	WinHttpCloseHandle(request_handle);
premature_return_ch:	WinHttpCloseHandle(connection_handle);
premature_return_sh:	WinHttpCloseHandle(session_handle);
premature_return:	return scr_handles;
}



char* read_http_response(_In_ const hscr_t scr_handles, _Inout_ uint64_t* const restrict response_size) {

	// if the call to HttpGet() failed,
	if (scr_handles.session_handle == NULL || scr_handles.connection_handle == NULL || scr_handles.request_handle == NULL) {
		fprintf_s(stderr, "read_http_response failed. Possible errors in previous call to http_get.\n");
		return NULL;
	}

	// this procedure is required to close these three handles.
	HINTERNET session_handle = scr_handles.session_handle,
		connection_handle = scr_handles.connection_handle,
		request_handle = scr_handles.request_handle;

	// Calling malloc first and then calling realloc in a do while loop is terribly inefficient for a 
	// simple app sending a single GET request.
	// So, malloc all the needed memory beforehand and use a moving pointer to keep track of the
	// last write offset, so the next write operation can start from there such that we can prevent
	// overwriting previously written memory.

	char* buffer = (char*) malloc(RESP_BUFF_SIZE);	// now that's 1 MiB.
	// if malloc() failed,
	if (!buffer) {
		fputs("malloc returned NULL", stderr);
		return NULL;
	}

	memset(buffer,0U, RESP_BUFF_SIZE);		// zero out the buffer.

	bool is_received = WinHttpReceiveResponse(request_handle, NULL);

	if (!is_received) {
		fprintf_s(stderr, "Error %lu in WinHttpReceiveResponse.\n", GetLastError());
		free(buffer);
		return NULL;
	}

	uint64_t total_bytes_in_response = 0, total_bytes_read_from_response = 0;
	// uint32_t because Win32 expects DWORDs.
	uint32_t bytes_in_current_query = 0, bytes_read_from_current_query = 0;

	do {
		// for every iteration, zero these counters since these are specific to each query.
		bytes_in_current_query = bytes_read_from_current_query = 0;

		if (!WinHttpQueryDataAvailable(request_handle, &bytes_in_current_query)) {
			fprintf_s(stderr, "Error %lu in WinHttpQueryDataAvailable.\n", GetLastError());
			break;
		}

		// If there aren't any more bytes to read,
		if (!bytes_in_current_query) break;

		if (!WinHttpReadData(request_handle, buffer + total_bytes_read_from_response,
			bytes_in_current_query, &bytes_read_from_current_query)) {
			fprintf_s(stderr, "Error %lu in WinHttpReadData.\n", GetLastError());
			break;
		}

		// Increment the total counters.
		total_bytes_in_response += bytes_in_current_query;
		total_bytes_read_from_response += bytes_read_from_current_query;

#ifdef _DEBUG
		printf_s("Read %lu bytes in this iteration.\n", bytes_in_current_query);
#endif // _DEBUG

		if (total_bytes_read_from_response >= (RESP_BUFF_SIZE - 128U)) {
			fprintf_s(stderr, "Warning: Truncation of response due to insufficient memory!\n");
			break;
		}

	// while there's data in the response to be read,
	} while (bytes_in_current_query > 0);


	// using the base CloseHandle() here will (did) crash the debug session.
	WinHttpCloseHandle(session_handle);
	WinHttpCloseHandle(connection_handle);
	WinHttpCloseHandle(request_handle);

#ifdef _DEBUG
	printf_s("%llu bytes have been received in total.\n", total_bytes_read_from_response);
#endif // _DEBUG

	(*response_size) = total_bytes_read_from_response;
	return buffer;
}


// return the offset of the buffer where the stable releases start.
range_t get_stable_releases_offset_range(_In_ const char* restrict html_body, _In_ const uint32_t size) {

	uint64_t start_offset = 0, end_offset = 0, stable_releases_start = 0;

	for (uint64_t i = 0; i < size; ++i) {

		// if the text matches the <h2> tag,
		if (html_body[i] == '<' && html_body[i + 1] == 'h' &&
			html_body[i + 2] == '2' && html_body[i + 3] == '>') {

			// <h2>Stable Releases</h2>
			if (start_offset == 0 && html_body[i + 4] == 'S' && html_body[i + 5] == 't' &&
				html_body[i + 6] == 'a' && html_body[i + 7] == 'b' && html_body[i + 8] == 'l' &&
				html_body[i + 9] == 'e') {
				// The HTML body contains only a single <h2> tag with an inner text that starts with "Stable"
				// so ignoring the " Releases</h2> part for cycle trimming.
				// If the start offset has already been found, do not waste time in this body in subsequent 
				// iterations -> short circuiting with the first conditional.
				start_offset = (i + 24);
			}


			// <h2>Pre-releases</h2>
			if (html_body[i + 4] == 'P' && html_body[i + 5] == 'r' && html_body[i + 6] == 'e' &&
				html_body[i + 7] == '-' && html_body[i + 8] == 'r' && html_body[i + 9] == 'e') {
				// The HTML body contains only a single <h2> tag with an inner text that starts with "Pre"
				// so ignoring the "leases</h2> part for cycle trimming.
				end_offset = (i - 1);
				// If found, break out of the loop.
				break;
			}
		}
	}

#ifdef _DEBUG
	printf_s("Start offset is %llu and stop offset id %llu. Stable releases string is %llu bytes long.\n",
		start_offset, end_offset, (end_offset - start_offset));
#endif // _DEBUG

	range_t delimiters = { .start = start_offset, .end = end_offset };
	return delimiters;
}



parsedstructs_t deserialize_stable_releases(_In_ const char* restrict stable_releases_chunk,
											  _In_ const uint64_t size) {

	// Caller is obliged to free the memory in return.py_start.

	// A struct to be returned by this function
	// Holds a pointer to the first python_t struct in the malloced buffer -> py_start
	// Number of structs in the allocated memory -> struct_count
	// Number of deserialized structs -> parsed_struct_count

	parsedstructs_t parse_results =  { .py_start = NULL, .struct_count = N_PYTHON_RELEASES,
									.parsed_struct_count = 0};

	// Allocate memory for N_PYTHON_RELEASES python_t structs.
	python_t* py_releases = (python_t*) malloc(sizeof(python_t) * N_PYTHON_RELEASES);

	// If malloc failed,
	if (!py_releases) {
		fprintf_s(stderr, "Error %lu. malloc returned NULL.\n",
			GetLastError());
		return parse_results;
	}

	// Zero out the malloced memory.
	memset(py_releases, 0, sizeof(python_t) * N_PYTHON_RELEASES);

	// A counter to remember last deserialized python_t struct.
	uint64_t last_deserialized_struct_offset = 0;

	// Start and end offsets of the version and url strings.
	uint64_t url_start_offset = 0, url_end_offset = 0, version_start_offset = 0, version_end_offset = 0;

	// Target template ->
	// <a href="https://www.python_t.org/ftp/python_t/3.10.11/python_t-3.10.11-amd64.exe">

	// A bool to keep identify whether the release in a amd64.exe format release.
	// needed since other release types like arm64, amd32, zip files have similarly formatted urls
	// that differ only at the end. 
	// Thus, this conditional is needed to skip over those releases
	bool is_amd64 = false;

	// (size - 100) to prevent reading past the buffer.
	for (uint64_t i = 0; i < (size - 100); ++i) {

		if (stable_releases_chunk[i] == '<' && stable_releases_chunk[i + 1] == 'a') {

			if (stable_releases_chunk[i + 2] == ' ' && stable_releases_chunk[i + 3] == 'h' && stable_releases_chunk[i + 4] == 'r' &&
				stable_releases_chunk[i + 5] == 'e' && stable_releases_chunk[i + 6] == 'f' && stable_releases_chunk[i + 7] == '=' &&
				stable_releases_chunk[i + 8] == '"' && stable_releases_chunk[i + 9] == 'h' && stable_releases_chunk[i + 10] == 't' &&
				stable_releases_chunk[i + 11] == 't' && stable_releases_chunk[i + 12] == 'p' && stable_releases_chunk[i + 13] == 's' &&
				stable_releases_chunk[i + 14] == ':' && stable_releases_chunk[i + 15] == '/' && stable_releases_chunk[i + 16] == '/' &&
				stable_releases_chunk[i + 17] == 'w' && stable_releases_chunk[i + 18] == 'w' && stable_releases_chunk[i + 19] == 'w' &&
				stable_releases_chunk[i + 20] == '.' && stable_releases_chunk[i + 21] == 'p' && stable_releases_chunk[i + 22] == 'y' &&
				stable_releases_chunk[i + 23] == 't' && stable_releases_chunk[i + 24] == 'h' && stable_releases_chunk[i + 25] == 'o' &&
				stable_releases_chunk[i + 26] == 'n' && stable_releases_chunk[i + 27] == '.' && stable_releases_chunk[i + 28] == 'o' &&
				stable_releases_chunk[i + 29] == 'r' && stable_releases_chunk[i + 30] == 'g' && stable_releases_chunk[i + 31] == '/' &&
				stable_releases_chunk[i + 32] == 'f' && stable_releases_chunk[i + 33] == 't' && stable_releases_chunk[i + 34] == 'p' &&
				stable_releases_chunk[i + 35] == '/' && stable_releases_chunk[i + 36] == 'p' && stable_releases_chunk[i + 37] == 'y' &&
				stable_releases_chunk[i + 38] == 't' && stable_releases_chunk[i + 39] == 'h' && stable_releases_chunk[i + 40] == 'o' &&
				stable_releases_chunk[i + 41] == 'n' && stable_releases_chunk[i + 42] == '/') {

				url_start_offset = i + 9;
				version_start_offset = i + 43;

				for (uint32_t j = 0; j < 50; ++j) {
					if (stable_releases_chunk[i + j + 43] == '/') {
						version_end_offset = i + j + 43;
						break;
					}
				}

				// The above equality checks will pass even for non <>amd64.exe releases :(
				// So, check the url's ending for <>amd64.exe
				for (uint32_t j = 0; j < 50; ++j) {
					if (stable_releases_chunk[i + j + 43] == 'a' && stable_releases_chunk[i + j + 44] == 'm' && stable_releases_chunk[i + j + 45] == 'd'
						&& stable_releases_chunk[i + j + 46] == '6' && stable_releases_chunk[i + j + 47] == '4' && stable_releases_chunk[i + j + 48] == '.'
						&& stable_releases_chunk[i + j + 49] == 'e' && stable_releases_chunk[i + j + 50] == 'x' && stable_releases_chunk[i + j + 51] == 'e') {
						url_end_offset = i + j + 52;
						// If every char checks out, set the flag true.
						is_amd64 = true;
						break;
					}
				}
			}

		// If the release is indeed an amd64.exe release,
		if(is_amd64) {

			/* Zeroed the whole malloced buffer, at line 283. So, this is unnecessary now. */
			// Zero the struct fields.
			// memset(py_releases[last_deserialized_struct_offset].version_string, 0, 40);
			// memset(py_releases[last_deserialized_struct_offset].amd64_download_url, 0, 150);

			// Copy the chars representing the release version to the deserialized struct's 
			// version_string field.
			memcpy_s((py_releases[last_deserialized_struct_offset]).version_string, 40U,
					(stable_releases_chunk + version_start_offset), (version_end_offset - version_start_offset));
#ifdef _DEBUG
			puts(py_releases[last_deserialized_struct_offset].version_string);
#endif // _DEBUG

			// Copy the chars representing the release url to the deserialized struct's 
			// amd64_download_url field.
			memcpy_s((py_releases[last_deserialized_struct_offset]).amd64_download_url, 150U,
				(stable_releases_chunk + url_start_offset), (url_end_offset - url_start_offset));

#ifdef _DEBUG
			puts(py_releases[last_deserialized_struct_offset].amd64_download_url);
#endif // _DEBUG

			// Increment the counter for last deserialized struct by one.
			last_deserialized_struct_offset++;

			// Increment the deserialized struct counter in parsedstructs_t by one.
			parse_results.parsed_struct_count++;

			// Reset the flag.
			is_amd64 = false;

			// Reset the offsets.
			url_start_offset = url_end_offset = version_start_offset = version_end_offset = 0;

			// If the release is not an amd64.exe,
			}else continue;
		}
	}

	parse_results.py_start = py_releases;

	return parse_results;
}



void print_python_releases(_In_ const parsedstructs_t parse_results, _In_ const char* restrict installed_python_version) {

	char python_version[BUFF_SIZE] = { 0 };
	uint32_t start_offset = 0;
	for (uint64_t i = 7; i < BUFF_SIZE; ++i) {
		// ASCII '0' to '9' is 48 to 57 and '.' is 46 ('/' is 47)
		// installed_python_version will be in the form of "Python 3.10.5"
		// Numeric version starts after offset 7. (@ 8)
		if ((installed_python_version[i] >= 46) && (installed_python_version[i] <= 57)) {
			python_version[i - 7] = installed_python_version[i];
		}
		// if any other characters encountered,
		else break;
	}


#ifdef _DEBUG
	printf_s("Installed python's version is %s", python_version);
#endif // _DEBUG

	puts("-----------------------------------------------------------------------------------");
	printf_s("|\x1b[36m%9s\x1b[m  |\x1b[36m%40s\x1b[m                             |\n",
		"Version", "Download URL");
	puts("-----------------------------------------------------------------------------------");
	for (uint64_t i = 0; i < parse_results.parsed_struct_count; ++i) {

#ifdef _DEBUG
		printf_s("strcmp: %d\n", strcmp(python_version, parse_results.py_start[i].version_string));
		printf_s("strlen: %zd, %zd\n", strlen(python_version), strlen(parse_results.py_start[i].version_string));
#endif // _DEBUG

		if (!strcmp(python_version, parse_results.py_start[i].version_string)) {
			printf_s("|\x1b[35;47;1m   %-7s |  %-66s \x1b[m|\n", parse_results.py_start[i].version_string,
				parse_results.py_start[i].amd64_download_url);
		}
		else {
			printf_s("|\x1b[91m   %-7s \x1b[m| \x1b[32m %-66s \x1b[m|\n", parse_results.py_start[i].version_string,
				parse_results.py_start[i].amd64_download_url);
		}
	}
	puts("-----------------------------------------------------------------------------------");
	return;
}