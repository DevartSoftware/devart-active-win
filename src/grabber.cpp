#include <iostream>
#include <Windows.h>
#include <map>
#include <iterator>
#include <array>
#include <string>
#include <vector>
#include <UIAutomation.h>
#include <napi.h>
#include <comdef.h>

#pragma comment(lib, "Version.lib")

IUIAutomation* automation;
IUIAutomationCondition* condition;

struct PROCESSINFO {
    std::wstring title;
    std::wstring processName;
    std::wstring processPath;
    std::wstring url;
    DWORD processId;
} processInfo = {};

class InitAutomation {
public:  
    InitAutomation() { };
    HRESULT Init() {
        HRESULT hr = CoInitialize(NULL);
        if SUCCEEDED(hr) {
            hr = CoCreateInstance(__uuidof(CUIAutomation), NULL,
                CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, __uuidof(IUIAutomation),
                (void**)&automation);
            if SUCCEEDED(hr) {
                VARIANT prop;
                prop.vt = VT_BOOL;
                prop.boolVal = VARIANT_TRUE;
                hr = automation->CreatePropertyCondition(UIA_IsControlElementPropertyId, prop, &condition);
            }
        }

        return hr;
    }

    ~InitAutomation() {
        if (condition) {
            condition->Release();
        }
        if (automation) {
            automation->Release();
        }
        CoUninitialize();
    };
} __initAutomation;

Napi::String getNapiString(Napi::Env env, const std::wstring& value) {
    if (!value.empty()) {
        std::u16string u16Str(value.begin(), value.end());
        return Napi::String::New(env, u16Str);
    }

    return Napi::String::New(env, "");
}

std::wstring getFileNameFromPath(const std::wstring& path) {
    return path.substr(path.find_last_of(L"/\\") + 1);
}

DWORD getProcessId(HWND hwnd) {
    DWORD processId;

    GetWindowThreadProcessId(hwnd, &processId);
    return processId;
}

std::wstring getProcessPath(HWND hwnd, DWORD processId) {
    HANDLE windowsHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!windowsHandle) {
        return L"";
    }

    WCHAR path[MAX_PATH];
    DWORD maxPath = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(windowsHandle, 0, path, &maxPath);
    CloseHandle(windowsHandle);
    if (!ok) {
        return L"";
    }

    return std::wstring(path, maxPath);
}

std::wstring getFileDescription(const std::wstring& path)
{
    DWORD verSize = GetFileVersionInfoSizeW(path.c_str(), NULL);

    if (verSize != NULL)
    {
        std::vector<BYTE> verData(verSize);

        if (GetFileVersionInfoW(path.c_str(), 0, verSize, verData.data()))
        {
            UINT cnt;
            struct LANGANDCODEPAGE {
                WORD wLanguage;
                WORD wCodePage;
            } *lang;

            if (VerQueryValueW(verData.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&lang, &cnt)) {
                UINT nCnt = cnt / sizeof(struct LANGANDCODEPAGE);
                UINT nczBufLn;
                LPWSTR  lpBuffer = NULL;

                for (UINT i = 0; i < nCnt; i++) {
                    WCHAR strQuery[MAX_PATH];
                    wsprintfW(strQuery, L"\\StringFileInfo\\%04x%04x\\FileDescription", lang[i].wLanguage, lang[i].wCodePage);
                    if (VerQueryValueW(verData.data(), strQuery, (VOID**)&lpBuffer, &nczBufLn))
                    {
                        if (lpBuffer && lpBuffer[0]) {
                            return lpBuffer; // lpBuffer points into verData, copy it before verData is freed
                        }
                    }
                }
            }
        }
    }

    return getFileNameFromPath(path);
}

std::wstring EnumerateChildren(IUIAutomationElement* parentElement, TreeScope scope) {
    IUIAutomationElementArray* elemArr = NULL;
    std::wstring res;

    HRESULT hr = parentElement->FindAll(scope, condition, &elemArr);

    if (SUCCEEDED(hr) && elemArr) {
        int length = 0;
        
        hr = elemArr->get_Length(&length);
        if SUCCEEDED(hr) {
            for (int i = 1; i < length; i++) {
                IUIAutomationElement* elem = NULL;
                
                hr = elemArr->GetElement(i, &elem);
                if (SUCCEEDED(hr) && elem) {
                    // TD-1755: Firefox keeps documents of background tabs in the UIA tree marked as offscreen
                    BOOL offscreen = FALSE;
                    if (SUCCEEDED(elem->get_CurrentIsOffscreen(&offscreen)) && offscreen) {
                        elem->Release();
                        continue;
                    }

                    CONTROLTYPEID contolType = 0;
                    
                    hr = elem->get_CurrentControlType(&contolType);
                    if (SUCCEEDED(hr) && (contolType == UIA_EditControlTypeId || contolType == UIA_GroupControlTypeId || contolType == UIA_DocumentControlTypeId)) {
                        IUnknown* pattern = NULL;

                        hr = elem->GetCurrentPattern(UIA_ValuePatternId, &pattern);
                        elem->Release();
                        if (SUCCEEDED(hr) && pattern) {
                            IUIAutomationValuePattern* valPattern = NULL;

                            hr = pattern->QueryInterface(IID_IUIAutomationValuePattern, (LPVOID*)&valPattern);
                            pattern->Release();
                            if (SUCCEEDED(hr) && valPattern != NULL) {
                                BSTR value;
                                hr = valPattern->get_CurrentValue(&value);
                                valPattern->Release();
                                if SUCCEEDED(hr) {
                                    if (value) {
                                        res = value;
                                    }
                                    SysFreeString(value);
                                    break;
                                }
                            }
                        }
                    }
                    else {
                        res = EnumerateChildren(elem, scope);
                        elem->Release();
                        if (!res.empty()) {
                            break;
                        }
                    }
                }
            }
        }
    }
    if (elemArr) {
        elemArr->Release();
    }

    return res;
}

BOOL CALLBACK enumChildWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD processId = getProcessId(hwnd);

    std::wstring appPath = getProcessPath(hwnd, processId);
    if (appPath.empty()) {
        return FALSE;
    }

    if (appPath != ((PROCESSINFO*)lParam)->processPath) {
        ((PROCESSINFO*)lParam)->processPath = appPath;
        ((PROCESSINFO*)lParam)->processName = getFileDescription(appPath);
        return FALSE;
    }

    return TRUE;
}

std::wstring getBrowserUrl(HWND hwnd, const std::wstring& processName) {
    IUIAutomationElement* elem = NULL;
    std::wstring url;

    std::array<std::wstring, 10>  arr = {
        L"Google Chrome",
        L"Microsoft Edge",
        L"Firefox",
        L"Opera Internet Browser",
        L"Vivaldi",
        L"Brave Browser",
        L"Ghost Browser",
        L"Wavebox",
        L"Sidekick",
        L"Orbita-Browser"
    };

    if (std::find(arr.begin(), arr.end(), processName) == arr.end()) {
        return url;
    }

    if (!automation || !condition) {
        if (condition) {
            condition->Release();
        }
        if (automation) {
            automation->Release();
        }

        CoUninitialize();

        return url;
    }

    HRESULT hr = automation->ElementFromHandle(hwnd, &elem);
    if (SUCCEEDED(hr) && elem)
    {
        url = EnumerateChildren(elem, (TreeScope)(TreeScope::TreeScope_Element | TreeScope::TreeScope_Children));
    }
    if (elem) {
        elem->Release();
    }

    return url;
}

Napi::Object makeResult(Napi::Env env, const PROCESSINFO& processInfo) {
    Napi::Object result = Napi::Object::New(env);
    result.Set(Napi::String::New(env, "title"), getNapiString(env, processInfo.title));
    result.Set(Napi::String::New(env, "platform"), Napi::String::New(env, "windows"));
    result.Set(Napi::String::New(env, "url"), getNapiString(env, processInfo.url));
    Napi::Object owner = Napi::Object::New(env);
    owner.Set(Napi::String::New(env, "processId"), Napi::Number::New(env, processInfo.processId));
    owner.Set(Napi::String::New(env, "path"), getNapiString(env, processInfo.processPath));
    owner.Set(Napi::String::New(env, "name"), getNapiString(env, processInfo.processName));
    result.Set(Napi::String::New(env, "owner"), owner);

    return result;
}

Napi::Value windows(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    HWND hwnd = GetForegroundWindow();
    if (hwnd == NULL) {
        return env.Null();
    }

    int windowTitleLength = GetWindowTextLengthW(hwnd);
    std::wstring windowTitle(windowTitleLength, L'\0');
    windowTitleLength = GetWindowTextW(hwnd, windowTitle.data(), windowTitleLength + 1);
    windowTitle.resize(windowTitleLength);

    DWORD processId = getProcessId(hwnd);
    if (processId <= 0) {
        Napi::TypeError::New(env, "Unable to get process ID").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (processId != processInfo.processId || windowTitle != processInfo.title) {
        processInfo.title = windowTitle;
        processInfo.processPath = getProcessPath(hwnd, processId);
        if (processInfo.processPath.empty()) {
            return env.Null();
        }
        processInfo.processName = getFileDescription(processInfo.processPath);
        processInfo.processId = processId;
        processInfo.url = getBrowserUrl(hwnd, processInfo.processName);

        if (getFileNameFromPath(processInfo.processPath) == L"ApplicationFrameHost.exe") {
            EnumChildWindows(hwnd, enumChildWindowsProc, (LPARAM)&processInfo);
        }

        if (processInfo.title.empty()) {
            processInfo.title = processInfo.processName;
        }
    }
    
    return makeResult(env, processInfo);
}


Napi::Object Init(Napi::Env env, Napi::Object exports) {
    __initAutomation.Init();
    exports.Set(Napi::String::New(env, "windows"), Napi::Function::New(env, windows));
    return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, Init)