#include "pch.h"
#include "WinSysTrayIcon.h"
#include "resource.h"
#include <map>
#include <string>
#include <filesystem>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shellapi.h>

namespace fs = std::filesystem;

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

// Define a simple structure to hold our menu entry items
struct MenuEntryItem {
	std::wstring name;
	std::wstring fullPath;
};

std::map<int, std::wstring> menuCommandMap;
int currentMenuID = ID_TRAY_BASE;
HINSTANCE hInst;
std::wstring rootShortcutDir;

HICON GetSmallIcon(const std::wstring& filePath);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	hInst = hInstance;

	// Read ini file
	wchar_t iniFileName[MAX_PATH];
	if (GetModuleFileName(nullptr, iniFileName, MAX_PATH))
	{
		fs::path iniFile = fs::path{ iniFileName };
		iniFile.replace_extension("ini");
		wchar_t readBuffer[MAX_PATH];
		*readBuffer = 0;
		GetPrivateProfileString(L"startup", L"Folder", L"", readBuffer, MAX_PATH, iniFile.c_str());
		if (*readBuffer)
			rootShortcutDir.assign(readBuffer);
	}

	auto hr = CoInitialize(NULL);

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"ShortcutTrayAppClass";
	RegisterClass(&wc);

	HWND hwnd = CreateWindow(wc.lpszClassName, L"TrayApp", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
		NULL, NULL, hInstance, NULL);

	// Add tray icon
	NOTIFYICONDATA nid = {};
	nid.cbSize = sizeof(nid);
	nid.hWnd = hwnd;
	nid.uID = 1;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wcscpy_s(nid.szTip, L"Shortcut Tray");

	DWORD rc = 0;
	wchar_t buffer[MAX_PATH];
	rc = GetModuleFileName(nullptr, buffer, MAX_PATH);
	if (rc)
	{
		nid.hIcon = GetSmallIcon(buffer);
	}

	Shell_NotifyIcon(NIM_ADD, &nid);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Shell_NotifyIcon(NIM_DELETE, &nid);
	CoUninitialize();
	return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_TRAYICON) {
		if (LOWORD(lParam) == WM_LBUTTONUP) {
			ShowAppMenu(hwnd);
		}
		else if (LOWORD(lParam) == WM_RBUTTONUP) {
			ShowContextMenu(hwnd);
		}
	}
	else if (msg == WM_COMMAND) {
		if (LOWORD(wParam) == ID_SETTINGS_EXIT) {
			PostQuitMessage(0);
		}
		else if (LOWORD(wParam) == ID_SETTINGS_FOLDER) {
			std::wstring selectedFolder;
			if (PickFolderDialog(hwnd, selectedFolder)) {
				// Update the active system runtime string allocation cache
				rootShortcutDir = selectedFolder;

				// Resolve the path to the relative target configuration file location
				wchar_t iniFileName[MAX_PATH];
				if (GetModuleFileName(nullptr, iniFileName, MAX_PATH)) {
					fs::path iniFile = fs::path{ iniFileName };
					iniFile.replace_extension("ini");

					// Flush data updates permanently into storage
					WritePrivateProfileStringW(L"startup", L"Folder", rootShortcutDir.c_str(), iniFile.c_str());
				}
			}
		}
		else {
			int id = LOWORD(wParam);
			if (menuCommandMap.count(id)) {
				LaunchItem(menuCommandMap[id]);
			}
		}
	}
	else if (msg == WM_DESTROY) {
		PostQuitMessage(0);
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowAppMenu(HWND hwnd) {
	POINT pt;
	GetCursorPos(&pt);

	HMENU hMenu = CreatePopupMenu();
	currentMenuID = ID_TRAY_BASE;
	menuCommandMap.clear();

	PopulateMenuFromFolder(hMenu, rootShortcutDir, true);

	SetForegroundWindow(hwnd);
	TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(hMenu);
}

void ShowContextMenu(HWND hwnd) {
	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(hwnd);

	HMENU hMenu = CreatePopupMenu();

	// Format text dynamically to explicitly display which target folder is active
	std::wstring menuText = L"Folder: " + (rootShortcutDir.empty() ? L"[Not Set]" : rootShortcutDir);

	// Append the dynamic settings toggle item followed by your standard exit choice
	AppendMenuW(hMenu, MF_STRING, ID_SETTINGS_FOLDER, menuText.c_str());
	AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(hMenu, MF_STRING, ID_SETTINGS_EXIT, L"Exit");

	TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
	DestroyMenu(hMenu);
}

void PopulateMenuFromFolder(HMENU hMenu, const std::wstring& folder, bool recurse) {
	WIN32_FIND_DATA findData;
	std::wstring searchPath = folder + L"\\*";
	HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);

	if (hFind == INVALID_HANDLE_VALUE)
		return;

	// Two lists to separate directories from executables/shortcuts
	std::vector<MenuEntryItem> folderList;
	std::vector<MenuEntryItem> fileList;

	// 1. Separate entries based on file attributes
	do {
		std::wstring name = findData.cFileName;
		if (name == L"." || name == L"..")
			continue;

		std::wstring fullPath = folder + L"\\" + name;

		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (recurse) {
				folderList.push_back({ name, fullPath });
			}
		}
		else if (PathMatchSpec(name.c_str(), L"*.lnk") || PathMatchSpec(name.c_str(), L"*.exe")) {
			fileList.push_back({ name, fullPath });
		}
	} while (FindNextFile(hFind, &findData));

	FindClose(hFind);

	// 2. Add Folders to the menu first (Maintains natural NTFS sort order)
	for (const auto& dir : folderList) {
		HMENU subMenu = CreatePopupMenu();
		PopulateMenuFromFolder(subMenu, dir.fullPath, recurse);
		InsertMenu(hMenu, -1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)subMenu, dir.name.c_str());
	}

	// 3. Add Files, Hard links, and Symlinks next
	for (const auto& file : fileList) {
		HICON hIcon = GetSmallIcon(file.fullPath);
		MENUITEMINFO mii = { sizeof(MENUITEMINFO) };
		mii.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_BITMAP;
		mii.fType = MFT_STRING;
		mii.wID = currentMenuID;

		// Remove extension cleanly (.lnk or .exe)
		std::wstring base_name = file.name;
		auto menuitem = base_name.substr(0, base_name.length() - 4);
		mii.dwTypeData = (LPWSTR)menuitem.c_str();

		HBITMAP hBmp = NULL;
		if (hIcon) {
			ICONINFO iconInfo;
			GetIconInfo(hIcon, &iconInfo);
			hBmp = iconInfo.hbmColor;
			mii.hbmpItem = hBmp;
		}

		InsertMenuItem(hMenu, -1, TRUE, &mii);
		menuCommandMap[currentMenuID++] = file.fullPath;
	}
}

HICON GetSmallIcon(const std::wstring& filePath) {
	SHFILEINFO sfi{ 0 };
	SHGetFileInfo(filePath.c_str(), 0, &sfi, sizeof(sfi),
		SHGFI_ICON | SHGFI_SMALLICON);
	return sfi.hIcon;
}

// UPDATED: Completely handles .lnk, native .exe, hard links, and symlinks seamlessly
void LaunchItem(const std::wstring& path) {
	fs::path itemPath(path);

	// Case 1: Standard Windows Shortcut (.lnk)
	if (itemPath.extension() == L".lnk") {
		IShellLink* psl = NULL;
		HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			IID_IShellLink, (LPVOID*)&psl);
		if (SUCCEEDED(hr)) {
			IPersistFile* ppf = NULL;
			hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
			if (SUCCEEDED(hr)) {
				hr = ppf->Load(path.c_str(), STGM_READ);
				if (SUCCEEDED(hr)) {
					psl->Resolve(NULL, SLR_NO_UI);
					WIN32_FIND_DATA wfd{ 0 };
					WCHAR szTarget[MAX_PATH];
					psl->GetPath(szTarget, MAX_PATH, &wfd, SLGP_UNCPRIORITY);
					ShellExecute(NULL, L"open", szTarget, NULL, NULL, SW_SHOWNORMAL);
				}
				ppf->Release();
			}
			psl->Release();
		}
	}
	// Case 2: Executable, Hard link, or Symbolic Link (.exe)
	else if (itemPath.extension() == L".exe") {
		fs::path pathToLaunch = itemPath;
		fs::path workingDirectory = itemPath.parent_path();

		// Check if the filesystem entry is specifically a Symbolic Link
		if (fs::is_symlink(itemPath)) {
			fs::path trueTarget = fs::read_symlink(itemPath);
			// Resolve relative symlink offsets to full absolute structures
			if (trueTarget.is_relative()) {
				trueTarget = itemPath.parent_path() / trueTarget;
			}
			pathToLaunch = trueTarget;
			workingDirectory = trueTarget.parent_path(); // Crucial: Fixes target app companion dependency queries!
		}

		// Launch via ShellExecuteW using explicit target environment parameters
		ShellExecuteW(
			NULL,
			L"open",
			pathToLaunch.c_str(),
			NULL,
			workingDirectory.c_str(), // Directing CWD cleanly ensures hardlinks & symlinks resolve assets accurately
			SW_SHOWNORMAL
		);
	}
}

bool PickFolderDialog(HWND hwndOwner, std::wstring& outPath) {
	IFileOpenDialog* pFileOpen = NULL;

	// Create the FileOpenDialog instance object interface
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
		IID_IFileOpenDialog, (LPVOID*)&pFileOpen);

	if (SUCCEEDED(hr)) {
		// Set options to restrict selection strictly to folder targets
		DWORD dwOptions;
		pFileOpen->GetOptions(&dwOptions);
		pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);

		// NEW: Try to set the initial starting location to the current INI directory
		if (!rootShortcutDir.empty() && PathFileExistsW(rootShortcutDir.c_str())) {
			IShellItem* pInitialFolderItem = NULL;
			hr = SHCreateItemFromParsingName(rootShortcutDir.c_str(), NULL, IID_PPV_ARGS(&pInitialFolderItem));
			if (SUCCEEDED(hr)) {
				// Set both the default folder and current folder parameters
				pFileOpen->SetFolder(pInitialFolderItem);
				pInitialFolderItem->Release();
			}
		}

		// Present the window viewport to the user
		hr = pFileOpen->Show(hwndOwner);
		if (SUCCEEDED(hr)) {
			IShellItem* pItem = NULL;
			hr = pFileOpen->GetResult(&pItem);
			if (SUCCEEDED(hr)) {
				wchar_t* pszFolderPath = NULL;
				hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
				if (SUCCEEDED(hr)) {
					outPath = pszFolderPath;
					CoTaskMemFree(pszFolderPath); // Release COM baseline string allocation
				}
				pItem->Release();
			}
		}
		pFileOpen->Release();
	}
	return SUCCEEDED(hr);
}
