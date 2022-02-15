#include <iostream>
#include <Windows.h>
#include <map>
#include <iterator>
#include <array>
#include <UIAutomation.h>
#include <napi.h>
#include <comdef.h>

#pragma comment(lib, "Version.lib")

IUIAutomation* automation;
IUIAutomationCondition* condition;

struct PROCESSINFO {
    LPWSTR title;
    LPWSTR processName;
    LPWSTR processPath;
    LPWSTR url;
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

Napi::String getNapiStringFromLPWSTR(Napi::Env env, LPWSTR value) {
    if (value && value[0]) {
        std::wstring wStr= value;
        std::u16string u16Str(wStr.begin(), wStr.end());
        return Napi::String::New(env, u16Str);
    }

    return Napi::String::New(env, "");
}

LPWSTR getFileNameFromPath(LPWSTR path) {
    std::wstring appPath = path;
    std::wstring fileName = appPath.substr(appPath.find_last_of(L"/\\") + 1).c_str();
    LPWSTR res = new WCHAR[fileName.size() + 2];
    copy(fileName.begin(), fileName.end(), res);
    res[fileName.size()] = 0;
    return res;
}

DWORD getProcessId(HWND hwnd) {
    DWORD processId;

    GetWindowThreadProcessId(hwnd, &processId);
    return processId;
}

LPWSTR getProcessPath(HWND hwnd, DWORD processId) {
    HANDLE windowsHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!windowsHandle) {
        return NULL;
    }

    DWORD maxPath = MAX_PATH;
    LPWSTR path = new WCHAR[MAX_PATH];
    QueryFullProcessImageNameW(windowsHandle, 0, path, &maxPath);
    CloseHandle(windowsHandle);
    if (!path || !path[0]) {
        return NULL;
    }

    return path;
}

LPWSTR getFileDescription(LPWSTR path)
{
    DWORD verSize = GetFileVersionInfoSizeW(path, NULL);

    if (verSize != NULL)
    {
        LPWSTR verData = new WCHAR[verSize];

        if (GetFileVersionInfoW(path, 0, verSize, verData))
        {
            UINT cnt;
            struct LANGANDCODEPAGE {
                WORD wLanguage;
                WORD wCodePage;
            } *lang;

            if (VerQueryValueW(verData, L"\\VarFileInfo\\Translation", (LPVOID*)&lang, &cnt)) {
                UINT nCnt = cnt / sizeof(struct LANGANDCODEPAGE);
                UINT nczBufLn;
                LPWSTR  lpBuffer = NULL;

                for (UINT i = 0; i < nCnt; i++) {
                    LPWSTR strQuery = new WCHAR[MAX_PATH];
                    wsprintfW(strQuery, L"\\StringFileInfo\\%04x%04x\\FileDescription", lang[i].wLanguage, lang[i].wCodePage);
                    if (VerQueryValueW(verData, strQuery, (VOID**)&lpBuffer, &nczBufLn))
                    {
                        if (lpBuffer && lpBuffer[0]) {
                            return lpBuffer;
                        }
                    }
                }
            }
        }

        delete[] verData;
    }

    return getFileNameFromPath(path);
}

LPWSTR EnumerateChildren(IUIAutomationElement* parentElement, TreeScope scope) {
    IUIAutomationElementArray* elemArr = NULL;
    LPWSTR res = L"";

    HRESULT hr = parentElement->FindAll(scope, condition, &elemArr);

    if (SUCCEEDED(hr) && elemArr) {
        int length = 0;
        
        hr = elemArr->get_Length(&length);
        if SUCCEEDED(hr) {
            for (int i = 1; i < length; i++) {
                IUIAutomationElement* elem = NULL;
                
                hr = elemArr->GetElement(i, &elem);
                if (SUCCEEDED(hr) && elem) {
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
                                    res = (WCHAR*)_bstr_t(value);
                                    SysFreeString(value);
                                    break;
                                }
                            }
                        }
                    }
                    else {
                        res = EnumerateChildren(elem, scope);
                        if (res && res[0]) {
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

    LPWSTR appPath = getProcessPath(hwnd, processId);
    if (!appPath || !appPath[0]) {
        return FALSE;
    }

    if (wcscmp(appPath, ((PROCESSINFO*)lParam)->processPath) != 0) {
        ((PROCESSINFO*)lParam)->processPath = appPath;
        ((PROCESSINFO*)lParam)->processName = getFileDescription(appPath);
        return FALSE;
    }

    return TRUE;
}

LPWSTR getBrowserUrl(HWND hwnd, LPWSTR processName) {
    IUIAutomationElement* elem = NULL;
    LPWSTR url = L"";

    std::wstring wStr = processName;
    std::array<std::wstring, 9>  arr = {
        L"Google Chrome",
        L"Microsoft Edge",
        L"Firefox",
        L"Opera Internet Browser",
        L"Vivaldi",
        L"Brave Browser",
        L"Ghost Browser",
        L"Wavebox",
        L"Sidekick"
    };

    if (std::find(arr.begin(), arr.end(), wStr) == arr.end()) {
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

Napi::Object makeResult(Napi::Env env, PROCESSINFO processInfo) {
    Napi::Object result = Napi::Object::New(env);
    result.Set(Napi::String::New(env, "title"), getNapiStringFromLPWSTR(env, processInfo.title));
    result.Set(Napi::String::New(env, "platform"), Napi::String::New(env, "windows"));
    result.Set(Napi::String::New(env, "url"), getNapiStringFromLPWSTR(env, processInfo.url));
    Napi::Object owner = Napi::Object::New(env);
    owner.Set(Napi::String::New(env, "processId"), Napi::Number::New(env, processInfo.processId));
    owner.Set(Napi::String::New(env, "path"), getNapiStringFromLPWSTR(env, processInfo.processPath));
    owner.Set(Napi::String::New(env, "name"), getNapiStringFromLPWSTR(env, processInfo.processName));
    result.Set(Napi::String::New(env, "owner"), owner);

    return result;
}

Napi::Value windows(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    HWND hwnd = GetForegroundWindow();
    if (hwnd == NULL) {
        Napi::TypeError::New(env, "Unable to get window handle").ThrowAsJavaScriptException();
        return env.Null();
    }

    int windowTitleLength = GetWindowTextLengthW(hwnd);
    LPWSTR windowTitle = new WCHAR[windowTitleLength + 1];
    GetWindowTextW(hwnd, windowTitle, windowTitleLength + 1);

    DWORD processId = getProcessId(hwnd);
    if (processId <= 0) {
        Napi::TypeError::New(env, "Unable to get process ID").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (processId != processInfo.processId || wcscmp(windowTitle, processInfo.title) != 0) {
        LPWSTR processPath = getProcessPath(hwnd, processId);
        processInfo.title = windowTitle;
        processInfo.processPath = getProcessPath(hwnd, processId);
        processInfo.processName = getFileDescription(processInfo.processPath);
        processInfo.processId = processId;
        processInfo.url = getBrowserUrl(hwnd, processInfo.processName);

        LPWSTR fileName = getFileNameFromPath(processInfo.processPath);
        if (wcscmp(fileName, L"ApplicationFrameHost.exe") == 0) {
            EnumChildWindows(hwnd, enumChildWindowsProc, (LPARAM)&processInfo);
        }

        if (!processInfo.title || !processInfo.title[0]) {
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