// Copyright (c) 2010 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cefclient.h"
#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#include <MMSystem.h>
#include <sstream>
#include <string>
#include "include/cef_app.h"
#include "include/cef_version.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_runnable.h"
#include "client_handler.h"
#include "config.h"
#include "resource.h"
#include "string_util.h"
#include "client_switches.h"
#include "native_menu_model.h"
#include "appshell_node_process.h"

#include <algorithm>
#include <ShellAPI.h>
#include <ShlObj.h>

#include "cef_registry.h"
#include "cef_main_window.h"

//si try to read json..
//#include "Jzon.h"
#include <iostream>
#include <fstream>
#include "JSON.h"

// Global Variables:
DWORD            g_appStartupTime;
HINSTANCE        hInst;                     // current instance
HACCEL           hAccelTable;
std::wstring     gFilesToOpen;              // Filenames passed as arguments to app
cef_main_window* gMainWnd = NULL;
//si
JSONObject configRoot;

// static variables (not exported)
static char      szWorkingDir[MAX_PATH];    // The current working directory
static wchar_t   szInitialUrl[MAX_PATH] = {0};


// Forward declarations of functions included in this code module:
BOOL InitInstance(HINSTANCE, int);

// The global ClientHandler reference.
extern CefRefPtr<ClientHandler> g_handler;

#if defined(OS_WIN)
// Add Common Controls to the application manifest because it's required to
// support the default tooltip implementation.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")  // NOLINT(whitespace/line_length)
#endif

// If 'str' ends with a colon followed by some digits, then remove the colon and digits. For example:
//    "c:\bob\abc.txt:123:456" will be changed to "c:\bob\abc.txt:123"
//    "c:\bob\abc.txt:123" will be changed to "c:\bob\abc.txt"
//    "c:\bob\abc.txt" will not be changed
// (Note: we could do this with a regular expression, but there is no regex library currently
// built into brackets-shell, and I don't want to add one just for this simple case).
void StripColonNumber(std::wstring& str) {
    bool gotDigits = false;
    int index;
    for (index = str.size() - 1; index >= 0; index--) {
        if (!isdigit(str[index]))
            break;
        gotDigits = true;
    }
    if (gotDigits && index >= 0 && str[index] == ':') {
        str.resize(index);
    }
}

// Determine if 'str' is a valid filename.
bool IsFilename(const std::wstring& str) {
    // Strip off trailing line and column number, if any.
    std::wstring temp(str);
    StripColonNumber(temp);
    StripColonNumber(temp);

	// Return true if the OS thinks the filename is OK.
    return (GetFileAttributes(temp.c_str()) != INVALID_FILE_ATTRIBUTES);
}

std::wstring GetFilenamesFromCommandLine() {
    std::wstring result = L"[]";

    if (AppGetCommandLine()->HasArguments()) {
        bool firstEntry = true;
        std::vector<CefString> args;
        AppGetCommandLine()->GetArguments(args);
        std::vector<CefString>::iterator iterator;

        result = L"[";
        for (iterator = args.begin(); iterator != args.end(); iterator++) {
            std::wstring argument = (*iterator).ToWString();
            if (IsFilename(argument)) {
                if (!firstEntry) {
                    result += L",";
                }
                firstEntry = false;
                result += L"\"" + argument + L"\"";
            }
        }
        result += L"]";
    }

    return result;
}

// forward declaration; implemented in appshell_extensions_win.cpp
void ConvertToUnixPath(ExtensionString& filename);

// Program entry point function.
int APIENTRY wWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  g_appStartupTime = timeGetTime();

  CefMainArgs main_args(hInstance);
  CefRefPtr<ClientApp> app(new ClientApp);

  // Execute the secondary process, if any.
  int exit_code = CefExecuteProcess(main_args, app.get());
  if (exit_code >= 0)
    return exit_code;

  // Retrieve the current working directory.
  if (_getcwd(szWorkingDir, MAX_PATH) == NULL)
    szWorkingDir[0] = 0;

  // Parse command line arguments. The passed in values are ignored on Windows.
  AppInitCommandLine(0, NULL);

  HANDLE hMutex = NULL;
  /*
  // Determine if we should use an already running instance of Brackets.
  HANDLE hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, FIRST_INSTANCE_MUTEX_NAME);
  if ((hMutex != NULL) && AppGetCommandLine()->HasArguments() && (lpCmdLine != NULL)) {
	  // for subsequent instances, re-use an already running instance if we're being called to
	  //   open an existing file on the command-line (eg. Open With.. from Windows Explorer)
	  HWND hFirstInstanceWnd = cef_main_window::FindFirstTopLevelInstance();
	  if (hFirstInstanceWnd != NULL) {
		  ::SetForegroundWindow(hFirstInstanceWnd);
		  if (::IsIconic(hFirstInstanceWnd))
			  ::ShowWindow(hFirstInstanceWnd, SW_RESTORE);
		  
		  // message the other Brackets instance to actually open the given filename
		  std::wstring wstrFilename = lpCmdLine;
		  ConvertToUnixPath(wstrFilename);
		  // note: WM_COPYDATA will manage passing the string across process space
		  COPYDATASTRUCT data;
		  data.dwData = ID_WM_COPYDATA_SENDOPENFILECOMMAND;
		  data.cbData = (wstrFilename.length() + 1) * sizeof(WCHAR);
		  data.lpData = (LPVOID)wstrFilename.c_str();
		  ::SendMessage(hFirstInstanceWnd, WM_COPYDATA, (WPARAM)(HWND)hFirstInstanceWnd, (LPARAM)(LPVOID)&data);

		  // exit this instance
		  return 0;
	  }
	  // otherwise, fall thru and launch a new instance
  }
  */
  if (hMutex == NULL) {
	  // first instance of this app, so create the mutex and continue execution of this instance.
	  hMutex = ::CreateMutex(NULL, FALSE, FIRST_INSTANCE_MUTEX_NAME);
  }

  CefSettings settings;

  // Populate the settings based on command line arguments.
  AppGetSettings(settings, app);
  CefRefPtr<CefCommandLine> cmdLine = AppGetCommandLine();
  
  
	//si: read some json.
	wchar_t appPath2[MAX_PATH];

	wchar_t jsonPath[MAX_PATH];
	wchar_t *jsonPathRoot;
	if (cmdLine->HasSwitch(cefclient::kJSONConfig)) {
	  wcscpy(appPath2, cmdLine->GetSwitchValue(cefclient::kJSONConfig).c_str());
	  wcscpy(jsonPath, cmdLine->GetSwitchValue(cefclient::kJSONConfig).c_str());
	  jsonPathRoot = wcsrchr(jsonPath, '\\');
	  wcscpy(jsonPathRoot, L"\\");
	} else {
		// get exe filename, strip the .exe filename (and preceding "\") from the appPath
		// and store in pathRoot2
		wchar_t *pathRoot2;
		GetModuleFileName(NULL, appPath2, MAX_PATH);
		pathRoot2 = wcsrchr(appPath2, '.');
		wcscpy(pathRoot2, L".json");
		char appPathJsonString[MAX_PATH];
		wcstombs(appPathJsonString,appPath2,MAX_PATH);
		//wcstombs(appPathJsonString,jsonPath,MAX_PATH);

		GetModuleFileName(NULL, jsonPath, MAX_PATH);
		jsonPathRoot = wcsrchr(jsonPath, '\\');
		wcscpy(jsonPathRoot, L"\\");
	}
	//wchar_t *jsonPathRoot = wcsrchr(jsonPath, '\\');
	bool configOk = false;
	JSONValue *config = NULL;
	if (GetFileAttributes(appPath2) != INVALID_FILE_ATTRIBUTES) {
			
		std::string s;
		{
			std::ifstream file (appPath2,std::ios::binary);
			if (file) {
					std::ostringstream os;
					os << file.rdbuf();
					s = os.str();
			}
		}

		config = JSON::Parse(s.c_str());
	} 
	// Did it go wrong?
	
	if (config != NULL)
	{
		// Retrieve the main object
		if (config->IsObject() == true)
		{
			configRoot = config->AsObject();
			configOk = true;
			
			if (configRoot.find(L"appDir") != configRoot.end() && configRoot[L"appDir"]->IsString()) {	
				//appDir is abs location that all json paths are relative to.
				//normally you don't set it and it automatically has the directory the json file is located in.
			} else {
				configRoot[L"appDir"] = new JSONValue(jsonPath);
			}
		}
	} 
	if(!configOk) {
		OutputDebugString(L"Error reading json config!\r\n");
		const wchar_t* DEFAULT = L"{\"windowFrame\":true}"; 
		
		config = JSON::Parse( DEFAULT );
		configRoot = config->AsObject();
	}

  // Check command
  if (CefString(&settings.cache_path).length() == 0) {
	  CefString(&settings.cache_path) = AppGetCachePath();
  }

  //set settings from the JSON.
  if (configRoot.find(L"kCachePath") != configRoot.end() && configRoot[L"kCachePath"]->IsString()) {	
		wchar_t relPath2[MAX_PATH];
		wchar_t *relPath2Ptr;
		wcscpy(relPath2, jsonPath);
		relPath2Ptr = wcsncat(relPath2,configRoot[L"kCachePath"]->AsString().c_str(),MAX_PATH);
		if (GetFileAttributes(relPath2) != INVALID_FILE_ATTRIBUTES) {
			CefString(&settings.cache_path) = relPath2;
		}
  }
  if (configRoot.find(L"kUserAgent") != configRoot.end() && configRoot[L"kUserAgent"]->IsString()) {	
	  CefString(&settings.user_agent) = configRoot[L"kUserAgent"]->AsString().c_str();
  }
  if (configRoot.find(L"kDisableStandardCommandArgs") != configRoot.end() && configRoot[L"kDisableStandardCommandArgs"]->IsBool()) {	
	  settings.command_line_args_disabled = configRoot[L"kDisableStandardCommandArgs"]->AsBool();
  }
  if (configRoot.find(L"kIgnoreCertificateErrors") != configRoot.end() && configRoot[L"kIgnoreCertificateErrors"]->IsBool()) {	
	  settings.ignore_certificate_errors = configRoot[L"kIgnoreCertificateErrors"]->AsBool();
  }
  if (configRoot.find(L"kjsFlags") != configRoot.end() && configRoot[L"kjsFlags"]->IsString()) {	
	  CefString(&settings.javascript_flags) = configRoot[L"kjsFlags"]->AsString().c_str();
  }
   if (configRoot.find(L"klogFile") != configRoot.end() && configRoot[L"klogFile"]->IsString()) {	
	  //CefString(&settings.log_file) = configRoot[L"klogFile"]->AsString().c_str();
	  wchar_t relPath3[MAX_PATH];
		wchar_t *relPath3Ptr;
		wcscpy(relPath3, jsonPath);
		relPath3Ptr = wcsncat(relPath3,configRoot[L"klogFile"]->AsString().c_str(),MAX_PATH);
		CefString(&settings.log_file) = relPath3;
		
  }
  //if (configRoot.find(L"klogSeverity") != configRoot.end() && configRoot[L"klogSeverity"]->IsNumber()) {	
//	  settings.log_severity = (int)configRoot[L"klogSeverity"]->AsNumber();
//  }
  if (configRoot.find(L"kMultiThreadedMessageLoop") != configRoot.end() && configRoot[L"kMultiThreadedMessageLoop"]->IsBool()) {	
	settings.multi_threaded_message_loop = configRoot[L"kMultiThreadedMessageLoop"]->AsBool();
  }
  if (configRoot.find(L"kPersistSessionCookies") != configRoot.end() && configRoot[L"kPersistSessionCookies"]->IsBool()) {	
	  settings.persist_session_cookies = configRoot[L"kPersistSessionCookies"]->AsBool();
  }
  if (configRoot.find(L"kProductVersion") != configRoot.end() && configRoot[L"kProductVersion"]->IsString()) {	
	  CefString(&settings.product_version) = configRoot[L"kProductVersion"]->AsString().c_str();
  }
  if (configRoot.find(L"kRemoteDebugPort") != configRoot.end() && configRoot[L"kRemoteDebugPort"]->IsNumber()) {	
	  settings.remote_debugging_port = (int)configRoot[L"kRemoteDebugPort"]->AsNumber();
  }
  if (configRoot.find(L"kSingleProcess") != configRoot.end() && configRoot[L"kSingleProcess"]->IsBool()) {	
	  settings.single_process = configRoot[L"kSingleProcess"]->AsBool();
  }
 
  // Initialize CEF.
  CefInitialize(main_args, settings, app.get());

  if (cmdLine->HasSwitch(cefclient::kStartupPath)) {
	  wcscpy(szInitialUrl, cmdLine->GetSwitchValue(cefclient::kStartupPath).c_str());
  }  else {
	// If the shift key is not pressed, look for the index.html file 
	if (GetAsyncKeyState(VK_SHIFT) == 0) {
	// Get the full pathname for the app. We look for the index.html
	// file relative to this location.
	wchar_t appPath[MAX_PATH];
	wchar_t *pathRoot;
	GetModuleFileName(NULL, appPath, MAX_PATH);

	//try to boot off an app relative to the JSON file we are reading.
	if (configRoot.find(L"appLocation") != configRoot.end() && configRoot[L"appLocation"]->IsString()) {	
		wchar_t relPath1[MAX_PATH];
		wchar_t *relPath1Ptr;
		wcscpy(relPath1, jsonPath);
		relPath1Ptr = wcsncat(relPath1,configRoot[L"appLocation"]->AsString().c_str(),MAX_PATH);
		if (GetFileAttributes(relPath1) != INVALID_FILE_ATTRIBUTES) {
			wcscpy(szInitialUrl, relPath1);
		}
	} else {
		//try to boot off an app relative to this binary.
		// Strip the .exe filename (and preceding "\") from the appPath
		pathRoot = wcsrchr(appPath, '\\');
		wcscpy(pathRoot, L"\\..\\..\\deskshell-brackets-app\\index.htm");
		// If the file exists, use it
		if (GetFileAttributes(appPath) != INVALID_FILE_ATTRIBUTES) {
			wcscpy(szInitialUrl, appPath);
		}
	}
	
	

	if (!wcslen(szInitialUrl)) {
		// Look for .\www\index.html next
		wcscpy(pathRoot, L"\\www\\index.html");
		if (GetFileAttributes(appPath) != INVALID_FILE_ATTRIBUTES) {
			wcscpy(szInitialUrl, appPath);
		}
	}
	}
  }

  if (!wcslen(szInitialUrl)) {
      // If we got here, either the startup file couldn't be found, or the user pressed the
      // shift key while launching. Prompt to select the index.html file.
      OPENFILENAME ofn = {0};
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFile = szInitialUrl;
      ofn.nMaxFile = MAX_PATH;
      ofn.lpstrFilter = L"Web Files\0*.htm;*.html\0\0";
      ofn.lpstrTitle = L"Please select the " APP_NAME L" index.html file.";
      ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

      if (!GetOpenFileName(&ofn)) {
        // User cancelled, exit the app
        CefShutdown();
        return 0;
      }
  }

  // Perform application initialization
  if (!InitInstance (hInstance, nCmdShow))
    return FALSE;

  // Start the node server process
  startNodeProcess();

  gFilesToOpen = GetFilenamesFromCommandLine();

  int result = 0;

  if (!settings.multi_threaded_message_loop) {
    // Run the CEF message loop. This function will block until the application
    // recieves a WM_QUIT message.
    CefRunMessageLoop();
  } else {
    MSG msg;

    // Run the application message loop.
    while (GetMessage(&msg, NULL, 0, 0)) {
      if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }

    result = static_cast<int>(msg.wParam);
  }

  OnBeforeShutdown();

  // Shut down CEF.
  CefShutdown();

  // release the first instance mutex
  if (hMutex != NULL)
	  ReleaseMutex(hMutex);

  return result;
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
  hInst = hInstance;  // Store instance handle in our global variable
  gMainWnd = new cef_main_window();
  return gMainWnd->Create();
}

// Global functions

std::string AppGetWorkingDirectory() {
  return szWorkingDir;
}

CefString AppGetCachePath() {
  std::wstring cachePath = ClientApp::AppGetSupportDirectory();
  cachePath +=  L"/cef_data";

  return CefString(cachePath);
}

CefString AppGetInitialURL() {
    return szInitialUrl;    
}

// Helper function for AppGetProductVersionString. Reads version info from
// VERSIONINFO and writes it into the passed in std::wstring.
void GetFileVersionString(std::wstring &retVersion) {
  DWORD dwSize = 0;
  BYTE *pVersionInfo = NULL;
  VS_FIXEDFILEINFO *pFileInfo = NULL;
  UINT pLenFileInfo = 0;

  HMODULE module = GetModuleHandle(NULL);
  TCHAR executablePath[MAX_PATH];
  GetModuleFileName(module, executablePath, MAX_PATH);

  dwSize = GetFileVersionInfoSize(executablePath, NULL);
  if (dwSize == 0) {
    return;
  }

  pVersionInfo = new BYTE[dwSize];

  if (!GetFileVersionInfo(executablePath, 0, dwSize, pVersionInfo)) 	{
    delete[] pVersionInfo;
    return;
  }

  if (!VerQueryValue(pVersionInfo, TEXT("\\"), (LPVOID*) &pFileInfo, &pLenFileInfo)) {
    delete[] pVersionInfo;
    return;
  }

  int major  = (pFileInfo->dwFileVersionMS >> 16) & 0xffff ;
  int minor  = (pFileInfo->dwFileVersionMS) & 0xffff;
  int hotfix = (pFileInfo->dwFileVersionLS >> 16) & 0xffff;
  int other  = (pFileInfo->dwFileVersionLS) & 0xffff;

  delete[] pVersionInfo;

  std::wostringstream versionStream(L"");
  versionStream << major << L"." << minor << L"." << hotfix << L"." << other; 
  retVersion = versionStream.str();
}

CefString AppGetProductVersionString() {
  std::wstring s(APP_NAME);
  size_t i = s.find(L" ");
  while (i != std::wstring::npos) {
    s.erase(i, 1);
    i = s.find(L" ");
  }
  std::wstring version(L"");
  GetFileVersionString(version);
  s.append(L"/");
  s.append(version);
  return CefString(s);
}

CefString AppGetChromiumVersionString() {
  std::wostringstream versionStream(L"");
  versionStream << L"Chrome/" << cef_version_info(2) << L"." << cef_version_info(3)
                << L"." << cef_version_info(4) << L"." << cef_version_info(5);

  return CefString(versionStream.str());
}
