#pragma once

#include <windows.h>
#include <string>

#include "resource.h"

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ShowContextMenu(HWND hwnd);
void ShowAppMenu(HWND hwnd);
void PopulateMenuFromFolder(HMENU hMenu, const std::wstring& folder, bool recurse);
void ExecuteShortcut(const std::wstring& path);
void LaunchItem(const std::wstring& path);
bool PickFolderDialog(HWND hwndOwner, std::wstring& outPath);
bool PickFolderDialog(HWND hwndOwner, std::wstring& outPath);
