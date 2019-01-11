#include "rapidjson/reader.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/rapidjson.h"
#include "zdx-upgrade.h"
#include "curl-data.h"
#include "curl-download.h"
#include "SharedMemory.hpp"
#include <Shlobj.h>
#include "md5.h"
#include "log11.hpp"
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iostream>
#include <codecvt>
#include <string>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <memory>
#include <map>
#include <iomanip>


using namespace std;

#pragma comment( lib, "shell32.lib")

#define LOG_INFO(...)  if(g_log && (g_dns_correction_log != 0)){g_log->log.info( ##__VA_ARGS__);}
#define LOG_ERROR(...) if(g_log && (g_dns_correction_log != 0)){g_log->log.error(##__VA_ARGS__);}

// 共享内存大小是4k
enum class shared_memory_size {
    sms_size = 4096
};
enum class zdx_upgrade_level {
    upgrade_mandatory   = 1,   // 强制更新
    upgrade_recommended = 2,   // 建议更新(不强制更新)
    upgrade_no_need_to  = 3,   // 不更新
};

enum class zdx_upgrade_usability {
    usability_available = 0,  // 可用
    usability_disabled  = 1,  // 该更新被禁用，这个更新不可用
};

enum class zdx_upgrade_status {
    init = 0,                // 初始化状态
    upgrade_start = 1,       // 开始更新
    latest_version = 2,      // 已经是最新版本
    check_info_fail = 3,     // 检查信息失败
    check_info_success = 4,  // 检查信息完成
    downloading = 5,         // 下载中
    download_fail = 6,       // 下载失败
    download_success = 7,    // 下载完成
    installing = 8,          // 安装中
    install_fail = 9,        // 安装失败
    upgrade_success = 10,    // 更新完成
};

HANDLE g_hAvoidMultipleStartMutex = NULL; // 避免多重启动的Mutex
HANDLE g_hSharedMemoryMap = NULL;         // 共享内存
const std::string g_zdx_upgrade_avoid_multiple_start_mutex = "Global\\Zdx_Upgrade_Avoid_Multiple_Startup_Mutex_";

const std::string g_shared_memory_name("zdx_upgrade_shared_memory_");  // 内存映射对象名称
FileLog *g_log = nullptr;        // 日志文件
int g_dns_correction_log = 1;              // 1 写日志， 0 不写日志
const std::string g_zdx_work_path = "ZdxBrowser";
const std::string g_zdx_installer_default_name = "zdx_installer.exe";
const std::string g_log_prefix = "zdx_upgrade";

// 安装包本地的默认值(命令行和共享内存均没传参数)
// 实际下载的url大致内容:url = "http://zdx.s-api.yunvm.com/files/chromium/zdx_installer-win32-1.0.0.17.exe";
const std::string g_default_zdx_upgrade_api_url = "https://zdx.app/api/v1/desktop/update";
const std::string g_default_zdx_upgrade_type = "zdx_browser_win32_upgrade";

// 这个结构体，是未主进程zdx.exe提供数据的，调用zdx_upgrade.exe的参数，通过命令行传递，注意共享内存大小
struct zdx_upgrade_data {
    int        zdx_upgrade_status;                 // 更新状态
    long long  zdx_upgrade_max_file_size;          // 服务端文件(zdx_installer.exe)总大小
    long long  zdx_upgrade_download_size;          // 已经下载大小
    char       zdx_upgrade_md5[32+1];              // 服务端文件md5码
    char       zdx_upgrade_version[32];            // 服务端返回版本号
    char       zdx_upgrade_memo[256];              // 更新描述，如果超过255个字符，截断
    char       zdx_upgrade_filename[MAX_PATH];     // 服务端返回文件名称(只是文件名:zdx_install.exe,或 zdx_install_1.0.0.16.exe，不包含url)
    char       zdx_upgrade_api_url[MAX_PATH];      // 获取自动更新包api地址
    char       zdx_upgrade_type[64];               // 更新包的类型，zdx_browser_win32_upgrade，zdx_browser_win64_upgrade
    char       zdx_upgrade_client_md5[32+1];       // 上次更新包的md5码，保存在配置文件中
    char       zdx_upgrade_current_version[32];    // 客户端版本
    char       zdx_upgrade_client_path[MAX_PATH];  // 客户端安装路径
    char       zdx_upgrade_url[512];               // 安装包下载url地址
    int        zdx_upgrade_mode;                   // 更新调用模式, 1:只获取服务端信息,10:下载更新, 20:本地安装, 30:执行全部过程
    zdx_upgrade_data() {
        memset(this, 0, sizeof(zdx_upgrade_data));
    }
};

std::string GbkToUtf8(const char *src_str)
{
    int len = MultiByteToWideChar(CP_ACP, 0, src_str, -1, NULL, 0);
    wchar_t* wstr = new wchar_t[len + 1];
    memset(wstr, 0, len + 1);
    MultiByteToWideChar(CP_ACP, 0, src_str, -1, wstr, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* str = new char[len + 1];
    memset(str, 0, len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    string strTemp = str;
    if (wstr) delete[] wstr;
    if (str) delete[] str;
    return strTemp;
}

std::string Utf8ToGbk(const char *src_str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, src_str, -1, NULL, 0);
    wchar_t* wszGBK = new wchar_t[len + 1];
    memset(wszGBK, 0, len * 2 + 2);
    MultiByteToWideChar(CP_UTF8, 0, src_str, -1, wszGBK, len);
    len = WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, NULL, 0, NULL, NULL);
    char* szGBK = new char[len + 1];
    memset(szGBK, 0, len + 1);
    WideCharToMultiByte(CP_ACP, 0, wszGBK, -1, szGBK, len, NULL, NULL);
    std::string strTemp(szGBK);
    if (wszGBK) delete[] wszGBK;
    if (szGBK) delete[] szGBK;
    return strTemp;
}

void UnicodeToGB2312(const wstring& wstr, string& result)
{
    int n = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, 0, 0, 0, 0);
    result.resize(n);
    ::WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, (char*)result.c_str(), n, 0, 0);
}

void Utf8ToUnicode(const string& src, wstring& result)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, NULL, 0);
    result.resize(n);
    ::MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, (LPWSTR)result.c_str(), (int)result.length());
}

// 读写共享内存信息
bool read_write_status(struct zdx_upgrade_data& zud, HANDLE hMap, bool is_read = true) {
    if (!hMap) {
        return false;
    }
    HANDLE pBuffer = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!pBuffer) {
        LOG_ERROR("更新程序获取/建立共享内存失败！");
        return false;
    }
    if (is_read) {
        memcpy((void*)&zud, pBuffer, sizeof(zud));
    }
    else {
        memcpy(pBuffer, (void*)&zud, sizeof(zud));
    }
    // 共享内存析构。解除文件映射，关闭内存映射文件对象句柄
    if (pBuffer) {
        ::UnmapViewOfFile(pBuffer);
        pBuffer = NULL;
    }
    return true;
}

// 检查服务端最新版本
int zdx_upgrade_check_info(std::string& download_url, 
    std::string& md5_server, 
    std::string& version_server, 
    std::string& memo, 
    const std::string& api_url, 
    const std::string& upgrade_type, 
    const std::string& md5_client)
{
    long long current_time = (long long)time(NULL);
    std::string timestamp = std::to_string(current_time);
    std::string precode = "timestamptype" + timestamp + upgrade_type;
    std::string code = MD5(precode).toString();
    CurlData cdCheckInfo(api_url, "application/x-www-form-urlencoded; charset=UTF-8");
    cdCheckInfo.PrepareData("timestamp", timestamp);
    cdCheckInfo.PrepareData("type", upgrade_type);
    cdCheckInfo.PrepareData("code", code);

    std::string sheader;
    std::string sbody;
    std::string err;
    bool bRet = cdCheckInfo.post(sheader, sbody, err);
    sheader = Utf8ToGbk(sheader.c_str());
    sbody = Utf8ToGbk(sbody.c_str());
    err = Utf8ToGbk(err.c_str());
    if (!bRet) {
        LOG_ERROR("获取自动更新包的过程出错:", err, ", 消息头:", sheader, ", 消息体:", sbody);
        return -1;
    }

    rapidjson::Document d;
    d.Parse(sbody.c_str());
    if (d.HasParseError())
    {
        rapidjson::ParseErrorCode code = d.GetParseError();
        LOG_ERROR("获取自动更新包的json文件解析不正确:", sbody);
        return -2;
    }
    int rt = 0;
    std::string error;
    if (d.HasMember("rt") && d["rt"].IsInt()) {
        rapidjson::Value& ret = d["rt"];
        rt = ret.GetInt();
    }
    if (d.HasMember("error") && d["error"].IsString()) {
        rapidjson::Value& err = d["error"];
        error = err.GetString();
    }
    if (rt < 1) {
        LOG_ERROR("获得安装包回复内容有异常:", error);
        return -3;
    }
    if (!(d.HasMember("data") && d["data"].IsObject())) {
        LOG_ERROR("获得安装包回复内容有异常:没有data对象!", sbody);
        return -4;
    }
    rapidjson::Value& data = d["data"];

    std::string zdx_upgrade_type;
    std::string zdx_upgrade_url;
    std::string zdx_upgrade_level;
    std::string zdx_upgrade_usability;

    if (data.HasMember("type") && data["type"].IsString()) {
        rapidjson::Value& type = data["type"];
        zdx_upgrade_type = type.GetString();
    }
    if (data.HasMember("url") && data["url"].IsString()) {
        rapidjson::Value& url = data["url"];
        zdx_upgrade_url = url.GetString();
    }
    if (data.HasMember("version") && data["version"].IsString()) {
        rapidjson::Value& version = data["version"];
        version_server = version.GetString();
    }
    if (data.HasMember("md5_str") && data["md5_str"].IsString()) {
        rapidjson::Value& md5_str = data["md5_str"];
        md5_server = md5_str.GetString();
    }
    if (data.HasMember("memo") && data["memo"].IsString()) {
        rapidjson::Value& zmemo = data["memo"];
        memo = zmemo.GetString();
    }
    if (data.HasMember("status") && data["status"].IsString()) {
        rapidjson::Value& status = data["status"];
        zdx_upgrade_level = status.GetString();
    }
    if (data.HasMember("disabled") && data["disabled"].IsString()) {
        rapidjson::Value& disabled = data["disabled"];
        zdx_upgrade_usability = disabled.GetString();
    }
    int level = std::atoi(zdx_upgrade_level.c_str());
    int usability = std::atoi(zdx_upgrade_usability.c_str());
    if (!((level == (int)zdx_upgrade_level::upgrade_mandatory ||
        level == (int)zdx_upgrade_level::upgrade_recommended) &&
        (usability == (int)zdx_upgrade_usability::usability_available))) {
        LOG_ERROR("获取自动更新安装包内容不可用！ ", sbody);
        return -5;
    }
    if (md5_client.length() > 0 && md5_client.compare(md5_server) == 0) {
        LOG_INFO("本地当前版本已经是最新版本");
        return 1;
    }

    download_url = zdx_upgrade_url;

    return 0;
}

// 下载服务端最新版本
int zdx_upgrade_download(const std::string& download_url, const std::string& local_filename) {
    CurlDownload CurlDownload(download_url, "application/x-www-form-urlencoded; charset=UTF-8");
    long long upgrade_max_file_size = CurlDownload.GetMaxDownloadSize();
    if (upgrade_max_file_size == 0) { // 更新包大小为0

        return -1;
    }
    CurlDownload.PrepareDownloadData(local_filename);
    bool bRet = CurlDownload.Download();
    if (!bRet) { // 下载失败
        return -2;
    }
    return 0;
}

// 安装从服务端下载的最新版本
int zdx_upgrade_installer(const std::string& local_filename, const std::string& md5_server) {
    // 校验自动更新包的md5码
    std::ifstream install_file;
    install_file.open(local_filename.c_str(), ios_base::in | ios_base::binary);
    if (!install_file.is_open()) {
        std::cout << "\n读文件[" << local_filename.c_str() << "]打开失败！\n" << std::endl;
        install_file.close();
        // 自动更新包校验打开失败
        LOG_ERROR("紫丁香浏览器自动更新包文件打开失败！");
        return -2;
    }
    MD5 md5(install_file);
    std::string check_md5 = md5.toString();
    if (check_md5.compare(md5_server) != 0) {
        // 自动更新包校验失败
        LOG_ERROR("紫丁香浏览器自动更新包校验失败！md5码不匹配。");
        return -3;
    }

    // 运行自动更新包
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    char cmdline[] = "";
    BOOL bRet = ::CreateProcessA(local_filename.c_str(), cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (!bRet) {
        DWORD dwErrCode = GetLastError();
        char* lpErrMsg = NULL;
        DWORD error = ERROR_DS_OBJ_STRING_NAME_EXISTS;
        ::FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            dwErrCode,
            0,
            (LPSTR)&lpErrMsg,
            0,
            NULL);
        LOG_ERROR("自动更新运行失败；错误码:",dwErrCode, ", 错误信息:", lpErrMsg);
        if (lpErrMsg) {
            ::LocalFree(lpErrMsg);
            lpErrMsg = NULL;
        }
    }
    ::CloseHandle(pi.hThread);

    DWORD exit_code = ERROR_SUCCESS;
    DWORD wr = ::WaitForSingleObject(pi.hProcess, INFINITE);
    if (WAIT_OBJECT_0 != wr || !::GetExitCodeProcess(pi.hProcess, &exit_code)) {
        // WAIT_FOR_PROCESS_FAILED
        return -7;
    }

    ::CloseHandle(pi.hProcess);

    return 0;
}


int ParseCmdLine(const wchar_t* lpCmdLine, std::map<std::wstring, std::wstring>& pMapCmdLine)
{
    int nArgs = 0;
    LPWSTR * szArglist = CommandLineToArgvW(lpCmdLine, &nArgs);

    for (int i = 0; i < nArgs; i++)
    {
        if (wcsncmp(L"-", szArglist[i], 1) != 0)
        {
            continue;
        }
        if (i + 1 < nArgs)
        {
            if (wcsncmp(L"-", szArglist[i + 1], 1) != 0)
            {
                std::wstring key = szArglist[i];
                while (key.find(L"-") == 0) {
                    key = key.replace(0, 1, L"");
                }
                pMapCmdLine.insert(std::make_pair(key, szArglist[i + 1]));
                i++;
                continue;
            }
        }
        pMapCmdLine.insert(std::make_pair(szArglist[i], L"1"));
    }
    LocalFree(szArglist);
    return 0;
}

int main(int argc, char* argv[])
{
    std::string upgrade_api_url;          // 获取自动更新包api地址
    std::string upgrade_type;             // 更新包的类型，zdx_browser_win32_upgrade，zdx_browser_win64_upgrade
    std::string upgrade_client_md5;       // 上次更新包的md5码，保存在配置文件中
    std::string upgrade_client_path;      // 本地安装路径
    std::string upgrade_filename;         // 更新文件名称
    std::string upgrade_current_version;  // 客户端当前版本
    std::string upgrade_url;              // 安装包下载地址
    std::string upgrade_md5;              // 从server获取的md5码，供本地安装时使用
    int upgrade_mode;                     // 更新调用模式, 1:只获取服务端信息,10:下载更新, 20:本地安装, 30:执行全部过程

    upgrade_mode = 30;

    int nRet = EXIT_SUCCESS;
    char path[MAX_PATH] = { 0 };
    ::GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string cur_path = path;
    cur_path = cur_path.substr(0, cur_path.rfind("\\") + 1);
    if (g_dns_correction_log) {
        std::string logfile = cur_path + g_log_prefix;
        logfile += ".log";
        g_log = new FileLog(logfile);
        LOG_INFO("准备运行更新程序。");
    }

    // 防止多重启动
    g_hAvoidMultipleStartMutex = ::CreateMutexA(NULL, false, g_zdx_upgrade_avoid_multiple_start_mutex.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        ::CloseHandle(g_hAvoidMultipleStartMutex);
        g_hAvoidMultipleStartMutex = NULL;
        // 设置标志位，多重启动，已退出
        std::cout << "已经启动，自动退出" << std::endl;
        LOG_INFO("自动更新已经运行！");
        return -11;
    }

    // 解析命令行参数,参数的key只支持小写
    // zdx_upgrade.exe -url "https://zdx.app/api/v1/desktop/update" -type "zdx_browser_win32_upgrade" -md5 "9cf6428eb6fd54c298761fdc7379bb54" -path "C:\Users\soft\AppData\Local\ZdxBrowser"
    std::map<std::wstring, std::wstring> mapCmd;
    ParseCmdLine(GetCommandLineW(), mapCmd);
    std::wstring value;
    std::string ascii_value;
    size_t nCount = mapCmd.size();
    std::map<std::wstring, std::wstring>::iterator it = mapCmd.begin();
    for (; it != mapCmd.end(); ++it)
    {
        std::wstring key = it->first.c_str();
        if (key.compare(L"url") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_api_url);
        }
        if (key.compare(L"type") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_type);
        }
        if (key.compare(L"md5") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_md5);
        }
        if (key.compare(L"client_md5") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_client_md5);
        }
        if (key.compare(L"path") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_client_path);
        }
        if (key.compare(L"filename") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_filename);
        }
        if (key.compare(L"current_version") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_current_version);
        }
        if (key.compare(L"download_url") == 0) {
            value = it->second.c_str();
            UnicodeToGB2312(value, upgrade_url);
        }
        if (key.compare(L"mode") == 0) {
            value = it->second.c_str();
            std::string mode;
            UnicodeToGB2312(value, mode);
            if (mode.length() == 0) {
                mode = "30";
            }
            upgrade_mode = std::atoi(mode.c_str());
        }
    }
    
    g_hSharedMemoryMap = ::OpenFileMapping(FILE_MAP_ALL_ACCESS, 0, g_shared_memory_name.c_str());
    if (!g_hSharedMemoryMap)
    {
        LOG_INFO("共享内存操作.");
        g_hSharedMemoryMap = ::CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (int)shared_memory_size::sms_size, g_shared_memory_name.c_str());
        if (!g_hSharedMemoryMap) {
            LOG_ERROR("更新程序获取/建立共享内存失败！");
            return -12;
        }
        //// 本程序测试, test begin
        //HANDLE pBuffer = ::MapViewOfFile(g_hSharedMemoryMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        //if (!pBuffer) {
        //    LOG_ERROR("更新程序获取/建立共享内存失败！");
        //    return -13;
        //}
        //struct zdx_upgrade_data zud;
        //std::string upgrade_api_url = "https://zdx.app/api/v1/desktop/update";
        //std::string upgrade_type = "zdx_browser_win32_upgrade";
        //std::string upgrade_client_md5 = "";
        //std::string upgrade_client_path = "C:\\Users\\soft\\AppData\\Local\\ZdxBrowser\\Aplication";

        //memcpy(zud.zdx_upgrade_api_url, upgrade_api_url.c_str(), upgrade_api_url.length());
        //memcpy(zud.zdx_upgrade_type, upgrade_type.c_str(), upgrade_type.length());
        //memcpy(zud.zdx_upgrade_client_md5, upgrade_client_md5.c_str(), upgrade_client_md5.length());
        //memcpy(zud.zdx_upgrade_client_path, upgrade_client_path.c_str(), upgrade_client_path.length());
        //zud.zdx_upgrade_mode = 30;
        //memcpy(pBuffer, (void*)&zud, sizeof(zud));
        //if (pBuffer) {
        //    ::UnmapViewOfFile(pBuffer);
        //    pBuffer = NULL;
        //}
        //// 本程序测试, test end
    }

    // 读共享内存信息
    struct zdx_upgrade_data zud;
    read_write_status(zud, g_hSharedMemoryMap, true);
    if (strlen(zud.zdx_upgrade_api_url) > 0) {
        upgrade_api_url = zud.zdx_upgrade_api_url;
    }
    if (strlen(zud.zdx_upgrade_type) > 0) {
        upgrade_type = zud.zdx_upgrade_type;
    }
    if (strlen(zud.zdx_upgrade_md5) > 0) {
        upgrade_md5 = zud.zdx_upgrade_md5;
    }
    if (strlen(zud.zdx_upgrade_client_md5) > 0) {
        upgrade_client_md5 = zud.zdx_upgrade_client_md5;
    }
    if (strlen(zud.zdx_upgrade_client_path) > 0) {
        upgrade_client_path = zud.zdx_upgrade_client_path;
    }
    if (strlen(zud.zdx_upgrade_filename) > 0) {
        upgrade_filename = zud.zdx_upgrade_filename;
    }
    if (strlen(zud.zdx_upgrade_current_version) > 0) {
        upgrade_current_version = zud.zdx_upgrade_current_version;
    }
    if (strlen(zud.zdx_upgrade_url) > 0) {
        upgrade_url = zud.zdx_upgrade_url;
    }
    if (zud.zdx_upgrade_mode >= 1) {
        upgrade_mode = zud.zdx_upgrade_mode;
    }

    // 默认参数
    if (upgrade_api_url.length() == 0) {
        upgrade_api_url = g_default_zdx_upgrade_api_url;
    }
    if (upgrade_type.length() == 0) {
        upgrade_type = g_default_zdx_upgrade_type;
    }
    if (upgrade_client_path.length() == 0) {
        upgrade_client_path = cur_path;
    }
    if (upgrade_filename.length() == 0) {
        upgrade_filename = g_zdx_installer_default_name;
    }

    {
        zud.zdx_upgrade_status = (int)zdx_upgrade_status::upgrade_start;
        std::string info = GbkToUtf8("开始更新");
        memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
        memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
        read_write_status(zud, g_hSharedMemoryMap, false);
        LOG_INFO("开始更新.");
    }

    // 获得服务配置信息
    switch (upgrade_mode) {
    case 1:
        {
            std::string md5_server = "";
            std::string version_server = "";
            std::string download_url;
            std::string memo = "";
            nRet = zdx_upgrade_check_info(download_url, md5_server, version_server, memo, upgrade_api_url, upgrade_type, upgrade_client_md5);
            if (md5_server.length() > 0) {
                memset(zud.zdx_upgrade_md5, 0, sizeof(zud.zdx_upgrade_md5));
                memcpy(zud.zdx_upgrade_md5, md5_server.c_str(), md5_server.length());
            }
            if (version_server.length() > 0) {
                memset(zud.zdx_upgrade_version, 0, sizeof(zud.zdx_upgrade_version));
                memcpy(zud.zdx_upgrade_version, version_server.c_str(), version_server.length());
            }
            if (memo.length() > 0) {
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, memo.c_str(), memo.length() > 255 ? 255 : memo.length());
            }
            std::string installer_filename;
            if (download_url.length() > 0) {
                size_t idx = download_url.rfind('/');
                if (idx != std::string::npos) {
                    installer_filename = download_url.substr(idx + 1);
                }
            }
            if (installer_filename.length() == 0) {
                installer_filename = g_zdx_installer_default_name;
            }
            if (installer_filename.length() > 0) {
                memset(zud.zdx_upgrade_filename, 0, sizeof(zud.zdx_upgrade_filename));
                memcpy(zud.zdx_upgrade_filename, installer_filename.c_str(), installer_filename.length());
            }
            if (download_url.length() > 0) {
                memset(zud.zdx_upgrade_url, 0, sizeof(zud.zdx_upgrade_url));
                memcpy(zud.zdx_upgrade_url, download_url.c_str(), download_url.length());
            }

            if (nRet == 0) {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::check_info_success;
                std::string info = GbkToUtf8("检查版本信息完成");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("检查版本信息完成.");
            }
            else if (nRet == 1) { // 已经是最新版本
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::latest_version;
                std::string info = GbkToUtf8("本地已经是最新版本");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("本地已经是最新版本.");
            }
            else { // 错误处理。这里不需要多次检查，只要返回给zdx.exe状态即可。
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::check_info_fail;
                std::string error = GbkToUtf8("获取版本信息失败!");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, error.c_str(), error.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_ERROR("获取版本信息失败.");
            }
        }
    break;
    case 10:
        {
            if (upgrade_url.length() == 0) {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::download_fail;
                std::string info = GbkToUtf8("没有指定下载url");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_ERROR("没有指定下载url.");
                break;
            }
            zud.zdx_upgrade_status = (int)zdx_upgrade_status::downloading;
            std::string info = GbkToUtf8("下载中");
            memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
            memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
            read_write_status(zud, g_hSharedMemoryMap, false);
            LOG_INFO("下载中.");

            std::string local_filename = cur_path + upgrade_filename;
            nRet = zdx_upgrade_download(upgrade_url, local_filename);
            if (nRet != 0) { // 安装
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::download_fail;
                std::string info = GbkToUtf8("下载失败");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_ERROR("下载失败.");
            }
            else {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::download_success;
                std::string info = GbkToUtf8("下载完成");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("下载完成.");
            }
        }
    break;
    case 20:
        {
            zud.zdx_upgrade_status = (int)zdx_upgrade_status::installing;
            std::string info = GbkToUtf8("正在本地安装");
            memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
            memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
            read_write_status(zud, g_hSharedMemoryMap, false);
            LOG_INFO("正在本地安装.");
            std::string local_filename = cur_path + upgrade_filename;
            nRet = zdx_upgrade_installer(local_filename, upgrade_md5);
            if (nRet != 0) {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::install_fail;
                std::string info = GbkToUtf8("本地安装失败");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_ERROR("本地安装失败.");
            }
            else {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::upgrade_success;
                std::string info = GbkToUtf8("本地安装成功");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("本地安装成功.");
            }
        }
        break;
    default:
        {
            std::string md5_server = "";
            std::string version_server = "";
            std::string download_url;
            std::string memo = "";
            nRet = zdx_upgrade_check_info(download_url, md5_server, version_server, memo, upgrade_api_url, upgrade_type, upgrade_client_md5);
            if (md5_server.length() > 0) {
                memset(zud.zdx_upgrade_md5, 0, sizeof(zud.zdx_upgrade_md5));
                memcpy(zud.zdx_upgrade_md5, md5_server.c_str(), md5_server.length());
            }
            if (version_server.length() > 0) {
                memset(zud.zdx_upgrade_version, 0, sizeof(zud.zdx_upgrade_version));
                memcpy(zud.zdx_upgrade_version, version_server.c_str(), version_server.length());
            }
            if (memo.length() > 0) {
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, memo.c_str(), memo.length() > 255 ? 255 : memo.length());
            }
            std::string installer_filename;
            if (download_url.length() > 0) {
                size_t idx = download_url.rfind('/');
                if (idx != std::string::npos) {
                    installer_filename = download_url.substr(idx + 1);
                }
            }
            if (installer_filename.length() == 0) {
                installer_filename = g_zdx_installer_default_name;
            }
            if (installer_filename.length() > 0) {
                memset(zud.zdx_upgrade_filename, 0, sizeof(zud.zdx_upgrade_filename));
                memcpy(zud.zdx_upgrade_filename, installer_filename.c_str(), installer_filename.length());
            }
            if (download_url.length() > 0) {
                memset(zud.zdx_upgrade_url, 0, sizeof(zud.zdx_upgrade_url));
                memcpy(zud.zdx_upgrade_url, download_url.c_str(), download_url.length());
            }

            if (nRet == 0) {
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::downloading;
                std::string info = GbkToUtf8("下载中");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("下载中.");
                std::string local_filename = cur_path + installer_filename;
                // 这里应该还有些问题，怎么指定安装目录？？？
                nRet = zdx_upgrade_download(download_url, local_filename);
                if (nRet != 0) { // 安装
                    zud.zdx_upgrade_status = (int)zdx_upgrade_status::download_fail;
                    std::string info = GbkToUtf8("下载失败");
                    memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                    memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                    read_write_status(zud, g_hSharedMemoryMap, false);
                    LOG_ERROR("下载失败.");
                }
                else {
                    zud.zdx_upgrade_status = (int)zdx_upgrade_status::download_success;
                    std::string info = GbkToUtf8("下载完成");
                    memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                    memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                    read_write_status(zud, g_hSharedMemoryMap, false);
                    LOG_INFO("下载完成.");

                    zud.zdx_upgrade_status = (int)zdx_upgrade_status::installing;
                    info = GbkToUtf8("正在本地安装");
                    memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                    memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                    read_write_status(zud, g_hSharedMemoryMap, false);
                    LOG_INFO("正在本地安装.");
                    nRet = zdx_upgrade_installer(local_filename, md5_server);
                    if (nRet != 0) {
                        zud.zdx_upgrade_status = (int)zdx_upgrade_status::install_fail;
                        std::string info = GbkToUtf8("本地安装失败");
                        memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                        memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                        read_write_status(zud, g_hSharedMemoryMap, false);
                        LOG_ERROR("本地安装失败.");
                    }
                    else {
                        zud.zdx_upgrade_status = (int)zdx_upgrade_status::upgrade_success;
                        std::string info = GbkToUtf8("本地安装成功");
                        memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                        memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                        read_write_status(zud, g_hSharedMemoryMap, false);
                        LOG_INFO("本地安装成功.");
                    }
                }
            }
            else if (nRet == 1) { // 已经是最新版本
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::latest_version;
                std::string info = GbkToUtf8("当前已经是最新版本");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, info.c_str(), info.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_INFO("当前已经是最新版本.");
            }
            else { // 错误处理。这里不需要多次检查，只要返回给zdx.exe状态即可。
                zud.zdx_upgrade_status = (int)zdx_upgrade_status::check_info_fail;
                std::string error = GbkToUtf8("获取版本信息失败!");
                memset(zud.zdx_upgrade_memo, 0, sizeof(zud.zdx_upgrade_memo));
                memcpy(zud.zdx_upgrade_memo, error.c_str(), error.length());
                read_write_status(zud, g_hSharedMemoryMap, false);
                LOG_ERROR("获取版本信息失败.");
            }
        }
        break;
    }

    
    if (g_hSharedMemoryMap) {
        ::CloseHandle(g_hSharedMemoryMap);
        g_hSharedMemoryMap = NULL;
    }

    LOG_INFO("更新程序运行结束; 结束码:", nRet);

    // 防止多重启动析构
    if (g_hAvoidMultipleStartMutex) {
        ::CloseHandle(g_hAvoidMultipleStartMutex);
        g_hAvoidMultipleStartMutex = NULL;
    }
    return nRet;
}
